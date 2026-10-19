"""Full demo: mock OR real driver + rosbridge + static web server."""
from launch import LaunchDescription
from launch.actions import DeclareLaunchArgument, IncludeLaunchDescription, GroupAction
from launch.conditions import IfCondition, UnlessCondition
from launch.launch_description_sources import PythonLaunchDescriptionSource
from launch.substitutions import LaunchConfiguration, PathJoinSubstitution
from launch_ros.actions import Node
from launch_ros.substitutions import FindPackageShare


def generate_launch_description() -> LaunchDescription:
    use_mock = LaunchConfiguration('use_mock')
    web_port = LaunchConfiguration('web_port')
    rosbridge_port = LaunchConfiguration('rosbridge_port')

    return LaunchDescription([
        DeclareLaunchArgument('use_mock', default_value='true',
                              description='true = run mock_publisher; false = run real GC-1000 Modbus driver'),
        DeclareLaunchArgument('web_port', default_value='8000'),
        DeclareLaunchArgument('rosbridge_port', default_value='9090'),

        GroupAction([
            IncludeLaunchDescription(
                PythonLaunchDescriptionSource([
                    PathJoinSubstitution([
                        FindPackageShare('keyence_glr_driver'),
                        'launch', 'mock.launch.py',
                    ])
                ]),
                condition=IfCondition(use_mock),
            ),
            IncludeLaunchDescription(
                PythonLaunchDescriptionSource([
                    PathJoinSubstitution([
                        FindPackageShare('keyence_glr_driver'),
                        'launch', 'driver.launch.py',
                    ])
                ]),
                condition=UnlessCondition(use_mock),
            ),
        ]),

        Node(
            package='rosbridge_server',
            executable='rosbridge_websocket',
            name='rosbridge_websocket',
            parameters=[{'port': rosbridge_port}],
            output='screen',
        ),

        Node(
            package='keyence_glr_bringup',
            executable='web_server',
            name='keyence_glr_web',
            parameters=[{'port': web_port}],
            output='screen',
        ),
    ])
