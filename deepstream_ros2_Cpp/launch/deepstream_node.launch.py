from launch import LaunchDescription
from launch_ros.actions import Node

def generate_launch_description():
    return LaunchDescription([
        Node(
            package='deepstream_ros2',
            executable='deepstream_node',
            output='screen'
        )
    ])

