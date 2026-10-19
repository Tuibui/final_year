# Hikvision RTSP camera: Jetson Nano / ROS 2 Humble

This guide targets **ROS 2 Humble**. The development computer currently has Jazzy installed; that does not establish the Jetson's environment. Build this package again inside the Jetson's Humble environment. Do not copy the development computer's `build/`, `install/`, or `log/` directories.

The existing `hikvision_camera` package opens an RTSP stream using C++ OpenCV's **FFmpeg backend** and displays a window. It only needs ROS `rclcpp` and OpenCV at runtime; the Keyence drivers and DeepStream are not required. It does **not** publish `sensor_msgs/Image` or provide a headless mode. Instructions below were checked against the source, but have not been tested on the physical camera or Jetson.

## 1. Check the target environment first

Run on the Jetson, and again inside a container if that is where Humble runs:

```bash
cat /etc/os-release
uname -m
printenv ROS_DISTRO
ls /opt/ros
python3 --version
```

On the Jetson host, also check:

```bash
cat /proc/device-tree/model
cat /etc/nv_tegra_release
dpkg-query -W nvidia-l4t-core
```

Missing NVIDIA files/packages inside a container do not establish the host's JetPack version.

| Environment | What to do |
| --- | --- |
| Ubuntu 22.04 ARM64 with Humble already installed | Use the Humble build and run steps below. |
| Original Jetson Nano with stock JetPack 4 / Ubuntu 18.04 | Do not install Jammy Humble debs into the host. Use the project's existing working Humble environment, if present; otherwise a compatible container/custom OS setup is a separate prerequisite. |
| Humble inside a container | Build and run inside it. Verify camera network access, GUI forwarding, and the OpenCV backend there. Host NVIDIA plugins are not automatically available inside it. |
| Jetson **Orin Nano** | This is different hardware. Check its actual JetPack/Ubuntu version rather than applying original Nano assumptions. |

[Humble supports Ubuntu 22.04 on ARM64](https://repo.test.ros2.org/en/humble/Releases/Release-Humble-Hawksbill.html). NVIDIA's [JetPack 4.6.1 release](https://developer.nvidia.com/embedded/jetpack-sdk-461) supports the original Nano and uses Ubuntu 18.04. A custom Ubuntu image or newer container does not by itself guarantee compatible NVIDIA hardware decoding. Do not upgrade/reflash the device just to follow this guide.

## 2. Connect and identify the camera

1. Power the camera using its supported supply or a PoE injector/switch. A normal Jetson Ethernet port does not supply PoE.
2. Connect camera and Jetson to the same reachable LAN. Activate the camera and configure its login using Hikvision's supported setup tools if necessary.
3. Find the camera IP through the router, camera settings, or the repository's discovery script. From the repository root, with **Python 3.10 or newer**:

   ```bash
   python3 keyence_glr_ws/src/sadp_discover.py --timeout 5
   ```

   The script uses `dict | None` annotations and will not run unchanged on stock Ubuntu 18.04's Python 3.6. Run it from a suitable laptop/Humble environment on the camera LAN instead. Discovery uses multicast UDP 37020; container networking or multiple NICs may prevent replies. A failed discovery does not prove RTSP is unavailable. The script's reported `Port` is not necessarily the RTSP port.

4. Inspect networking without changing the existing Keyence connection:

   ```bash
   ip -br address
   ip route
   ping -c 3 CAMERA_IP
   nc -vz -w 3 CAMERA_IP 554
   ```

   Replace `CAMERA_IP` with the actual address; `nc` comes from `netcat-openbsd`. The Jetson needs a route to the camera subnet and a distinct IP address. Ping can be blocked even when RTSP works. Do not assume the interface is `eth0` or run `setup_gc1000_net.sh` to configure the camera: that script targets the Keyence network.

## 3. Verify RTSP before using ROS

[Hikvision documents](https://supportusa.hikvision.com/support/solutions/articles/17000129064-how-do-i-get-my-rtsp-stream-) this URL format:

```text
rtsp://USERNAME:PASSWORD@CAMERA_IP:554/Streaming/Channels/102
```

- `101`: camera/channel 1 main stream.
- `102`: camera/channel 1 substream; start here to reduce Nano CPU/network load.
- `201` / `202`: channel 2 main/substream on a recorder.

Check the configured RTSP port; 554 is the usual default. Start with standard **H.264**, a low-resolution substream, and a modest frame rate in the camera settings. Disable H.264+/H.265+ for initial troubleshooting. The H.264 GStreamer example below will not decode an H.265 stream.

For a standalone test, install `ffmpeg` in the environment doing the test, then enter the URL interactively in Bash (percent-encode special characters in its username/password):

```bash
read -r -s -p 'RTSP URL: ' CAMERA_RTSP_URL
printf '\n'
ffprobe -v error -rtsp_transport tcp -show_streams "$CAMERA_RTSP_URL"
```

Expect a video stream with codec, width, and height. On a graphical desktop:

```bash
ffplay -rtsp_transport tcp -fflags nobuffer -an "$CAMERA_RTSP_URL"
```

For a headless decode test (30 frames, no window):

```bash
ffmpeg -rtsp_transport tcp -i "$CAMERA_RTSP_URL" -map 0:v:0 -frames:v 30 -an -f null -
unset CAMERA_RTSP_URL
```

Interactive entry avoids saving the literal URL in shell history, but tools may expose it in process arguments or diagnostic output. Redact credentials before sharing logs.

## 4. Build only the camera package with Humble

Use a fresh terminal that does not auto-source Jazzy or an old workspace overlay. In your existing **Ubuntu 22.04 / Humble environment**, with the ROS apt repository already configured:

```bash
source /opt/ros/humble/setup.bash
printenv ROS_DISTRO
sudo apt update
sudo apt install build-essential cmake pkg-config python3-colcon-common-extensions \
  libopencv-dev ffmpeg ros-humble-ament-cmake ros-humble-rclcpp
```

`ROS_DISTRO` must print `humble`. If Humble is installed from source, use that installation's setup file instead. If `/opt/ros/humble` is absent, first resolve the environment in section 1; do not substitute Jazzy. For a new supported Jammy installation, follow the [official Humble installation instructions](https://docs.ros.org/en/humble/Installation/Ubuntu-Install-Debs.html).

From the repository root:

```bash
cd keyence_glr_ws
CMAKE_BUILD_PARALLEL_LEVEL=1 colcon --log-base log_humble build \
  --build-base build_humble --install-base install_humble \
  --packages-select hikvision_camera --executor sequential \
  --cmake-args -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=OFF
source install_humble/local_setup.bash
ros2 pkg executables hikvision_camera
```

Expect `hikvision_camera rtsp_display`. Separate output directories prevent reuse of Jazzy build caches; single-job compilation limits Nano memory pressure. `BUILD_TESTING=OFF` skips lint dependencies for this camera bring-up, not runtime requirements. Keep generated build directories out of your commit.

## 5. Configure and open the camera

Keep deployment credentials outside the repository. Create a private config from the template (do not overwrite an existing private config):

```bash
mkdir -p "$HOME/.config/hikvision_camera"
cp -n src/hikvision_camera/config/camera.yaml "$HOME/.config/hikvision_camera/camera.yaml"
chmod 600 "$HOME/.config/hikvision_camera/camera.yaml"
nano "$HOME/.config/hikvision_camera/camera.yaml"
```

The checked-in template currently contains a password value. Treat it as untrusted deployment data, replace it in your local copy, and do not copy it into documentation. If it is a real credential, rotate it and remove it from the tracked configuration before publishing.

Set your private file to this structure, replacing placeholders with actual values:

```yaml
hikvision_camera:
  ros__parameters:
    ip_address: "CAMERA_IP"
    username: "CAMERA_USER"
    password: "CAMERA_PASSWORD"
    rtsp_port: 554
    channel: 102
    rtsp_url: ""
    use_tcp: true
    reconnect_delay_s: 2.0
    window_name: "Hikvision camera"
    show_fps: true
    log_every_n: 60
```

The node percent-encodes credentials when it builds the URL from separate fields. Enter the password literally in those fields, using valid YAML quoting. A nonempty `rtsp_url` overrides IP, username, password, port, and channel; encode credentials yourself in that case.

Run on the Jetson's graphical desktop, from the sourced Humble workspace:

```bash
export OPENCV_FFMPEG_CAPTURE_OPTIONS='rtsp_transport;tcp'
ros2 run hikvision_camera rtsp_display --ros-args \
  --params-file "$HOME/.config/hikvision_camera/camera.yaml"
```

Expect `Stream opened.`, frame counter messages, and a resizable video window. Press **q** or **Esc** in the window to quit. Explicitly setting the environment variable avoids inheriting conflicting FFmpeg options: the node's `setenv()` does not overwrite an existing value.

For each new terminal, source `/opt/ros/humble/setup.bash` and `keyence_glr_ws/install_humble/local_setup.bash` again.

The repository also has `ros2 launch hikvision_camera display.launch.py`, but that launcher always reads the **installed** `config/camera.yaml` and exposes no custom config launch argument. Use `ros2 run --params-file` as above for the private file. If you intentionally use the launcher, install `ros-humble-launch-ros` and `ros-humble-ament-index-python`, configure the source YAML, and rebuild to copy it into the install directory. Avoid committing credentials.

## 6. Jetson decoding and display limitations

The current node explicitly calls `cap_.open(url_, cv::CAP_FFMPEG)`. It does not select NVIDIA's GStreamer decoder, and installing CUDA/DeepStream does not automatically accelerate this path. Do not pass a GStreamer pipeline as `rtsp_url`: it would require a code change to select `cv::CAP_GSTREAMER`.

For an optional **separate** H.264 hardware decode test on a compatible JetPack host, first check the plugins:

```bash
gst-inspect-1.0 rtspsrc
gst-inspect-1.0 nvv4l2decoder
gst-inspect-1.0 nvvidconv
gst-inspect-1.0 autovideosink
```

If available, on a graphical desktop:

```bash
read -r -s -p 'H.264 RTSP URL: ' CAMERA_RTSP_URL
printf '\n'
gst-launch-1.0 -e rtspsrc location="$CAMERA_RTSP_URL" protocols=tcp latency=200 \
  ! rtph264depay ! h264parse ! nvv4l2decoder ! nvvidconv \
  ! 'video/x-raw,format=I420' ! videoconvert ! autovideosink sync=false
unset CAMERA_RTSP_URL
```

For a headless pipeline test, replace `autovideosink sync=false` with `fakesink sync=false`; stop with Ctrl+C. This is a diagnostic example assembled from [NVIDIA's accelerated GStreamer documentation](https://docs.nvidia.com/jetson/l4t/Tegra%20Linux%20Driver%20Package%20Development%20Guide/accelerated_gstreamer.html), not a tested integration with this ROS node. Missing NVIDIA plugins require matching JetPack/L4T components; generic Ubuntu GStreamer packages alone do not provide them.

## Troubleshooting

| Symptom | Check / next step |
| --- | --- |
| Timeout / no route / connection refused | Power, actual camera IP, subnet/VLAN, routing, configured RTSP port, firewall. Test RTSP outside ROS first. |
| `401 Unauthorized` | Camera activation, username/password, account permissions, and URL encoding. |
| Invalid stream / `404` | Correct camera or NVR channel and enabled substream; try `101` if `102` is disabled. |
| `Could not open stream` but ffplay works | The C++ OpenCV library must include FFmpeg support. Check `opencv_version --verbose` if available; inspect the OpenCV path selected in `build_humble/hikvision_camera/CMakeCache.txt`. Installing Python `opencv-python` does not fix this C++ dependency. |
| Qt/GTK/display error before any RTSP log | `cv::namedWindow()` runs before connection. The node requires a working GUI session. Plain SSH/headless containers need the standalone headless test instead; setting `DISPLAY` alone does not grant display access. |
| Slow playback / high CPU / growing delay | Use H.264 substream `102`, reduce camera resolution/FPS/bitrate, and test standalone decode. The node's buffer-size request may be ignored by FFmpeg; it does not guarantee low latency. |
| Long hangs on disconnect | Capture/open/read calls are synchronous and no explicit I/O timeout is configured. Reconnection can wait for the backend timeout. |
| Package not found / library errors | Source Humble and the correct `install_humble/local_setup.bash`; rebuild on ARM64 without Jazzy overlays or copied x86 binaries. |
| No ROS image topic | Expected: this executable only displays video. Image publishing would need an additional implementation. |

## Target verification checklist

- Record device model, Ubuntu, JetPack/L4T, ROS distro, and whether each command ran on the host or in a container.
- Verify RTSP connection and decode independently of ROS.
- Build the package in Humble on the target and confirm the viewer opens the substream.
- Check frame rate/CPU usage, quit behavior, and reconnection after a brief camera disconnect.
- Review `git diff` before pushing; include documentation, not private camera config or build artifacts.
