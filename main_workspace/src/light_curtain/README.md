# Light curtain — ROS 2 Humble

`light_curtain` is the single project package for Keyence GL-R / GC-1000.
It contains the messages, real Modbus driver, mock publisher, ROSbridge launch
files, and the optional local web monitor. It replaces `keyence_glr_msgs`,
`keyence_glr_driver`, and `keyence_glr_bringup`.

The camera remains in `hand_detect`; the systems do not combine decisions.
ROSbridge and the native C# GUI remain separate processes.

## Run on Jetson

After sourcing ROS 2 Humble and the workspace in the terminal:

```bash
ros2 launch light_curtain desktop_monitor.launch.py
```

This starts the real GC-1000 driver and ROSbridge on port 9090. Open the GUI
from the Jetson desktop in a second terminal:

```bash
cd ~/kosen/main_workspace/tools/LightCurtainMonitor
./run.sh
```

The default parameter file is
`~/kosen/runtime/light_curtain/config/gc_modbus.local.yaml` when present,
otherwise the packaged `config/gc_modbus.yaml`. Logs stay under
`~/kosen/runtime/light_curtain/logs`. The existing node name
`keyence_glr_driver` is retained so machine-local YAML files continue to work.

If the driver is already running, use `start_driver:=false`. If ROSbridge is
already running, reuse it rather than starting a second bridge on port 9090.
Stop old launch sessions with Ctrl+C before starting the new package.

For the driver alone, without ROSbridge or any GUI:

```bash
ros2 launch light_curtain driver.launch.py
```

The optional web monitor is `monitor.launch.py`; it starts ROSbridge and the
HTTP server on port 8000. Add `start_driver:=true` if the driver is stopped.
`demo.launch.py` uses explicitly synthetic data by default; `mock.launch.py`
starts only the synthetic publisher.

## Build and migration

Build on Jetson, not on the Jazzy development computer:

```bash
source /opt/ros/humble/setup.bash
cd ~/kosen/main_workspace
colcon build --packages-select light_curtain
source install/local_setup.bash
```

When migrating an existing workspace, move the old three package directories
out of `src`, `build`, and `install` before building. Keep any backup outside
the workspace under `~/kosen/runtime/light_curtain/`. Open a fresh terminal
and source Humble plus the updated workspace after the migration.

The updated C# GUI and web monitor subscribe to
`light_curtain/msg/CurtainStatus`. Existing clients that hard-code
`keyence_glr_msgs/msg/CurtainStatus` must change their message type and restart.
The `/gl_r/status` topic and message fields are unchanged.

## Network and beam data

The wired configuration observed on 2026-10-22 is:

| Device | Jetson interface / address | Device address |
| --- | --- | --- |
| GC-1000 | `eno1`, `192.168.0.100/24`, profile `gc1000` | `192.168.0.10:502` |
| Hikvision camera | `eth1`, `192.168.1.100/24`, profile `kosen_ip` | `192.168.1.64:554` |

Both profiles use `ipv4.never-default` so Wi-Fi can carry SSH and internet.
Use actual device addresses if wiring or addressing changes.

The real driver polls GC-Link Port A at 20 Hz, reading holding registers
970–973 for 52 beams. Each raw bit is inverted: 1 means clear on the controller;
`BeamStatus.blocked` is true for a blocked beam. No writes are issued.
Synchronous reads use a 0.2-second timeout and a 1-second reconnect delay.
The requested polling rate is not guaranteed while the controller is unavailable.
Current and older pymodbus device-ID keyword forms are supported.

Messages use best-effort sensor-data QoS. Each beam's `health` is 0=OK,
1=STALE, or 2=FAULT; unavailable data does not establish a clear beam.
OSSD A/B, lockout, and muted are not read from this register block, so their
default false values are not measured states.

```bash
ros2 topic echo /gl_r/status --qos-reliability best_effort
```

Port B / `/gl_r_y/status` is reserved for future work; the current real driver
reads Port A only. The GUI shows Y as unavailable until a publisher exists.
There is no beam-to-shelf-cell mapping or camera/light-curtain fusion.
