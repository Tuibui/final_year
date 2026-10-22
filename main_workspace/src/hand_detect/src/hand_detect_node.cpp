#include <gst/gst.h>
#include <gstnvdsmeta.h>
#include <nvdsmeta.h>
#include <rclcpp/rclcpp.hpp>
#include <std_msgs/msg/string.hpp>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <filesystem>
#include <iterator>
#include <map>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#include "hand_detect/msg/inspection.hpp"
#include "hand_detect/result.hpp"

using namespace std::chrono_literals;
using Steady = std::chrono::steady_clock;
using Inspection = hand_detect::msg::Inspection;

struct Snapshot
{
  Inspection message;
  Steady::time_point created;
  uint64_t generation;
};

class HandDetect : public rclcpp::Node
{
public:
  HandDetect() : Node("hand_detect")
  {
    source_type_ = declare_parameter<std::string>("source_type", "rtsp");
    uri_ = declare_parameter<std::string>("rtsp_url", "");
    const auto file_uri = declare_parameter<std::string>("file_uri", "");
    if (uri_.empty()) {
      const char * env = std::getenv("HAND_DETECT_RTSP_URL");
      if (env) {uri_ = env;}
    }
    if (source_type_ == "file") {uri_ = file_uri;}
    display_ = declare_parameter<bool>("display", true);
    infer_ = declare_parameter<bool>("inference_enabled", true);
    const char * data_dir = std::getenv("HAND_DETECT_DATA_DIR");
    infer_config_ = declare_parameter<std::string>("infer_config",
      data_dir ? std::string(data_dir) + "/config/infer.local.txt" : "");
    hand_id_ = declare_parameter<int>("hand_class_id", 2);
    threshold_ = declare_parameter<double>("confidence_threshold", 0.5);
    roi_top_fraction_ = declare_parameter<double>("roi_top_fraction", 0.5);
    width_ = declare_parameter<int>("width", 640);
    height_ = declare_parameter<int>("height", 480);
    latency_ = declare_parameter<int>("rtsp_latency_ms", 100);
    stale_ms_ = declare_parameter<int>("stale_timeout_ms", 2000);
    max_age_ms_ = declare_parameter<int>("max_pipeline_age_ms", 500);
    retry_ms_ = declare_parameter<int>("reconnect_delay_ms", 2000);
    frame_id_ = declare_parameter<std::string>("frame_id", "hikvision_optical_frame");
    if (source_type_ != "rtsp" && source_type_ != "test" && source_type_ != "file") {
      throw std::invalid_argument("source_type must be rtsp, file, or test");
    }
    if (source_type_ == "rtsp" && uri_.rfind("rtsp://", 0) != 0) {
      throw std::invalid_argument("Set HAND_DETECT_RTSP_URL or the rtsp_url parameter");
    }
    if (source_type_ == "file" && uri_.rfind("file://", 0) != 0) {
      throw std::invalid_argument("file_uri must be an absolute file:// URI");
    }
    if (width_ < 32 || height_ < 32 || latency_ < 0 || stale_ms_ < 100 ||
      retry_ms_ < 100 || max_age_ms_ < 1 || hand_id_ < 0 ||
      !std::isfinite(threshold_) || threshold_ < 0 || threshold_ > 1 ||
      !std::isfinite(roi_top_fraction_) || roi_top_fraction_ <= 0 || roi_top_fraction_ > 1)
    {
      throw std::invalid_argument("Invalid dimensions, timeout, class ID, threshold, or ROI fraction");
    }
    if (infer_) {validate_infer_config();}
    // Best-effort, volatile QoS avoids accumulating old inspection results.
    auto qos = rclcpp::QoS(rclcpp::KeepLast(1)).best_effort().durability_volatile();
    result_pub_ = create_publisher<Inspection>("~/result", qos);
    status_pub_ = create_publisher<std_msgs::msg::String>("~/status", qos);
    gst_init(nullptr, nullptr);
  }

  ~HandDetect() override {stop();}

  void start()
  {
    publisher_thread_ = std::thread(&HandDetect::publish_loop, this);
    try {
      pipeline_thread_ = std::thread(&HandDetect::pipeline_loop, this);
    } catch (...) {
      stop();
      throw;
    }
    RCLCPP_INFO(get_logger(),
      "Started: count hands per frame in top %.0f%%; publish hand = N; unavailable = UNKNOWN",
      roi_top_fraction_ * 100);
  }

  void stop()
  {
    stopping_ = true;
    wake_.notify_all();
    if (pipeline_thread_.joinable()) {pipeline_thread_.join();}
    if (publisher_thread_.joinable()) {publisher_thread_.join();}
  }

private:
  void validate_infer_config()
  {
    if (!std::filesystem::is_regular_file(infer_config_)) {
      throw std::invalid_argument("infer_config is missing; run setup_model.py first");
    }
    GKeyFile * config = g_key_file_new();
    GError * error = nullptr;
    const bool loaded = g_key_file_load_from_file(
      config, infer_config_.c_str(), G_KEY_FILE_NONE, &error);
    if (error) {g_error_free(error);}
    // The probe expects full-frame detector metadata on every inferred frame.
    const auto read = [config](const char * key) {
        GError * e = nullptr;
        int value = g_key_file_get_integer(config, "property", key, &e);
        if (e) {g_error_free(e); return -1;}
        return value;
      };
    bool valid = loaded && read("network-type") == 0 && read("process-mode") == 1 &&
      read("interval") == 0 && read("batch-size") == 1 && read("gie-unique-id") == 1;
    int classes = read("num-detected-classes");
    valid = valid && classes > hand_id_;
    g_key_file_unref(config);
    if (!valid) {
      throw std::invalid_argument(
              "Use detector network-type=0, process-mode=1, interval=0, batch-size=1, "
              "gie-unique-id=1, and a valid hand_class_id");
    }
  }

  GstElement * add(const char * factory, const char * name)
  {
    GstElement * element = gst_element_factory_make(factory, name);
    if (!element) {throw std::runtime_error(std::string("Missing GStreamer plugin: ") + factory);}
    if (!gst_bin_add(GST_BIN(pipeline_), element)) {
      gst_object_unref(element);
      throw std::runtime_error("Could not add pipeline element");
    }
    return element;
  }

  static void link(GstElement * a, GstElement * b)
  {
    if (!gst_element_link(a, b)) {throw std::runtime_error("Could not link pipeline elements");}
  }

  static void caps(GstElement * filter, const std::string & text)
  {
    GstCaps * value = gst_caps_from_string(text.c_str());
    if (!value) {throw std::runtime_error("Invalid caps");}
    g_object_set(filter, "caps", value, nullptr);
    gst_caps_unref(value);
  }

  static void on_source_setup(GstElement *, GstElement * source, gpointer data)
  {
    auto * self = static_cast<HandDetect *>(data);
    auto * klass = G_OBJECT_GET_CLASS(source);
    if (g_object_class_find_property(klass, "latency")) {
      g_object_set(source, "latency", self->latency_, nullptr);
    }
    if (g_object_class_find_property(klass, "protocols")) {
      // GstRTSPLowerTrans TCP is bit 2.
      g_object_set(source, "protocols", 4, "drop-on-latency", TRUE,
        "tcp-timeout", static_cast<guint64>(5000000), nullptr);
    }
  }

  static void on_pad_added(GstElement *, GstPad * pad, gpointer data)
  {
    auto * self = static_cast<HandDetect *>(data);
    GstCaps * current = gst_pad_get_current_caps(pad);
    if (!current) {current = gst_pad_query_caps(pad, nullptr);}
    if (!current || gst_caps_is_empty(current) || gst_caps_is_any(current)) {
      if (current) {gst_caps_unref(current);}
      return;
    }
    const auto * structure = gst_caps_get_structure(current, 0);
    const bool video = g_str_has_prefix(gst_structure_get_name(structure), "video/x-raw");
    const bool nvmm = gst_caps_features_contains(
      gst_caps_get_features(current, 0), "memory:NVMM");
    if (video) {
      if (!nvmm) {
        self->source_failed_ = true;
      } else {
        GstPad * sink = gst_element_get_static_pad(self->input_queue_, "sink");
        if (!gst_pad_is_linked(sink) && gst_pad_link(pad, sink) != GST_PAD_LINK_OK) {
          self->source_failed_ = true;
        }
        gst_object_unref(sink);
      }
    }
    gst_caps_unref(current);
  }

  static GstPadProbeReturn input_probe(GstPad *, GstPadProbeInfo * info, gpointer data)
  {
    auto * self = static_cast<HandDetect *>(data);
    GstBuffer * buffer = GST_PAD_PROBE_INFO_BUFFER(info);
    if (!buffer || !GST_BUFFER_PTS_IS_VALID(buffer)) {return GST_PAD_PROBE_OK;}
    try {
      std::unique_lock<std::mutex> guard(self->timing_mutex_, std::try_to_lock);
      if (guard.owns_lock()) {
        self->arrival_[GST_BUFFER_PTS(buffer)] = Steady::now();
        while (self->arrival_.size() > 256) {self->arrival_.erase(self->arrival_.begin());}
      }
    } catch (...) {
      self->source_failed_ = true;
    }
    return GST_PAD_PROBE_OK;
  }

  static GstPadProbeReturn result_probe(GstPad *, GstPadProbeInfo * info, gpointer data)
  {
    auto * self = static_cast<HandDetect *>(data);
    try {
      self->inspect(GST_PAD_PROBE_INFO_BUFFER(info));
    } catch (...) {
      // Exceptions must never escape a C callback.
      self->source_failed_ = true;
    }
    return GST_PAD_PROBE_OK;
  }

  void inspect(GstBuffer * buffer)
  {
    const auto begin = Steady::now();
    if (!buffer) {return;}
    auto * batch = gst_buffer_get_nvds_batch_meta(buffer);
    if (!batch || !batch->frame_meta_list) {source_failed_ = true; return;}
    for (NvDsMetaList * entry = batch->frame_meta_list; entry; entry = entry->next) {
      auto * frame = static_cast<NvDsFrameMeta *>(entry->data);
      Snapshot snapshot;
      snapshot.created = begin;
      snapshot.generation = generation_;
      auto & msg = snapshot.message;
      msg.header.stamp = now();
      msg.header.frame_id = frame_id_;
      msg.frame_number = sequence_.fetch_add(1);
      msg.pts_valid = GST_CLOCK_TIME_IS_VALID(frame->buf_pts);
      msg.stream_pts_ns = msg.pts_valid ? frame->buf_pts : 0;
      msg.inference_done = infer_ && frame->bInferDone;
      msg.pipeline_age_ms = -1.0;
      {
        std::unique_lock<std::mutex> lock(timing_mutex_, std::try_to_lock);
        if (lock.owns_lock()) {
          auto it = arrival_.find(frame->buf_pts);
          if (it != arrival_.end()) {
            msg.pipeline_age_ms =
              std::chrono::duration<double, std::milli>(begin - it->second).count();
            arrival_.erase(arrival_.begin(), std::next(it));
          }
        }
      }
      for (NvDsMetaList * item = frame->obj_meta_list; item;) {
        // Removing object metadata invalidates this list entry, so save the next one first.
        NvDsMetaList * next = item->next;
        auto * object = static_cast<NvDsObjectMeta *>(item->data);
        if (msg.inference_done && object->unique_component_id == 1 && hand_detect::accepted_hand(
            object->class_id, hand_id_, object->confidence, static_cast<float>(threshold_)) &&
          hand_detect::center_in_top_region(
            object->rect_params.left, object->rect_params.top,
            object->rect_params.width, object->rect_params.height,
            static_cast<float>(width_), static_cast<float>(height_), roi_top_fraction_))
        {
          ++msg.hand_count;
          msg.max_confidence = std::max(msg.max_confidence, object->confidence);
          if (display_) {
            g_free(object->text_params.display_text);
            object->text_params.display_text = g_strdup("hand");
          }
        } else {
          // Keep only accepted ROI hands for the downstream OSD.
          nvds_remove_obj_meta_from_frame(frame, object);
        }
        item = next;
      }
      const bool late = msg.pipeline_age_ms > max_age_ms_;
      msg.status = hand_detect::classify(infer_, msg.inference_done, msg.hand_count, late);
      msg.reason = !infer_ ? "inference_disabled" :
        !msg.inference_done ? "not_inferred" : late ? "pipeline_stale" :
        msg.hand_count ? "hand_detected" : "no_hand";
      if (display_) {draw_region(batch, frame, msg.status);}
      msg.probe_us = std::chrono::duration<double, std::micro>(Steady::now() - begin).count();
      last_frame_ticks_ = begin.time_since_epoch().count();
      if (!mailbox_.put(std::move(snapshot))) {++busy_drops_;}
      wake_.notify_one();
    }
  }

  void draw_region(NvDsBatchMeta * batch, NvDsFrameMeta * frame, const std::string & status)
  {
    auto * overlay = nvds_acquire_display_meta_from_pool(batch);
    if (!overlay) {return;}
    overlay->num_rects = 1;
    auto & rect = overlay->rect_params[0];
    // Object rectangles use the configured nvstreammux output coordinates.
    rect.left = 0;
    rect.top = 0;
    rect.width = static_cast<float>(width_);
    rect.height = static_cast<float>(height_ * roi_top_fraction_);
    rect.border_width = 2;
    rect.border_color = {0.0, 1.0, 1.0, 1.0};
    rect.has_bg_color = 0;
    overlay->num_labels = 1;
    static char font_name[] = "Sans";
    auto & count = overlay->text_params[0];
    count.display_text = g_strdup(status.c_str());
    count.x_offset = 12;
    count.y_offset = 40;
    count.font_params.font_name = font_name;
    count.font_params.font_size = 24;
    count.font_params.font_color = {1.0, 1.0, 1.0, 1.0};
    count.set_bg_clr = 1;
    count.text_bg_clr = {0.0, 0.0, 0.0, 0.7};
    nvds_add_display_meta_to_frame(frame, overlay);
  }

  void build_pipeline()
  {
    pipeline_ = gst_pipeline_new("hand-detect-pipeline");
    if (!pipeline_) {throw std::runtime_error("Could not allocate pipeline");}
    input_queue_ = add("queue", "latest-input");
    g_object_set(input_queue_, "max-size-buffers", 1U, "max-size-bytes", 0U,
      "max-size-time", static_cast<guint64>(0), "leaky", source_type_ == "file" ? 0 : 2, nullptr);
    if (source_type_ == "test") {
      auto * source = add("videotestsrc", "source");
      g_object_set(source, "is-live", TRUE, "pattern", 2, nullptr);
      auto * input_caps = add("capsfilter", "test-caps");
      caps(input_caps, "video/x-raw,format=I420,width=640,height=480,framerate=15/1");
      auto * converter = add("nvvideoconvert", "test-converter");
      auto * nvmm_caps = add("capsfilter", "test-nvmm");
      caps(nvmm_caps, "video/x-raw(memory:NVMM),format=NV12");
      link(source, input_caps);
      link(input_caps, converter);
      link(converter, nvmm_caps);
      link(nvmm_caps, input_queue_);
    } else {
      auto * source = add("uridecodebin", "source");
      g_object_set(source, "uri", uri_.c_str(), nullptr);
      g_signal_connect(source, "pad-added", G_CALLBACK(on_pad_added), this);
      g_signal_connect(source, "source-setup", G_CALLBACK(on_source_setup), this);
    }
    GstPad * input = gst_element_get_static_pad(input_queue_, "sink");
    gst_pad_add_probe(input, GST_PAD_PROBE_TYPE_BUFFER, input_probe, this, nullptr);
    gst_object_unref(input);
    auto * mux = add("nvstreammux", "mux");
    g_object_set(mux, "batch-size", 1U, "width", static_cast<guint>(width_),
      "height", static_cast<guint>(height_), "live-source", source_type_ != "file",
      "batched-push-timeout", 33000, nullptr);
    mux_pad_ = gst_element_request_pad_simple(mux, "sink_0");
    GstPad * output = gst_element_get_static_pad(input_queue_, "src");
    const bool linked = mux_pad_ && gst_pad_link(output, mux_pad_) == GST_PAD_LINK_OK;
    gst_object_unref(output);
    if (!linked) {throw std::runtime_error("Could not link source to nvstreammux");}
    GstElement * tail = mux;
    if (infer_) {
      auto * infer = add("nvinfer", "hand-inference");
      g_object_set(infer, "config-file-path", infer_config_.c_str(), nullptr);
      link(tail, infer);
      tail = infer;
    }
    GstPad * result = gst_element_get_static_pad(tail, "src");
    gst_pad_add_probe(result, GST_PAD_PROBE_TYPE_BUFFER, result_probe, this, nullptr);
    gst_object_unref(result);
    if (display_) {
      auto * queue = add("queue", "display-queue");
      g_object_set(queue, "max-size-buffers", 1U, "max-size-bytes", 0U,
        "max-size-time", static_cast<guint64>(0), "leaky", 2, nullptr);
      auto * converter = add("nvvideoconvert", "display-converter");
      auto * rgba = add("capsfilter", "rgba");
      caps(rgba, "video/x-raw(memory:NVMM),format=RGBA");
      auto * osd = add("nvdsosd", "osd");
      auto * sink = add("nveglglessink", "display");
      g_object_set(sink, "sync", FALSE, "qos", FALSE, nullptr);
      link(tail, queue);
      link(queue, converter);
      link(converter, rgba);
      link(rgba, osd);
      link(osd, sink);
    } else {
      auto * sink = add("fakesink", "headless");
      g_object_set(sink, "sync", source_type_ == "file", "async", FALSE, nullptr);
      link(tail, sink);
    }
  }

  void cleanup()
  {
    if (!pipeline_) {return;}
    gst_element_set_state(pipeline_, GST_STATE_NULL);
    if (mux_pad_) {
      GstElement * mux = gst_bin_get_by_name(GST_BIN(pipeline_), "mux");
      gst_element_release_request_pad(mux, mux_pad_);
      gst_object_unref(mux_pad_);
      gst_object_unref(mux);
      mux_pad_ = nullptr;
    }
    gst_object_unref(pipeline_);
    pipeline_ = nullptr;
    input_queue_ = nullptr;
    std::lock_guard<std::mutex> guard(timing_mutex_);
    arrival_.clear();
  }

  void pipeline_loop()
  {
    while (!stopping_ && rclcpp::ok()) {
      source_failed_ = false;
      ++generation_;
      reason_ = 0;
      GstBus * bus = nullptr;
      try {
        build_pipeline();
        bus = gst_element_get_bus(pipeline_);
        if (gst_element_set_state(pipeline_, GST_STATE_PLAYING) == GST_STATE_CHANGE_FAILURE) {
          throw std::runtime_error("Pipeline could not enter PLAYING");
        }
        active_ = true;
        last_frame_ticks_ = Steady::now().time_since_epoch().count();
        RCLCPP_INFO(get_logger(), "Pipeline started (%s, inference=%s, display=%s)",
          source_type_.c_str(), infer_ ? "on" : "off", display_ ? "on" : "off");
        while (!stopping_ && rclcpp::ok()) {
          GstMessage * message = gst_bus_timed_pop_filtered(bus, 100 * GST_MSECOND,
            static_cast<GstMessageType>(GST_MESSAGE_ERROR | GST_MESSAGE_EOS));
          if (message) {
            reason_ = GST_MESSAGE_TYPE(message) == GST_MESSAGE_EOS ? 2 : 3;
            // Backend debug strings may contain RTSP credentials; do not print them.
            if (GST_MESSAGE_TYPE(message) == GST_MESSAGE_ERROR) {
              GError * error = nullptr;
              gchar * debug = nullptr;
              gst_message_parse_error(message, &error, &debug);
              RCLCPP_ERROR(get_logger(), "GStreamer error from %s (code=%d); URI redacted",
                GST_OBJECT_NAME(message->src), error ? error->code : -1);
              if (error) {g_error_free(error);}
              g_free(debug);
            }
            gst_message_unref(message);
            break;
          }
          if (source_failed_) {reason_ = 3; break;}
          auto last = Steady::time_point(Steady::duration(last_frame_ticks_.load()));
          if (Steady::now() - last > std::chrono::milliseconds(stale_ms_)) {
            reason_ = 1;
            break;
          }
        }
      } catch (const std::exception & error) {
        reason_ = 3;
        RCLCPP_ERROR(get_logger(), "%s", error.what());
      }
      active_ = false;
      ++generation_;
      mailbox_.take();
      wake_.notify_one();
      if (bus) {gst_object_unref(bus);}
      cleanup();
      if (source_type_ == "file") {break;}
      std::unique_lock<std::mutex> lock(wake_mutex_);
      wake_.wait_for(lock, std::chrono::milliseconds(retry_ms_), [this] {return stopping_.load();});
    }
  }

  void publish(Inspection msg)
  {
    msg.overwritten_results = mailbox_.overwritten() + busy_drops_.load();
    result_pub_->publish(msg);
    std_msgs::msg::String status;
    status.data = msg.status;
    status_pub_->publish(status);
  }

  void publish_loop()
  {
    auto last_published = Steady::now() - 1s;
    while (!stopping_ && rclcpp::ok()) {
      auto snapshot = mailbox_.take();
      auto current = Steady::now();
      try {
        if (snapshot && active_ && snapshot->generation == generation_) {
          auto & msg = snapshot->message;
          msg.queue_delay_ms = std::chrono::duration<double, std::milli>(
            current - snapshot->created).count();
          if (msg.queue_delay_ms > max_age_ms_ ||
            (msg.pipeline_age_ms >= 0 &&
            msg.pipeline_age_ms + msg.queue_delay_ms > max_age_ms_))
          {
            msg.status = "UNKNOWN";
            msg.reason = "result_stale";
          }
          publish(std::move(msg));
          last_published = current;
        } else if (current - last_published >= 1s) {
          Inspection msg;
          msg.header.stamp = now();
          msg.header.frame_id = frame_id_;
          msg.status = "UNKNOWN";
          msg.pipeline_age_ms = -1;
          int reason = reason_;
          msg.reason = reason == 2 ? "end_of_stream" : reason == 3 ? "pipeline_error" :
            reason == 1 ? "stream_stale" : "waiting_for_frame";
          publish(std::move(msg));
          last_published = current;
        }
      } catch (const std::exception & error) {
        if (rclcpp::ok()) {RCLCPP_ERROR(get_logger(), "Publish failed: %s", error.what());}
      }
      // No middleware calls or message serialization run in the pad probe.
      std::unique_lock<std::mutex> lock(wake_mutex_);
      wake_.wait_for(lock, 10ms);
    }
  }

  std::string source_type_, uri_, infer_config_, frame_id_;
  bool display_, infer_;
  int hand_id_, width_, height_, latency_, stale_ms_, max_age_ms_, retry_ms_;
  double threshold_, roi_top_fraction_;
  std::atomic<bool> stopping_{false}, active_{false}, source_failed_{false};
  std::atomic<int> reason_{0};
  std::atomic<uint64_t> generation_{0}, sequence_{0}, busy_drops_{0};
  std::atomic<Steady::duration::rep> last_frame_ticks_{0};
  hand_detect::Latest<Snapshot> mailbox_;
  std::thread pipeline_thread_, publisher_thread_;
  std::mutex wake_mutex_, timing_mutex_;
  std::condition_variable wake_;
  std::map<GstClockTime, Steady::time_point> arrival_;
  GstElement * pipeline_{nullptr};
  GstElement * input_queue_{nullptr};
  GstPad * mux_pad_{nullptr};
  rclcpp::Publisher<Inspection>::SharedPtr result_pub_;
  rclcpp::Publisher<std_msgs::msg::String>::SharedPtr status_pub_;
};

int main(int argc, char ** argv)
{
  rclcpp::init(argc, argv);
  int result = 0;
  try {
    auto node = std::make_shared<HandDetect>();
    node->start();
    rclcpp::spin(node);
    node->stop();
  } catch (const std::exception & error) {
    RCLCPP_ERROR(rclcpp::get_logger("hand_detect"), "%s", error.what());
    result = 1;
  }
  rclcpp::shutdown();
  return result;
}
