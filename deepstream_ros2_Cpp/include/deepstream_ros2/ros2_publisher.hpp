#ifndef ROS2_PUBLISHER_HPP
#define ROS2_PUBLISHER_HPP

#include <rclcpp/rclcpp.hpp>
#include <std_msgs/msg/int32.hpp>   // <---- MISSING INCLUDE FIX

class FramePublisher : public rclcpp::Node
{
public:
    FramePublisher();

    void publish_frame(int frame_id);

private:
    rclcpp::Publisher<std_msgs::msg::Int32>::SharedPtr pub_;
};

#endif

