// Standalone display pipeline for a Hikvision IP camera (DS-2CD2046G2-I(U)).
//
// Pulls the camera's RTSP H.264/H.265 stream with OpenCV/FFmpeg and shows it in
// a window. No other package in this workspace is required — this node only
// needs rclcpp + OpenCV, so it can be launched on its own to confirm the camera
// works.
//
// RTSP URL (Hikvision G2 series):
//     rtsp://<user>:<pass>@<ip>:<port>/Streaming/Channels/<channel>
//         channel 101 = main stream (full 4 MP)
//         channel 102 = sub  stream (lower res)
//
// Fill the camera IP / credentials in config/camera.yaml (left blank on
// purpose), or pass rtsp_url directly. Press 'q' (or ESC) in the window to quit.

#include <chrono>
#include <cstdlib>
#include <iomanip>
#include <sstream>
#include <string>

#include <opencv2/opencv.hpp>
#include <rclcpp/rclcpp.hpp>

using namespace std::chrono_literals;

namespace
{
// Percent-encode credential characters that would break the RTSP URL
// (e.g. '@', ':', '/'). Mirrors Python's urllib.parse.quote(safe="").
std::string url_encode(const std::string & value)
{
  std::ostringstream out;
  out << std::hex << std::uppercase << std::setfill('0');
  for (unsigned char c : value) {
    if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
      out << c;
    } else {
      out << '%' << std::setw(2) << static_cast<int>(c);
    }
  }
  return out.str();
}
}  // namespace

class RtspDisplay : public rclcpp::Node
{
public:
  RtspDisplay()
  : rclcpp::Node("hikvision_camera")
  {
    // --- connection params (see config/camera.yaml) ----------------------
    declare_parameter<std::string>("ip_address", "");   // TODO: fill in camera.yaml
    declare_parameter<std::string>("username", "");     // TODO: fill in camera.yaml
    declare_parameter<std::string>("password", "");     // TODO: fill in camera.yaml
    declare_parameter<int>("rtsp_port", 554);
    declare_parameter<int>("channel", 101);             // 101 = main, 102 = sub
    declare_parameter<std::string>("rtsp_url", "");     // overrides the fields above
    // --- display params --------------------------------------------------
    declare_parameter<bool>("use_tcp", true);
    declare_parameter<double>("reconnect_delay_s", 2.0);
    declare_parameter<std::string>("window_name", "Hikvision DS-2CD2046G2");
    declare_parameter<bool>("show_fps", true);
    // Log the frame number every Nth frame: N=4 logs frames 1, 5, 9, 13, ...
    // (purely informational; every frame is still displayed).
    declare_parameter<int>("log_every_n", 4);

    use_tcp_ = get_parameter("use_tcp").as_bool();
    reconnect_delay_s_ = get_parameter("reconnect_delay_s").as_double();
    window_name_ = get_parameter("window_name").as_string();
    show_fps_ = get_parameter("show_fps").as_bool();
    log_every_n_ = std::max<int64_t>(1, get_parameter("log_every_n").as_int());

    // Create the window as resizable (WINDOW_AUTOSIZE, the imshow default,
    // locks it to the frame size). KEEPRATIO preserves the aspect ratio.
    cv::namedWindow(window_name_, cv::WINDOW_NORMAL | cv::WINDOW_KEEPRATIO);

    url_ = build_url();

    // RTSP over TCP is set via FFmpeg capture options before opening the stream.
    if (use_tcp_) {
      setenv("OPENCV_FFMPEG_CAPTURE_OPTIONS", "rtsp_transport;tcp", 0);
    }

    last_t_ = std::chrono::steady_clock::now();

    // Drive the grab/show loop off a timer so rclcpp stays responsive.
    // ~33 ms ≈ 30 Hz; the actual rate is capped by the camera stream.
    timer_ = create_wall_timer(33ms, std::bind(&RtspDisplay::tick, this));
    RCLCPP_INFO(get_logger(), "Opening RTSP stream: %s", safe_url().c_str());
  }

  ~RtspDisplay() override
  {
    if (cap_.isOpened()) {
      cap_.release();
    }
    cv::destroyAllWindows();
  }

private:
  // Use rtsp_url if given, otherwise assemble it from the parts.
  std::string build_url()
  {
    const std::string explicit_url = get_parameter("rtsp_url").as_string();
    if (!explicit_url.empty()) {
      return explicit_url;
    }

    const std::string ip = get_parameter("ip_address").as_string();
    const std::string user = get_parameter("username").as_string();
    const std::string pwd = get_parameter("password").as_string();
    const int port = get_parameter("rtsp_port").as_int();
    const int channel = get_parameter("channel").as_int();

    if (ip.empty()) {
      RCLCPP_ERROR(
        get_logger(),
        "No camera IP set. Fill ip_address (+ username/password) in "
        "config/camera.yaml, or pass rtsp_url.");
    }

    std::string cred;
    if (!user.empty()) {
      cred = url_encode(user) + ":" + url_encode(pwd) + "@";
    }
    std::ostringstream url;
    url << "rtsp://" << cred << ip << ":" << port
        << "/Streaming/Channels/" << channel;
    return url.str();
  }

  // URL with the password masked, for logging.
  std::string safe_url() const
  {
    const auto at = url_.find('@');
    if (at == std::string::npos) {
      return url_;
    }
    const std::string creds = url_.substr(0, at);
    const std::string host = url_.substr(at + 1);
    const auto colon = creds.rfind(':');
    const std::string scheme_user =
      (colon == std::string::npos) ? creds : creds.substr(0, colon);
    return scheme_user + ":****@" + host;
  }

  bool open_stream()
  {
    cap_.open(url_, cv::CAP_FFMPEG);
    if (cap_.isOpened()) {
      // Keep the buffer small so we show the latest frame, not a backlog.
      cap_.set(cv::CAP_PROP_BUFFERSIZE, 1);
      RCLCPP_INFO(get_logger(), "Stream opened.");
      return true;
    }
    RCLCPP_WARN(
      get_logger(), "Could not open stream, retrying in %.0fs...",
      reconnect_delay_s_);
    cap_.release();
    return false;
  }

  void tick()
  {
    if (!cap_.isOpened()) {
      if (!open_stream()) {
        rclcpp::sleep_for(std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::duration<double>(reconnect_delay_s_)));
        return;
      }
    }

    cv::Mat frame;
    if (!cap_.read(frame) || frame.empty()) {
      RCLCPP_WARN(get_logger(), "Frame read failed — reconnecting.");
      cap_.release();
      return;
    }

    // Log the frame number on frames 1, 1+N, 1+2N, ... (N == log_every_n_).
    if (++frame_count_ % log_every_n_ == 1 % log_every_n_) {
      RCLCPP_INFO(get_logger(), "frame %lu", frame_count_);
    }

    if (show_fps_) {
      const auto now = std::chrono::steady_clock::now();
      const double dt = std::chrono::duration<double>(now - last_t_).count();
      last_t_ = now;
      if (dt > 0.0) {
        // simple exponential smoothing
        fps_ = 0.9 * fps_ + 0.1 * (1.0 / dt);
      }
      std::ostringstream label;
      label << std::fixed << std::setprecision(1) << fps_ << " FPS";
      cv::putText(
        frame, label.str(), cv::Point(12, 28), cv::FONT_HERSHEY_SIMPLEX,
        0.8, cv::Scalar(0, 255, 0), 2, cv::LINE_AA);
    }

    cv::imshow(window_name_, frame);
    // 'q' or ESC closes the window and shuts the node down.
    const int key = cv::waitKey(1) & 0xFF;
    if (key == 'q' || key == 27) {
      RCLCPP_INFO(get_logger(), "Quit requested.");
      rclcpp::shutdown();
    }
  }

  cv::VideoCapture cap_;
  std::string url_;
  std::string window_name_;
  bool use_tcp_{true};
  bool show_fps_{true};
  int log_every_n_{4};
  unsigned long frame_count_{0};
  double reconnect_delay_s_{2.0};
  double fps_{0.0};
  std::chrono::steady_clock::time_point last_t_;
  rclcpp::TimerBase::SharedPtr timer_;
};

int main(int argc, char ** argv)
{
  rclcpp::init(argc, argv);
  rclcpp::spin(std::make_shared<RtspDisplay>());
  rclcpp::shutdown();
  return 0;
}
