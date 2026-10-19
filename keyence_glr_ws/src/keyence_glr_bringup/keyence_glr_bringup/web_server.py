"""Tiny static HTTP server that serves the dashboard from share/web/.
Kept as a ROS node so it lives/dies with the launch file."""
from __future__ import annotations

import functools
import http.server
import os
import socketserver
import threading

import rclpy
from rclpy.node import Node
from ament_index_python.packages import get_package_share_directory


class WebServerNode(Node):
    def __init__(self) -> None:
        super().__init__('keyence_glr_web')
        self.declare_parameter('port', 8000)
        port = int(self.get_parameter('port').value)
        web_dir = os.path.join(
            get_package_share_directory('keyence_glr_bringup'), 'web'
        )
        handler = functools.partial(http.server.SimpleHTTPRequestHandler, directory=web_dir)
        self.httpd = socketserver.TCPServer(('0.0.0.0', port), handler)
        self.httpd.allow_reuse_address = True
        self.thread = threading.Thread(target=self.httpd.serve_forever, daemon=True)
        self.thread.start()
        self.get_logger().info(f'Dashboard at http://<host>:{port}/  (serving {web_dir})')

    def destroy_node(self) -> bool:
        try:
            self.httpd.shutdown()
            self.httpd.server_close()
        except Exception:
            pass
        return super().destroy_node()


def main(argv: list[str] | None = None) -> None:
    rclpy.init(args=argv)
    node = WebServerNode()
    try:
        rclpy.spin(node)
    except KeyboardInterrupt:
        pass
    finally:
        node.destroy_node()
        rclpy.shutdown()


if __name__ == '__main__':
    main()
