// src/ros2_subscriber.cpp
#include <memory>
#include "rclcpp/rclcpp.hpp"
#include "std_msgs/msg/int32.hpp"

using std::placeholders::_1;

class FrameSubscriber : public rclcpp::Node
{
public:
    FrameSubscriber() : Node("frame_sub_node")
    {
        // Subscribe to topic "frame_id" to match your publisher
        subscription_ = this->create_subscription<std_msgs::msg::Int32>(
            "frame_id", 
            10, 
            std::bind(&FrameSubscriber::topic_callback, this, _1));
            
        RCLCPP_INFO(this->get_logger(), "Subscriber started. Waiting for frames...");
    }

private:
    void topic_callback(const std_msgs::msg::Int32::SharedPtr msg) const
    {
        // Print the received frame number
        RCLCPP_INFO(this->get_logger(), "I received frame: '%d'", msg->data);
    }

    rclcpp::Subscription<std_msgs::msg::Int32>::SharedPtr subscription_;
};

int main(int argc, char * argv[])
{
    rclcpp::init(argc, argv);
    rclcpp::spin(std::make_shared<FrameSubscriber>());
    rclcpp::shutdown();
    return 0;
}
