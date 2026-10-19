"""Mock CurtainStatus publisher — sweeps a virtual "hand" across the beams so
you can develop the dashboard without the GL-R + GC-1000 hooked up."""
from __future__ import annotations

import math

import rclpy
from rclpy.node import Node
from rclpy.qos import QoSPresetProfiles

from keyence_glr_msgs.msg import BeamStatus, CurtainStatus


class MockPublisher(Node):
    def __init__(self) -> None:
        super().__init__('keyence_glr_mock')

        self.declare_parameter('beam_count', 64)
        self.declare_parameter('publish_hz', 20.0)
        self.declare_parameter('frame_id', 'gl_r')
        self.declare_parameter('hand_width', 6)
        self.declare_parameter('sweep_period_s', 4.0)

        self.beam_count = int(self.get_parameter('beam_count').value)
        self.frame_id = self.get_parameter('frame_id').value
        self.hand_width = int(self.get_parameter('hand_width').value)
        self.period = float(self.get_parameter('sweep_period_s').value)
        hz = float(self.get_parameter('publish_hz').value)

        self.pub = self.create_publisher(
            CurtainStatus, 'gl_r/status', QoSPresetProfiles.SENSOR_DATA.value
        )
        self.t0 = self.get_clock().now()
        self.create_timer(1.0 / hz, self._tick)
        self.get_logger().info(
            f'Mocking {self.beam_count} beams, sweeping width={self.hand_width} '
            f'period={self.period}s @ {hz} Hz'
        )

    def _tick(self) -> None:
        now = self.get_clock().now()
        t = (now - self.t0).nanoseconds * 1e-9
        phase = (t % self.period) / self.period
        center = phase * self.beam_count
        half = self.hand_width / 2.0

        msg = CurtainStatus()
        msg.header.stamp = now.to_msg()
        msg.header.frame_id = self.frame_id
        msg.beam_count = self.beam_count
        msg.ossd_a = True
        msg.ossd_b = True
        msg.lockout = False
        msg.muted = False

        blocked = 0
        beams: list[BeamStatus] = []
        for i in range(self.beam_count):
            is_blocked = math.fabs(i - center) < half
            if is_blocked:
                blocked += 1
            beams.append(
                BeamStatus(index=i, blocked=is_blocked, health=BeamStatus.HEALTH_OK)
            )
        msg.beams = beams
        msg.any_blocked = blocked > 0
        msg.blocked_count = blocked
        if blocked > 0:
            msg.ossd_a = False
            msg.ossd_b = False
        self.pub.publish(msg)


def main(argv: list[str] | None = None) -> None:
    rclpy.init(args=argv)
    node = MockPublisher()
    try:
        rclpy.spin(node)
    except KeyboardInterrupt:
        pass
    finally:
        node.destroy_node()
        rclpy.shutdown()


if __name__ == '__main__':
    main()
