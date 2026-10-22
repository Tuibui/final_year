"""Start the real light-curtain driver and ROSbridge for the C# monitor."""

from launch import LaunchDescription
from launch.actions import DeclareLaunchArgument, IncludeLaunchDescription
from launch.conditions import IfCondition
from launch.launch_description_sources import PythonLaunchDescriptionSource
from launch.substitutions import LaunchConfiguration, PathJoinSubstitution
from launch_ros.actions import Node
from launch_ros.parameter_descriptions import ParameterValue
from launch_ros.substitutions import FindPackageShare
from light_curtain_nodes.runtime import prepare_runtime


def generate_launch_description():
    driver_share = FindPackageShare('light_curtain')
    default_config = prepare_runtime()
    return LaunchDescription([
        DeclareLaunchArgument('start_driver', default_value='true',
                              description='Set false when the Modbus driver is already running'),
        DeclareLaunchArgument('config', default_value=default_config),
        DeclareLaunchArgument('rosbridge_port', default_value='9090'),
        IncludeLaunchDescription(
            PythonLaunchDescriptionSource(PathJoinSubstitution([
                driver_share, 'launch', 'driver.launch.py'])),
            launch_arguments={'config': LaunchConfiguration('config')}.items(),
            condition=IfCondition(LaunchConfiguration('start_driver')),
        ),
        Node(
            package='rosbridge_server', executable='rosbridge_websocket',
            name='curtain_desktop_rosbridge',
            parameters=[{'port': ParameterValue(
                LaunchConfiguration('rosbridge_port'), value_type=int)}],
            output='screen',
        ),
    ])
