"""Read-only light-curtain dashboard, with an optional real Modbus driver."""

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
    return LaunchDescription([
        DeclareLaunchArgument('start_driver', default_value='false',
                              description='Start the real driver only if it is not already running'),
        DeclareLaunchArgument('config', default_value=prepare_runtime()),
        DeclareLaunchArgument('web_port', default_value='8000'),
        DeclareLaunchArgument('rosbridge_port', default_value='9090'),
        IncludeLaunchDescription(
            PythonLaunchDescriptionSource(PathJoinSubstitution([
                driver_share, 'launch', 'driver.launch.py'])),
            launch_arguments={'config': LaunchConfiguration('config')}.items(),
            condition=IfCondition(LaunchConfiguration('start_driver')),
        ),
        Node(
            package='rosbridge_server', executable='rosbridge_websocket',
            name='curtain_rosbridge',
            parameters=[{'port': ParameterValue(
                LaunchConfiguration('rosbridge_port'), value_type=int)}],
            output='screen',
        ),
        Node(
            package='light_curtain', executable='web_server',
            name='keyence_glr_web',
            parameters=[{'port': ParameterValue(
                LaunchConfiguration('web_port'), value_type=int)}],
            output='screen',
        ),
    ])
