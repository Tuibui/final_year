# Target validation — 2026-10-20

Source lives in `main_workspace/src/hand_detect` on both the development machine
and `~/kosen` on the target. Builds and runtime checks were performed on the
target only. No build was run on the Jazzy development machine.

## Inspected target

- NVIDIA Jetson Orin NX Engineering Reference Developer Kit, 8 GB RAM.
- Ubuntu 22.04.5, L4T R36.4.7, ROS 2 Humble.
- DeepStream 7.1, TensorRT 10.3.0, CUDA 12.5.
- Hikvision RTSP substream, camera `192.168.1.64`, Jetson camera interface
  `eno1` with `192.168.1.100/24` during validation.
- Desktop X11 display `:1`; the display number is session-dependent.

## Results

The initial checks below used the former OK/NG decision output. The current
node publishes per-frame `hand = N` status, as described in the latest section.

| Check | Observed result |
| --- | --- |
| Humble build | Passed on ARM64; SDK header/macro warnings on the initial parser compilation |
| Unit tests | 7 tests, 0 errors, 0 failures, 0 skipped |
| ONNX integrity | Validated with ONNX checker; SHA256 pinned in setup script |
| TensorRT FP16 engine | Built and deserialized successfully with optimization level 0 |
| Synthetic video, no inference, headless | 173 frame messages, all UNKNOWN; clean shutdown |
| Synthetic video, no inference, display | 145 frame messages, all UNKNOWN; clean shutdown |
| Black video with inference | 176 inferred frames, all NG after initial waiting status |
| Public hand clip with inference | 70 inferred frame results; OK observed, up to 2 hands; EOS published UNKNOWN |
| Hikvision RTSP, no inference | 202 frame messages; UNKNOWN as required |
| Hikvision RTSP, inference and display | 352 inferred frame messages, NG in the observed scene; clean shutdown |

The hand sample was the public OpenCV palm-detector demonstration
[`mppalmdet_demo.gif`](https://github.com/opencv/opencv_zoo/tree/main/models/palm_detection_mediapipe/example_outputs),
converted locally into a short H.264 clip. It contains existing overlays and
is a wiring/positive-detection check, not an accuracy benchmark. It produced
8 OK, 54 NG, and 8 stale UNKNOWN frame results, followed by EOS UNKNOWN.
This illustrates that detection can vary across frames and the initial model
still needs validation against the user's actual scene.

In the 20-second live RTSP run with inference and display:

- Maximum measured metadata extraction time: 81.984 microseconds.
- Maximum measured probe-to-publisher queue delay: 0.129791 milliseconds.
- Maximum measured decoded-input-to-probe age: 66.294177 milliseconds.

These are maxima in one short run, not guarantees or complete camera-to-result
latency. They exclude camera encoding and upstream network/decoder buffering.
No live-camera hand presentation was confirmed during this run, so live OK/NG
switching and actual-scene accuracy still require user validation. Physical
disconnect/reconnect recovery has not been verified.

## Local deployment data

Weights and TensorRT engine are under
`~/kosen/runtime/hand_detect/models` on the Jetson. Generated nvinfer configuration
is under `~/kosen/runtime/hand_detect/config`. Camera credentials are in the private
`~/kosen/runtime/hand_detect/config/camera.local.yaml` with mode 0600. Neither is copied into
version-controlled source. Existing user files were retained during the rename.
All deployment data is inside `~/kosen`; `runtime/` is ignored by Git.

After relocation, the model checksum was revalidated and all active model,
label, parser, and config paths were checked to be under `~/kosen`. The default
launch command selected the private camera file, loaded the existing engine,
displayed the live RTSP pipeline, and exited cleanly. The environment hooks
were checked to set project-local `ROS_LOG_DIR` and `TMPDIR` paths.

## Standard workspace outputs

All five workspace packages were rebuilt under Humble into the standard
`main_workspace/build/`, `main_workspace/install/`, and `main_workspace/log/`
directories. The previous outputs were preserved under
`runtime/workspace_backups/20261020_130601/`. The generated nvinfer config was
updated to load the custom parser from `main_workspace/install/hand_detect/lib/`.
Use `source ~/kosen/main_workspace/install/local_setup.bash` in new terminals.

## Top-75-percent detection region

The pad probe now counts hands only when their bounding-box centers are inside
the full-width top 75% of the muxed frame. The cutoff is y=360 at 640x480;
centers on the cutoff are excluded. The display includes a cyan ROI outline
and label. Inference still uses the full image.

The updated package built on Jetson, and all 8 reported tests passed, including
inside/outside ROI, exact boundary, crossing boxes, invalid geometry, and full
frame configuration. The existing camera launch was stopped cleanly and
restarted with its original display and ROS settings. The background launch
PID is recorded in `runtime/hand_detect/launch.pid`, and its log is
`runtime/hand_detect/logs/roi_launch.log`.

## Per-frame hand count and filtered labels

The display retains only accepted hand detections inside the ROI; body/head
objects, low-confidence hands, and hands outside the ROI are removed from the
metadata before OSD. Accepted boxes are labeled `hand`. A separate overlay
shows `hand = N` for each processed frame, and the same string is published on
`/hand_detect/status`. `Inspection.hand_count` remains the numeric count for
that frame. The count is not cumulative or limited to two hands.

Missing/stale inference remains UNKNOWN. Count-format tests cover 0, 1, 2,
3, and 10 hands alongside unavailable data and the ROI boundary cases.
The updated Humble build and all 8 reported tests passed on the Jetson.
The active background launch PID is in `runtime/hand_detect/launch.pid`;
its current log is `runtime/hand_detect/logs/count_launch.log`.

Live verification received 99 current frame results with matching text/numeric
counts. A 70-frame replay of the public hand clip with display enabled produced
66 `hand = 0`, 3 `hand = 1`, and 1 `hand = 2` results, followed by EOS UNKNOWN.
The replay exited cleanly and left the live camera launch running.
