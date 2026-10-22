# final_year

- [Open a Hikvision RTSP camera on Jetson Nano with ROS 2 Humble](main_workspace/src/hikvision_camera/README.md)
- [Hand detection: DeepStream + ROS 2 Humble](main_workspace/src/hand_detect/README.md)
- [Light curtain: GC-1000 driver and monitors](main_workspace/src/light_curtain/README.md)
- [Run camera/light curtain separately and measure CPU](SEPARATE_SYSTEMS.md)
- [C# 5 × 5 shelf debug monitor](main_workspace/tools/LightCurtainMonitor/README.md)

The ROS workspace is now `main_workspace`. Build on Jetson using ROS 2 Humble
and the standard `build/`, `install/`, and `log/` directories. After changing
the workspace path or ROS distribution, rebuild using fresh output directories.

```bash
source /opt/ros/humble/setup.bash
cd ~/kosen/main_workspace
CMAKE_BUILD_PARALLEL_LEVEL=2 colcon build --executor sequential \
  --cmake-args -DCMAKE_BUILD_TYPE=Release
source install/local_setup.bash
```
