# C# light-curtain shelf debug monitor

Native C# / Avalonia desktop application. White background, flat colors,
square borders, no gradients. The shelf has a static 5 × 5 outline surrounded
by four yellow light-curtain housings:

```text
                    Y transmitter
                 ↓ ↓ ↓ ... ↓ ↓ ↓
    X transmitter →  shelf front  → X receiver
                 ↓ ↓ ↓ ... ↓ ↓ ↓
                     Y receiver
```

X is the left/right pair, with horizontal rays and beam 52 at the top.
Y is the top/bottom pair, with vertical rays and beam 1 at the left.
This swaps the displayed axis orientation from the initial prototype while
keeping X on Port A and Y on Port B.

Every received beam is displayed independently: **1 = blocked, 0 = clear,
— = unavailable**. The receiver housings display each value with its beam
number. Red lines show individual blocked beams; faint green lines show
individual clear beams. Unknown beams have no ray. TX markers show emitter
positions, not a separately measured transmitter-health signal.

The window contains only the centered 2D display and a compact toolbar:
X/Y health, ROSbridge connection status, endpoint, and Reconnect. Headings,
sidebar panels, raw-value tables, legends, and footer text have been removed.
Hovering over the diagram shows nearby beam indices and values. Connection
and axis details are available as tooltips on the small status labels.
The 5 × 5 shelf lines are visual references only: there is no beam grouping,
slot mapping, cell highlighting, intersection decision, or inferred hand count.

The GUI is read only. It subscribes to ROSbridge and does not open Modbus,
change GC-1000 configuration, publish decisions, or connect to the camera.
The Modbus polling behavior is unchanged. The driver, messages, and launch files
now belong to one ROS package, `light_curtain`.

## Run on the development computer

From the repository root:

```bash
bash main_workspace/tools/LightCurtainMonitor/run.sh
```

`run.sh` first opens the prepared native build under `runtime/gui/linux-x64`
or `runtime/gui/linux-arm64`, depending on the machine. No .NET SDK is needed
when that build is present. Otherwise it falls back to running the source
with a .NET 8 SDK.

The default endpoint on Linux ARM64 (Jetson) is `ws://localhost:9090`.
On the development computer it is `ws://172.20.10.3:9090`. Edit the endpoint and click
**RECONNECT**, or pass it at startup:

```bash
bash main_workspace/tools/LightCurtainMonitor/run.sh --endpoint ws://172.20.10.3:9090
```

The prepared self-contained Linux x64 build needs no SDK:

```bash
./runtime/gui/linux-x64/LightCurtainMonitor
```

The GUI computer does not need ROS. A graphical desktop is required; a plain
SSH terminal without display forwarding cannot show the window. Avalonia 11
uses X11, including XWayland under a Wayland desktop.

If needed, install the SDK entirely inside the repository:

```bash
mkdir -p runtime/tools
curl -fsSL https://dot.net/v1/dotnet-install.sh -o runtime/tools/dotnet-install.sh
bash runtime/tools/dotnet-install.sh --channel 8.0 \
  --install-dir "$PWD/runtime/tools/dotnet" --no-path
```

`run.sh` finds this SDK and keeps the CLI home and NuGet cache under
`runtime/tools`. Generated SDK files, binaries, and caches are ignored by Git.

## Jetson data source / ROS 2 Humble

To open the prepared GUI on the Jetson desktop:

```bash
cd ~/kosen/main_workspace/tools/LightCurtainMonitor
./run.sh
```

The launcher connects to the local ROSbridge by default. It does not start
ROSbridge or the Modbus driver; start those separately as described below.
Pass `--endpoint` or `--config` to use another connection configuration.

After deploying source, install the bringup launch file on Jetson:

```bash
source /opt/ros/humble/setup.bash
cd ~/kosen/main_workspace
colcon build --packages-select light_curtain
source install/local_setup.bash
```

If the real driver is stopped, start the existing Port A driver and ROSbridge:

```bash
ros2 launch light_curtain desktop_monitor.launch.py
```

The launch starts the real driver and ROSbridge by default. It automatically
uses `runtime/light_curtain/config/gc_modbus.local.yaml` when present,
otherwise the packaged `gc_modbus.yaml`. Child-process logs use
`runtime/light_curtain/logs`. The `config:=...` override remains available.
The GUI subscribes using `light_curtain/msg/CurtainStatus`; restart the GUI
and ROSbridge after migrating from the old `keyence_glr_*` packages.

If the driver is running, pass `start_driver:=false`. If ROSbridge is already
running, reuse it instead of starting another bridge on port 9090. This launch
does not start a camera, HTTP server, or mock publisher. Stop it with Ctrl+C.
The Jetson must have a working wired route to GC-1000 `192.168.0.10:502`.

## Axis sources and health

| Axis | Diagram | GC-Link port | Default ROS topic |
| --- | --- | --- | --- |
| X | Left TX / right RX | A | `/gl_r/status` |
| Y | Top TX / bottom RX | B | `/gl_r_y/status` |

Port A = X and Port B = Y are configurable assumptions. The GUI cannot infer
physical orientation from wiring. The current driver polls Port A only; a
future Port B publisher must provide the second topic and correct per-port
health. Y initially shows **DISCONNECTED / No axis data**. An absent topic
is not proof of the physical GC-Link port state.

- **LIVE**: all expected beams arrived recently with `HEALTH_OK`.
- **DISCONNECTED**: ROSbridge is offline, or the axis has not sent a frame.
- **STALE**: no new frame within `staleAfterMs` (default 1000 ms).
- **FAULT**: invalid data, a subscription error, or nonzero beam health.

Every beam with missing/stale/faulted data displays a dash. If only some beams
are faulted, other healthy beam values remain visible, as in the web monitor.
Bridge disconnect/reconnect clears cached frames. Default-false OSSD, lockout,
and muted fields are not used as measured connection/health indicators.

`monitor.json` configures endpoint, timeout, port labels, topic names, beam
counts, and orientation. `reverse` changes display order only; raw beam IDs
remain unchanged. ROS indices are 0-based and GUI labels are 1-based.

```bash
bash main_workspace/tools/LightCurtainMonitor/run.sh --config /path/to/monitor.json
```

## Demo and verification

These explicitly labeled demo modes do not connect to ROS or hardware:

```bash
# X synthetic data; Y disconnected.
bash main_workspace/tools/LightCurtainMonitor/run.sh --demo

# Synthetic data for both independent axes.
bash main_workspace/tools/LightCurtainMonitor/run.sh --demo-both
```

Run health, raw-beam identity/orientation, fragmented WebSocket/disconnect,
and headless-render checks from the repository root:

```bash
DOTNET_CLI_HOME="$PWD/runtime/tools/dotnet-home" \
NUGET_PACKAGES="$PWD/runtime/tools/nuget" \
MONITOR_SCREENSHOT_DIR="$PWD/runtime/gui-validation" \
  runtime/tools/dotnet/dotnet test \
  main_workspace/tools/LightCurtainMonitor.Tests/LightCurtainMonitor.Tests.csproj -c Release
```

Generated previews and the local GUI PID/log are in `runtime/gui-validation/`.
The Jetson data-source PID/log are in `runtime/light_curtain/desktop_monitor.pid`
and `runtime/light_curtain/logs/desktop_monitor.log`. Port A hardware reception
was verified on 2026-10-21: 52 healthy beams and 187 messages during a 20-second
ROSbridge observation. Port B hardware has not been tested.

## Publish for Linux or Windows

With `dotnet` on PATH, run from this project directory:

```bash
# Jetson / Ubuntu ARM64 desktop.
dotnet publish -c Release -r linux-arm64 --self-contained true \
  -o ../../../runtime/gui/linux-arm64

# Ubuntu x64 desktop.
dotnet publish -c Release -r linux-x64 --self-contained true \
  -o ../../../runtime/gui/linux-x64

# Windows x64 desktop.
dotnet publish -c Release -r win-x64 --self-contained true \
  -o ../../../runtime/gui/win-x64
```

Copy the whole output directory, including `monitor.json` and native libraries.
On Jetson keep it at `~/kosen/runtime/gui/linux-arm64` and run from its desktop:

```bash
~/kosen/runtime/gui/linux-arm64/LightCurtainMonitor --endpoint ws://localhost:9090
```

A self-contained build does not need a target SDK/runtime installation; native
OS dependencies still apply. The SDK/build stays on the development computer.

References: [Avalonia supported platforms](https://v11.docs.avaloniaui.net/docs/overview/supported-platforms/),
[headless rendering](https://v11.docs.avaloniaui.net/docs/concepts/headless/),
[ROSbridge protocol](https://github.com/RobotWebTools/rosbridge_suite/blob/ros2/ROSBRIDGE_PROTOCOL.md).
