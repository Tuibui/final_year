"""Keyence GL-R driver — reads per-beam ON/OFF status from a GC-Link controller
(e.g. GC-1000) over Modbus/TCP and publishes CurtainStatus.

Register map (GC Series User's Manual, ch. 19-4, GC-Link port A):

    Word address (decimal / hex)   contents
    970 / 0x03CA                   beams  1..16   (bit 0 = beam 1)
    971 / 0x03CB                   beams 17..32
    972 / 0x03CC                   beams 33..48
    973 / 0x03CD                   beams 49..64   (GL-R52H uses bits 0..3)
    ...                            up to 240 axes

Manual semantics: bit = 1 means the axis is clear (light passing), bit = 0
means the axis is blocked. This driver inverts that so `BeamStatus.blocked`
follows the project convention (1 = blocked).
"""
from __future__ import annotations

import time

import rclpy
from rclpy.node import Node
from rclpy.qos import QoSPresetProfiles

from keyence_glr_msgs.msg import BeamStatus, CurtainStatus

try:
    from pymodbus.client import ModbusTcpClient            # pymodbus 3.x
    _HAVE_PYMODBUS = True
except ImportError:
    try:
        from pymodbus.client.sync import ModbusTcpClient   # pymodbus 2.x
        _HAVE_PYMODBUS = True
    except ImportError:
        _HAVE_PYMODBUS = False
        ModbusTcpClient = None  # type: ignore[assignment]


def _slave_kwarg(client: 'ModbusTcpClient', unit: int) -> dict:
    """`slave=` on pymodbus 3.x, `unit=` on 2.x."""
    import inspect
    fn = client.read_holding_registers
    params = inspect.signature(fn).parameters
    if 'slave' in params:
        return {'slave': unit}
    if 'unit' in params:
        return {'unit': unit}
    return {}


class GcModbusDriver(Node):
    def __init__(self) -> None:
        super().__init__('keyence_glr_driver')

        self.declare_parameter('ip_address', '192.168.0.10')
        self.declare_parameter('port', 502)
        self.declare_parameter('unit_id', 1)
        self.declare_parameter('register_type', 'holding')  # 'holding' or 'input'
        self.declare_parameter('start_address', 970)        # GC-Link port A, beams 1..
        self.declare_parameter('beam_count', 52)
        self.declare_parameter('poll_hz', 20.0)
        self.declare_parameter('frame_id', 'gl_r')
        self.declare_parameter('stale_timeout_s', 0.5)

        self.ip = str(self.get_parameter('ip_address').value)
        self.port = int(self.get_parameter('port').value)
        self.unit_id = int(self.get_parameter('unit_id').value)
        self.register_type = str(self.get_parameter('register_type').value).lower()
        self.start_address = int(self.get_parameter('start_address').value)
        self.beam_count = int(self.get_parameter('beam_count').value)
        self.frame_id = str(self.get_parameter('frame_id').value)
        self.stale_timeout = float(self.get_parameter('stale_timeout_s').value)
        poll_hz = float(self.get_parameter('poll_hz').value)

        if self.register_type not in ('holding', 'input'):
            self.get_logger().fatal(
                f"register_type must be 'holding' or 'input', got {self.register_type!r}"
            )
            raise SystemExit(1)

        # 16 beams per word; round up.
        self.word_count = (self.beam_count + 15) // 16

        self.pub = self.create_publisher(
            CurtainStatus, 'gl_r/status', QoSPresetProfiles.SENSOR_DATA.value
        )

        if not _HAVE_PYMODBUS:
            self.get_logger().fatal(
                "pymodbus not installed. On the Jetson: pip3 install --user 'pymodbus>=3'"
            )
            raise SystemExit(1)

        self.client: 'ModbusTcpClient | None' = None
        self.last_success_t = 0.0

        self.timer = self.create_timer(1.0 / poll_hz, self._tick)
        self.get_logger().info(
            f'Polling {self.ip}:{self.port} unit={self.unit_id} '
            f'{self.register_type} regs {self.start_address}..'
            f'{self.start_address + self.word_count - 1} '
            f'({self.beam_count} beams, {poll_hz} Hz)'
        )

    def _ensure_open(self) -> bool:
        if self.client is not None:
            return True
        try:
            client = ModbusTcpClient(self.ip, port=self.port)
            if not client.connect():
                self.get_logger().warn(f'Modbus connect failed to {self.ip}:{self.port}')
                return False
            self.client = client
            return True
        except Exception as exc:
            self.get_logger().warn(f'Modbus open error: {exc}')
            self.client = None
            return False

    def _read_words(self) -> list[int] | None:
        if not self._ensure_open():
            return None
        assert self.client is not None
        fn = (self.client.read_holding_registers
              if self.register_type == 'holding'
              else self.client.read_input_registers)
        try:
            try:
                rr = fn(self.start_address, count=self.word_count,
                        **_slave_kwarg(self.client, self.unit_id))
            except TypeError:
                rr = fn(self.start_address, self.word_count,
                        **_slave_kwarg(self.client, self.unit_id))
        except Exception as exc:
            self.get_logger().warn(f'Modbus read error: {exc}; will reconnect')
            self._drop_client()
            return None
        if rr is None or (hasattr(rr, 'isError') and rr.isError()):
            self.get_logger().warn(f'Modbus read failed: {rr}; will reconnect')
            self._drop_client()
            return None
        return list(rr.registers)

    def _drop_client(self) -> None:
        if self.client is not None:
            try:
                self.client.close()
            except Exception:
                pass
        self.client = None

    def _tick(self) -> None:
        words = self._read_words()
        now = self.get_clock().now().to_msg()
        msg = CurtainStatus()
        msg.header.stamp = now
        msg.header.frame_id = self.frame_id
        msg.beam_count = self.beam_count

        if words is None or len(words) < self.word_count:
            stale = (time.monotonic() - self.last_success_t) > self.stale_timeout
            health = BeamStatus.HEALTH_STALE if stale else BeamStatus.HEALTH_FAULT
            msg.beams = [
                BeamStatus(index=i, blocked=False, health=health)
                for i in range(self.beam_count)
            ]
            self.pub.publish(msg)
            return

        self.last_success_t = time.monotonic()

        # Manual: bit = 1 -> clear (light on), bit = 0 -> blocked.
        blocked = 0
        beams: list[BeamStatus] = []
        for i in range(self.beam_count):
            word = words[i // 16]
            bit = (word >> (i % 16)) & 1
            is_blocked = bit == 0
            if is_blocked:
                blocked += 1
            beams.append(
                BeamStatus(index=i, blocked=is_blocked, health=BeamStatus.HEALTH_OK)
            )
        msg.beams = beams
        msg.any_blocked = blocked > 0
        msg.blocked_count = blocked
        # OSSD A/B, lockout, muted not exposed via this register block; leave False.
        self.pub.publish(msg)


def main(argv: list[str] | None = None) -> None:
    rclpy.init(args=argv)
    node = GcModbusDriver()
    try:
        rclpy.spin(node)
    except KeyboardInterrupt:
        pass
    finally:
        node._drop_client()
        node.destroy_node()
        rclpy.shutdown()


if __name__ == '__main__':
    main()
