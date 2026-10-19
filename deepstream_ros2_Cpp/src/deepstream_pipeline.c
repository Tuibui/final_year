// src/deepstream_pipeline.c
#include "deepstream_pipeline.h"
#include <gst/gst.h>
#include <gst/video/video.h>

// Global variable to hold the loop
static GMainLoop *main_loop = NULL; 
static int frame_counter = 0;
static FrameCallback ros_callback = NULL;

static GstPadProbeReturn probe_cb(GstPad *pad, GstPadProbeInfo *info, gpointer user_data) {
    frame_counter++;
    if (ros_callback) {
        ros_callback(frame_counter);
    }
    return GST_PAD_PROBE_OK;
}

void start_deepstream_pipeline(FrameCallback cb) {
    ros_callback = cb;
    gst_init(NULL, NULL);

    GstElement *pipeline = gst_pipeline_new("simple-pipe");
    GstElement *source = gst_element_factory_make("v4l2src", "camera");
    GstElement *convert = gst_element_factory_make("videoconvert", "convert");
    GstElement *sink = gst_element_factory_make("autovideosink", "display");

    if (!pipeline || !source || !convert || !sink) {
        g_printerr("Failed to create elements\n");
        return;
    }

    g_object_set(G_OBJECT(source), "device", "/dev/video0", NULL);

    gst_bin_add_many(GST_BIN(pipeline), source, convert, sink, NULL);
    gst_element_link_many(source, convert, sink, NULL);

    GstPad *pad = gst_element_get_static_pad(convert, "sink");
    gst_pad_add_probe(pad, GST_PAD_PROBE_TYPE_BUFFER, probe_cb, NULL, NULL);
    gst_object_unref(pad);

    gst_element_set_state(pipeline, GST_STATE_PLAYING);

    // Create and run the loop using the global variable
    main_loop = g_main_loop_new(NULL, FALSE);
    g_main_loop_run(main_loop);

    // Cleanup
    gst_element_set_state(pipeline, GST_STATE_NULL);
    gst_object_unref(pipeline);
    g_main_loop_unref(main_loop);
    main_loop = NULL;
}

// --- ADD THIS FUNCTION ---
void stop_deepstream_pipeline() {
    if (main_loop) {
        g_main_loop_quit(main_loop);
    }
}
