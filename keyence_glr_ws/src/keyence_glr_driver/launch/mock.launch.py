from launch import LaunchDescription
from launch_ros.actions import Node


def generate_launch_description() -> LaunchDescription:
    return LaunchDescription([
        Node(
            package='keyence_glr_driver',
            executable='mock_publisher',
            name='keyence_glr_mock',
            parameters=[{'beam_count': 52, 'publish_hz': 20.0}],
            output='screen',
        ),
    ])
