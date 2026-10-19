cmake_minimum_required(VERSION 3.8)
project(hikvision_camera)

if(CMAKE_COMPILER_IS_GNUCXX OR CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  add_compile_options(-Wall -Wextra -Wpedantic)
endif()

find_package(ament_cmake REQUIRED)
find_package(rclcpp REQUIRED)
find_package(OpenCV REQUIRED)

add_executable(rtsp_display src/rtsp_display.cpp)
target_include_directories(rtsp_display PRIVATE ${OpenCV_INCLUDE_DIRS})
ament_target_dependencies(rtsp_display rclcpp)
target_link_libraries(rtsp_display ${OpenCV_LIBS})

install(TARGETS rtsp_display
  DESTINATION lib/${PROJECT_NAME})

install(DIRECTORY launch config
  DESTINATION share/${PROJECT_NAME})

if(BUILD_TESTING)
  find_package(ament_lint_auto REQUIRED)
  ament_lint_auto_find_test_dependencies()
endif()

ament_package()
