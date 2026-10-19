from launch import LaunchDescription
from launch_ros.actions import Node
from ament_index_python.packages import get_package_share_directory
import os


def generate_launch_description() -> LaunchDescription:
    cfg = os.path.join(
        get_package_share_directory('hikvision_camera'), 'config', 'camera.yaml'
    )
    return LaunchDescription([
        Node(
            package='hikvision_camera',
            executable='rtsp_display',
            name='hikvision_camera',
            parameters=[cfg],
            output='screen',
        ),
    ])
