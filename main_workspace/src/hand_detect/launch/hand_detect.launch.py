import os

from ament_index_python.packages import get_package_share_directory
from launch import LaunchDescription
from launch.actions import DeclareLaunchArgument
from launch.substitutions import LaunchConfiguration
from launch_ros.actions import Node
from launch_ros.parameter_descriptions import ParameterValue
from hand_detect_support import prepare_runtime


def generate_launch_description():
    runtime = prepare_runtime()
    config = os.path.join(
        get_package_share_directory("hand_detect"), "config", "hand_detect.yaml"
    )
    private_config = runtime / "config/camera.local.yaml"
    if private_config.is_file():
        config = str(private_config)
    return LaunchDescription([
        DeclareLaunchArgument("config", default_value=config),
        DeclareLaunchArgument("display", default_value="true"),
        DeclareLaunchArgument("inference_enabled", default_value="true"),
        DeclareLaunchArgument("source_type", default_value="rtsp"),
        DeclareLaunchArgument(
            "infer_config",
            default_value=str(runtime / "config/infer.local.txt"),
        ),
        Node(
            package="hand_detect",
            executable="hand_detect_node",
            name="hand_detect",
            output="screen",
            parameters=[
                LaunchConfiguration("config"),
                {
                    "display": ParameterValue(
                        LaunchConfiguration("display"), value_type=bool
                    ),
                    "inference_enabled": ParameterValue(
                        LaunchConfiguration("inference_enabled"), value_type=bool
                    ),
                    "source_type": LaunchConfiguration("source_type"),
                    "infer_config": LaunchConfiguration("infer_config"),
                },
            ],
        ),
    ])
