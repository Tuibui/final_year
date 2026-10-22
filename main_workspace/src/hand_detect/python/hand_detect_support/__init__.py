"""Repository-local deployment paths for hand_detect."""

import os
from pathlib import Path

from ament_index_python.packages import get_package_prefix


def runtime_directory():
    """Find the repository containing the installed workspace, regardless of its name."""
    prefix = Path(get_package_prefix("hand_detect")).resolve()
    for candidate in (prefix, *prefix.parents):
        if (candidate / "main_workspace/src/hand_detect/package.xml").is_file():
            return candidate / "runtime/hand_detect"
    raise RuntimeError("hand_detect must be built inside the repository's main_workspace")


def prepare_runtime():
    """Create project directories and keep child-process logs and temp files local."""
    directory = runtime_directory()
    for name in ("models", "config", "cache", "logs/ros", "tmp"):
        (directory / name).mkdir(parents=True, exist_ok=True)
    os.environ["HAND_DETECT_DATA_DIR"] = str(directory)
    os.environ["ROS_LOG_DIR"] = str(directory / "logs/ros")
    os.environ["TMPDIR"] = str(directory / "tmp")
    return directory
