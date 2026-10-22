from launch import LaunchDescription
from launch.actions import DeclareLaunchArgument
from launch.substitutions import LaunchConfiguration
from launch_ros.actions import Node
from light_curtain_nodes.runtime import prepare_runtime


def generate_launch_description() -> LaunchDescription:
    cfg = prepare_runtime()
    return LaunchDescription([
        DeclareLaunchArgument('config', default_value=cfg,
                              description='Path to the light-curtain-only parameter file'),
        Node(
            package='light_curtain',
            executable='gc_modbus_driver',
            name='keyence_glr_driver',
            parameters=[LaunchConfiguration('config')],
            output='screen',
        ),
    ])
