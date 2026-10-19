#include "ros2_publisher.hpp"
#include <std_msgs/msg/int32.hpp>

FramePublisher::FramePublisher() : Node("frame_pub_node") {
    pub_ = this->create_publisher<std_msgs::msg::Int32>("frame_id", 10);
}

void FramePublisher::publish_frame(int frame_id) {
    auto msg = std_msgs::msg::Int32();
    msg.data = frame_id;
    pub_->publish(msg);
}

