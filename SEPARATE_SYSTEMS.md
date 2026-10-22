# Run camera and light curtain separately on Jetson

The two systems remain separate executables, ROS nodes, and topics. There is
no combined launch file, decision logic, or camera/light-curtain subscription.
Start each manually. Everything below belongs to `~/kosen` on the target.

| System | Node | Output |
| --- | --- | --- |
| Camera / DeepStream | `/hand_detect` | `/hand_detect/status`, `/hand_detect/result` |
| GC-1000 / light curtain | `/keyence_glr_driver` | `/gl_r/status` |

Both can share a ROS domain while remaining independent. On the Jetson as
wired on 2026-10-22, `eno1` has `192.168.0.100/24` for GC-1000
`192.168.0.10:502`, and `eth1` has `192.168.1.100/24` for camera
`192.168.1.64:554`. See the
[light-curtain README](main_workspace/src/light_curtain/README.md).

## Camera terminal

Use the Jetson desktop for the display:

```bash
source /opt/ros/humble/setup.bash
source ~/kosen/main_workspace/install/local_setup.bash
ros2 launch hand_detect hand_detect.launch.py
```

For an SSH-only run, add `display:=false`. Keep the display setting the same
when comparing CPU measurements. Do not start a second camera instance if an
existing one is already running.

## Light-curtain terminal

After confirming the network and editing the local controller config:

```bash
source /opt/ros/humble/setup.bash
source ~/kosen/main_workspace/install/local_setup.bash
ros2 launch light_curtain driver.launch.py
```

Use Ctrl+C in the corresponding terminal to stop that system. Running both
commands at the same time still leaves their processes and decisions separate.
The `light_curtain` package automatically selects the private controller
configuration. To start the driver together with ROSbridge for the C# GUI,
use `ros2 launch light_curtain desktop_monitor.launch.py` instead.

## CPU measurement terminal

The monitor reads `/proc` using the Python standard library. It does not start
ROS nodes, contact the controller, or launch the camera. It reports per-process
CPU, RSS memory, thread counts, PIDs, and total system CPU.

```bash
cd ~/kosen
python3 main_workspace/tools/monitor_cpu.py --interval 1
```

Optionally record a 60-second CSV, using a new filename for each run:

```bash
python3 main_workspace/tools/monitor_cpu.py --interval 1 --duration 60 \
  --output runtime/performance/camera_only.csv
```

Suggested sequence:

1. Stop both systems; record `idle.csv`.
2. Start only the camera; wait until engine loading and stream startup finish,
   then record `camera_only.csv`.
3. Stop the camera and start only the light-curtain driver; confirm valid beam
   health before recording `curtain_only.csv`.
4. If desired, start both in their separate terminals and record `both_separate.csv`.

CPU percentages use different scales: **100% process CPU means one full CPU
core**, so a multithreaded process can exceed 100%. System CPU is normalized
across all CPU cores and ranges from 0 to 100%. A dash means the process is
stopped or needs another sample after startup. If multiple instances of a
system run, their values are summed and all matching PIDs are listed.

The monitor counts the camera/driver processes and their internal threads.
It excludes launcher, web-dashboard, and rosbridge overhead. Do not sum process
RSS to infer unique physical memory usage because shared pages can appear in
multiple processes.

For Jetson-wide CPU/GPU/memory information, run manually in another terminal:

```bash
tegrastats --interval 1000
```

Or inspect process/thread details using built-in tools:

```bash
pgrep -af 'hand_detect_node|gc_modbus_driver'
top
```

During the initial standalone preparation, no camera/light-curtain process
was started. ARP/TCP
controller reachability was verified after adding the secondary Ethernet
address. Modbus register responses, beam transitions, and hardware CPU figures
remain for the user's manual run.

## Local light-curtain web monitor

The project dashboard subscribes only to `/gl_r/status`. It shows each beam,
the blocked-beam count, and data/connection health. It does not use camera
results. Its JavaScript runs locally without an external CDN.

If the real light-curtain driver is already running, open only the web monitor:

```bash
source /opt/ros/humble/setup.bash
source ~/kosen/main_workspace/install/local_setup.bash
ROS_LOG_DIR="$HOME/kosen/runtime/light_curtain/logs" \
  ros2 launch light_curtain monitor.launch.py
```

If the driver is stopped, start the real driver with the monitor:

```bash
ROS_LOG_DIR="$HOME/kosen/runtime/light_curtain/logs" \
  ros2 launch light_curtain monitor.launch.py start_driver:=true \
  config:="$HOME/kosen/runtime/light_curtain/config/gc_modbus.local.yaml"
```

Open `http://localhost:8000` on the Jetson, or `http://172.20.10.3:8000` from
the computer connected to its Wi-Fi network. Port 9090 carries the ROS bridge
WebSocket. If using a different bridge port, append `?rosbridge_port=PORT`
to the page URL.

The UI shows 1=blocked, 0=clear, and a dash for unavailable beam data. It clears
old indications on disconnection and marks data unavailable after one second
without fresh healthy updates. It reconnects automatically when the bridge
returns. The optional driver is always the real GC-1000 driver, never mock data.
The dashboard adds separate rosbridge and HTTP processes to system load.

The web monitor was subsequently started at the user's request, with the real
light-curtain driver because no driver was running. HTTP access was verified
both on Jetson and from the development computer. A WebSocket subscription
received all 52 beams with HEALTH_OK. The camera was not started. Monitor
launch PID and logs are stored at `runtime/light_curtain/monitor.pid` and
`runtime/light_curtain/logs/monitor_launch.log`.
