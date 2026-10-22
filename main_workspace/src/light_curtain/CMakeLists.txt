cmake_minimum_required(VERSION 3.8)
project(light_curtain)

find_package(ament_cmake REQUIRED)
find_package(ament_cmake_python REQUIRED)
find_package(rosidl_default_generators REQUIRED)
find_package(std_msgs REQUIRED)

rosidl_generate_interfaces(${PROJECT_NAME}
  "msg/BeamStatus.msg"
  "msg/CurtainStatus.msg"
  DEPENDENCIES std_msgs
)

# Keep the node module separate from the generated light_curtain.msg module.
ament_python_install_package(light_curtain_nodes)
install(PROGRAMS
  scripts/gc_modbus_driver
  scripts/mock_publisher
  scripts/web_server
  DESTINATION lib/${PROJECT_NAME}
)
install(DIRECTORY config launch web DESTINATION share/${PROJECT_NAME})
install(FILES README.md DESTINATION share/${PROJECT_NAME})

ament_export_dependencies(rosidl_default_runtime)
ament_package()
