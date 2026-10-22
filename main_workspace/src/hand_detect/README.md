# hand_detect — ROS 2 Humble and DeepStream

**Count hands in the top 50% of each processed frame: `hand = 0`, `hand = 1`, `hand = 2`, etc.**
Disabled inference, skipped inference, stale results, stream loss, and pipeline errors
produce **UNKNOWN**, rather than claiming a zero-hand frame.
Missing or invalid inference configuration fails startup with an error; no
result topic is published until the configuration is corrected.

The inspected target is a **Jetson Orin NX** running Ubuntu 22.04, L4T R36.4.7,
ROS 2 Humble, DeepStream 7.1, and TensorRT 10.3. The development machine uses
Jazzy; do not build or copy binaries from that machine onto the Jetson.
This setup does not establish compatibility with an original Jetson Nano.

## Current model on the configured Jetson

The selected detector is now **TAO EfficientDet-D0 run4 epoch 80**, stored in
`~/kosen/runtime/hand_detect/tao/hand_efficientdet_d0_run4_e080_20261022`.
The active `runtime/hand_detect/config/infer.local.txt` selects its dedicated
parser and locally built TensorRT engine. The private camera configuration
uses `hand_class_id: 0`, confidence 0.30, and a 640x480 canvas. Camera credentials
are retained; the detection ROI is the top 50% of the frame.

Start it using the existing launch command; no ROS node rebuild is needed
to change the runtime detector:

```bash
source /opt/ros/humble/setup.bash
source ~/kosen/main_workspace/install/local_setup.bash
ros2 launch hand_detect hand_detect.launch.py
```

Use a Jetson desktop terminal for display, or append `display:=false` over SSH.
The epoch-35 deployment is removed after switching; legacy YOLOX files remain separate. See [MODEL.md](MODEL.md)
for the tensor contract and model provenance. The old `setup_model.py` command
below is only for selecting the legacy public YOLOX detector.

## Project directory layout

All hand_detect deployment data stays inside the repository. On Jetson:

```text
~/kosen/
├── main_workspace/src/hand_detect/    # Source code, tracked configs, and docs
├── main_workspace/install/    # Installed executable and custom parser
└── runtime/hand_detect/              # Machine-specific files, ignored by Git
    ├── models/                      # ONNX, TensorRT engine, labels, checksum
    ├── tao/                         # User-trained TAO deployment bundle and parser
    ├── config/                      # infer.local.txt and camera.local.yaml
    ├── cache/                       # Downloaded model bundle and timing cache
    ├── logs/                        # Validation logs and ROS logs
    └── tmp/                         # Temporary project files
```

Sourcing the workspace sets `HAND_DETECT_DATA_DIR`, `ROS_LOG_DIR`, and `TMPDIR`
to this layout. The setup and launch code also derives the repository location
from the installed package, so the repository need not be named `kosen` on
another machine. Rebuild after moving the repository to regenerate install paths.
System ROS, CUDA, and DeepStream remain installed at their system locations.

## Architecture

```text
One process / one ROS node: hand_detect
  ROS main thread: executor and ROS callbacks
  Pipeline thread: starts/stops GStreamer, polls errors, reconnects RTSP
    GStreamer streaming threads:
      RTSP -> NVIDIA decode -> leaky queue (1) -> nvstreammux -> nvinfer
                                                               |
                                                            pad probe
                                                               | copy small result
                                       latest-result mailbox (1)
                                                               |
  Publisher worker: takes newest result -> ~/result and ~/status

  Video continues after probe -> display queue (1) -> convert -> OSD -> display
  Headless mode uses fakesink and needs no desktop/X11.
```

A pad probe runs synchronously in the streaming thread. It copies metadata,
counts hands above the confidence threshold, and returns. It never calls
ROS publish, sleeps, waits for PLCs, or holds pointers to metadata for later use.
The mailbox uses a try-lock producer; busy/old results are dropped. No `tee`
is needed to display video and publish metadata. This is a latest-state
inspection pipeline, not an every-event recorder.

## Build on Jetson

The workspace was renamed from `keyence_glr_ws` to `main_workspace`. Existing
generated directories may contain absolute old paths. Use the standard
`build/`, `install/`, and `log/` directories with a fresh Humble build, and
update any shell startup script that sources the old overlay.
The old `deepstream_ros2_Cpp` experiment is retained outside this workspace.

In a fresh Jetson terminal:

```bash
source /opt/ros/humble/setup.bash
cd ~/kosen/main_workspace
CMAKE_BUILD_PARALLEL_LEVEL=2 colcon build \
  --packages-select hand_detect --executor sequential \
  --cmake-args -DCMAKE_BUILD_TYPE=Release
source install/local_setup.bash
```

Required: CUDA Toolkit and DeepStream development headers/libraries, GStreamer development
packages, ament_cmake_python, rclcpp, std_msgs, rosidl_default_generators, and
ament_cmake_gtest for tests. `DEEPSTREAM_ROOT` defaults to
`/opt/nvidia/deepstream/deepstream`; override with a CMake argument if needed.
No Python ML package is needed at runtime.

## Legacy public YOLOX model setup

Only use this section to select the earlier public detector. It overwrites the
active inference config and requires `hand_class_id: 2` in the private config:

```bash
ros2 run hand_detect setup_model.py
```

The model setup downloads a ~240 MiB bundle and extracts one small ONNX file
to `~/kosen/runtime/hand_detect/models`. First inference startup builds a local FP16
TensorRT engine and can take several minutes. Subsequent starts reuse it.
Read [MODEL.md](MODEL.md) for provenance, TAO findings, and replacement steps.
Recorded target results are in [VALIDATION.md](VALIDATION.md).

To build the engine explicitly before opening the camera (recommended for
first bring-up), run this on the target Jetson and let it finish:

```bash
/usr/src/tensorrt/bin/trtexec \
  --onnx="$HOME/kosen/runtime/hand_detect/models/yolox_n_body_head_hand_0461_0.4428_1x3x320x320.onnx" \
  --saveEngine="$HOME/kosen/runtime/hand_detect/models/hand_yolox_n_b1_fp16.engine" \
  --fp16 --skipInference --builderOptimizationLevel=0
```

Optimization level 0 shortened the initial build on the inspected target.
It can trade runtime performance for faster engine building; measure the
resulting inference rate for your workload. Wait for the successful engine-build message. Build engines on the target
hardware/software stack, and regenerate them after a TensorRT upgrade.

## Camera and launch

Ensure the camera is reachable first. The observed Jetson camera interface
was `eno1` with `192.168.1.100/24`; interface names can change. Do not blindly
reconfigure `eth0`. Use `ip route get 192.168.1.64` and check RTSP port 554.

Enter the full, percent-encoded RTSP URL without putting it in shell history:

```bash
read -r -s -p 'RTSP URL: ' HAND_DETECT_RTSP_URL
printf '\n'
export HAND_DETECT_RTSP_URL
ros2 launch hand_detect hand_detect.launch.py
```

Format:
`rtsp://USER:ENCODED_PASSWORD@192.168.1.64:554/Streaming/Channels/102`

Display is enabled by default; run from a Jetson desktop terminal. Over SSH:

```bash
ros2 launch hand_detect hand_detect.launch.py display:=false
```

Do not hard-code `DISPLAY=:0`: use the display session actually available.
Credentials are not logged by this node, but third-party backend logs can still
contain connection details. Do not commit a private URL or attach raw logs.

To use a private YAML file, copy `config/hand_detect.yaml` outside the repo,
edit it, and pass `config:=/absolute/path/camera.local.yaml`. Launch arguments
override display, source_type, inference_enabled, and infer_config.
Parameters are startup settings; restart the node after changing them.

On the configured development Jetson, a private file was created at
`~/kosen/runtime/hand_detect/config/camera.local.yaml` using its existing camera login.
From a desktop terminal, after sourcing Humble and the workspace, launch with:

```bash
ros2 launch hand_detect hand_detect.launch.py \
  config:="$HOME/kosen/runtime/hand_detect/config/camera.local.yaml"
```

Add `display:=false` for SSH/headless operation. This private file is specific
to that Jetson and is ignored by Git. When this file exists, launch selects it
automatically, so `ros2 launch hand_detect hand_detect.launch.py` is sufficient.

## Topics

| Topic | Type | Meaning |
| --- | --- | --- |
| `/hand_detect/status` | `std_msgs/msg/String` | `hand = N` for this frame, or UNKNOWN |
| `/hand_detect/result` | `hand_detect/msg/Inspection` | Timestamp, frame number, PTS, hand count, confidence, reason, timing, drop count |

Both use best-effort, volatile, depth-1 QoS. Subscribers should use compatible
QoS and treat absent/stale messages as unavailable. No latched count survives a
node restart.

```bash
ros2 topic echo /hand_detect/status --qos-reliability best_effort
ros2 topic echo /hand_detect/result --qos-reliability best_effort
```

These topics publish frame **metadata/results**, not raw `sensor_msgs/Image`
pixels. Video stays inside DeepStream and is displayed locally.

`header.stamp` is host time at the probe, not camera exposure time.
`stream_pts_ns` is a media timestamp, not Unix time.
`probe_us` measures metadata extraction before mailbox insertion.
`queue_delay_ms` measures probe-to-publisher waiting.
`pipeline_age_ms` measures decoded input-to-probe time when a PTS match is
available (`-1` otherwise); it excludes camera encoding, network and decoder
delay. None of these alone measures complete camera-to-output latency.

Default confidence threshold is 0.5. The decision region spans the full width
and the top 50% of the frame (`roi_top_fraction: 0.5`). A hand counts when its
bounding-box center is inside this region. Centers exactly on its bottom
boundary are excluded; for a 640x480 output the cutoff is y=240. Boxes may
cross the boundary while their centers remain inside.

Inference still runs on the full frame; the pad probe filters the resulting
detections by region. `hand_count` and `max_confidence` include accepted hands
only. The count resets for every processed frame and is not a running total.
Hands outside the region, low-confidence hands, bodies, and heads are removed
from the display metadata, so only accepted ROI hands receive boxes and the
label `hand`. The display shows `hand = N` on each frame together with the cyan
region outline. To adjust the height, set `roi_top_fraction` in the
private camera YAML and restart the node; 1.0 selects the entire frame.

There is no dwell time or debounce rule yet. A short hand appearance can
be missed if frames/results are dropped. This is not an event-counting API.

## Bring-up without a camera or model

```bash
ros2 launch hand_detect hand_detect.launch.py \
  source_type:=test display:=false inference_enabled:=false
```

This sends a black test pattern through NVIDIA conversion and muxing. Frame
numbers should increase, but status must remain UNKNOWN (`inference_disabled`).
To test inference on the black pattern, omit `inference_enabled:=false`.
It should normally report `hand = 0`; this checks wiring, not detection accuracy.

For a recorded test video:

```bash
ros2 run hand_detect hand_detect_node --ros-args \
  -p source_type:=file -p file_uri:=file:///absolute/path/test.mp4 \
  -p display:=false \
  -p infer_config:="$HOME/kosen/runtime/hand_detect/config/infer.local.txt"
```

File EOS produces UNKNOWN and does not replay. RTSP errors/stalls trigger
reconnection after a bounded backoff. Ctrl+C requests cleanup and joins workers;
backend shutdown or initial engine building can still delay exit.

## Tests and deployment checks

```bash
colcon test \
  --packages-select hand_detect
colcon test-result --test-result-base build/hand_detect --verbose
```

Check known hand and empty scenes, verify `inference_done`, inspect bounding
boxes on the display, and unplug/reconnect the camera to observe UNKNOWN then
recovery. Check CPU/GPU load and end-to-end latency under the actual camera
settings. A successful build is not an accuracy or latency guarantee.
