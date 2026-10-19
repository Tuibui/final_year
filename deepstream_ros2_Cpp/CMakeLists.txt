cmake_minimum_required(VERSION 3.8)
project(deepstream_ros2)

# ===========================
# ROS 2 Dependencies
# ===========================
find_package(ament_cmake REQUIRED)
find_package(rclcpp REQUIRED)
find_package(std_msgs REQUIRED)

# ===========================
# GStreamer Dependencies
# ===========================
find_package(PkgConfig REQUIRED)
pkg_check_modules(GST REQUIRED gstreamer-1.0)
pkg_check_modules(GSTBASE REQUIRED gstreamer-base-1.0)
pkg_check_modules(GSTVIDEO REQUIRED gstreamer-video-1.0)

include_directories(
  include
  include/deepstream_ros2
  ${GST_INCLUDE_DIRS}
  ${GSTBASE_INCLUDE_DIRS}
  ${GSTVIDEO_INCLUDE_DIRS}
  /opt/nvidia/deepstream/deepstream/sources/includes
)

link_directories(
  /opt/nvidia/deepstream/deepstream/lib
)

# ===========================
# DeepStream C Pipeline
# ===========================
add_library(ds_pipeline SHARED
  src/deepstream_pipeline.c
)

target_link_libraries(ds_pipeline
  ${GST_LIBRARIES}
  ${GSTBASE_LIBRARIES}
  ${GSTVIDEO_LIBRARIES}
  nvbufsurface
  nvbufsurftransform
  nvdsgst_meta
  nvdsgst_helper
  nvds_meta
)

# ===========================
# ROS 2 Node (C++)
# ===========================
add_executable(deepstream_node
  src/deepstream_node.cpp
  src/ros2_publisher.cpp
  
)
add_executable(getframe_node
  src/ros2_subscriber.cpp
  
)


ament_target_dependencies(deepstream_node
  rclcpp
  std_msgs
)

ament_target_dependencies(getframe_node
  rclcpp
  std_msgs
)

target_link_libraries(deepstream_node
  ds_pipeline
)

# ===========================
# Install Targets
# ===========================
install(
  TARGETS deepstream_node ds_pipeline
  DESTINATION lib/${PROJECT_NAME}
)

# Install launch files
install(
  DIRECTORY launch
  DESTINATION share/${PROJECT_NAME}
)
install(
  TARGETS getframe_node  
  DESTINATION lib/${PROJECT_NAME}
)

install(
  DIRECTORY resource
  DESTINATION share/${PROJECT_NAME}
)

# Install package.xml
install(
  FILES package.xml
  DESTINATION share/${PROJECT_NAME}
)

# ===========================
# Export Package
# ===========================
ament_package()

