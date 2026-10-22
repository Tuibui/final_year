#!/usr/bin/env python3
"""Run an isolated target-side pipeline and check its ROS output and shutdown."""

import argparse
from collections import Counter
import json
import os
from pathlib import Path
import signal
import subprocess
import tempfile
import time

import rclpy
from rclpy.qos import QoSProfile, ReliabilityPolicy
from ament_index_python.packages import get_package_prefix
from hand_detect.msg import Inspection
from hand_detect_support import prepare_runtime


def main():
    runtime = prepare_runtime()
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source-type", choices=["test", "rtsp", "file"], default="test")
    parser.add_argument("--inference", action="store_true")
    parser.add_argument("--display", action="store_true")
    parser.add_argument("--file-uri", default="")
    parser.add_argument("--seconds", type=float, default=15)
    parser.add_argument("--expect", help='Expected status, such as "hand = 0" or "UNKNOWN"')
    args = parser.parse_args()
    # Keep smoke-test traffic separate from the user's normal ROS graph.
    os.environ["ROS_DOMAIN_ID"] = "77"
    os.environ["ROS_LOCALHOST_ONLY"] = "1"
    executable = Path(get_package_prefix("hand_detect")) / "lib/hand_detect/hand_detect_node"
    config = runtime / "config/infer.local.txt"
    command = [
        str(executable), "--ros-args",
        "-p", f"source_type:={args.source_type}",
        "-p", f"display:={str(args.display).lower()}",
        "-p", f"inference_enabled:={str(args.inference).lower()}",
        "-p", f"infer_config:={config}",
    ]
    if args.source_type == "file":
        command.extend(["-p", f"file_uri:={args.file_uri}"])
    messages = []
    rclpy.init()
    node = rclpy.create_node("hand_detect_smoke")
    node.create_subscription(
        Inspection, "/hand_detect/result", messages.append,
        QoSProfile(depth=10, reliability=ReliabilityPolicy.BEST_EFFORT),
    )
    process = None
    graceful = False
    log_path = None
    try:
        with tempfile.NamedTemporaryFile(
            prefix="hand_detect_smoke_", suffix=".log", delete=False,
            dir=runtime / "logs",
        ) as log:
            log_path = log.name
            process = subprocess.Popen(command, stdout=log, stderr=subprocess.STDOUT)
            end = time.monotonic() + args.seconds
            while time.monotonic() < end and process.poll() is None:
                rclpy.spin_once(node, timeout_sec=0.1)
            if process.poll() is None:
                process.send_signal(signal.SIGINT)
            try:
                process.wait(timeout=15)
                graceful = process.returncode == 0
            except subprocess.TimeoutExpired:
                process.kill()
                process.wait()
        frames = [m for m in messages if m.reason in (
            "inference_disabled", "not_inferred", "hand_detected",
            "no_hand", "pipeline_stale", "result_stale",
        )]
        summary = {
            "messages": len(messages),
            "frames": len(frames),
            "statuses": dict(Counter(m.status for m in messages)),
            "reasons": dict(Counter(m.reason for m in messages)),
            "inferred_frames": sum(m.inference_done for m in frames),
            "max_hands": max((m.hand_count for m in frames), default=0),
            "max_probe_us": max((m.probe_us for m in frames), default=0),
            "max_queue_delay_ms": max((m.queue_delay_ms for m in frames), default=0),
            "max_pipeline_age_ms": max((m.pipeline_age_ms for m in frames), default=-1),
            "graceful_shutdown": graceful,
            "log": log_path,
        }
        print(json.dumps(summary, indent=2))
        assert frames, "No frame metadata received; inspect the private log"
        assert graceful, "Node did not exit cleanly"
        assert all(m.status == "UNKNOWN" or m.status == f"hand = {m.hand_count}"
                   for m in messages), "Status and numeric frame count disagree"
        if args.inference:
            assert any(m.inference_done for m in frames), "Inference did not run"
        else:
            assert all(m.status == "UNKNOWN" for m in messages), "Invalid disabled result"
        if args.expect:
            assert any(m.status == args.expect for m in frames), "Expected status not observed"
    finally:
        if process is not None and process.poll() is None:
            process.kill()
            process.wait()
        node.destroy_node()
        rclpy.shutdown()


if __name__ == "__main__":
    main()
