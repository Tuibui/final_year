// src/deepstream_node.cpp
#include "rclcpp/rclcpp.hpp"
#include "ros2_publisher.hpp"
#include "deepstream_pipeline.h"
#include <thread> // Required for threading

std::shared_ptr<FramePublisher> global_pub;

// C callback bridge
extern "C" void ros_frame_callback(int frame_id) {
    if (global_pub) {
        global_pub->publish_frame(frame_id);
    }
}

int main(int argc, char **argv) {
    rclcpp::init(argc, argv);

    global_pub = std::make_shared<FramePublisher>();

    RCLCPP_INFO(global_pub->get_logger(), "Starting DeepStream Pipeline in background thread...");

    // 1. Run DeepStream in a separate thread
    // We pass the function and its argument
    std::thread ds_thread(start_deepstream_pipeline, ros_frame_callback);

    // 2. Let ROS 2 take over the main thread
    // This will block here until you press Ctrl+C
    rclcpp::spin(global_pub);

    // 3. Shutdown sequence (When you press Ctrl+C)
    RCLCPP_INFO(global_pub->get_logger(), "Stopping DeepStream...");
    
    stop_deepstream_pipeline(); // Tell C code to quit the loop

    if (ds_thread.joinable()) {
        ds_thread.join(); // Wait for C code to finish cleanup
    }

    RCLCPP_INFO(global_pub->get_logger(), "DeepStream Stopped. Exiting.");
    rclcpp::shutdown();
    return 0;
}
