"""Resolve the private controller config and logs inside the repository."""

import os
from pathlib import Path

from ament_index_python.packages import get_package_share_directory


def prepare_runtime():
    share = Path(get_package_share_directory('light_curtain')).resolve()
    config = share / 'config/gc_modbus.yaml'
    for candidate in share.parents:
        if (candidate / 'main_workspace/src/light_curtain/package.xml').is_file():
            runtime = candidate / 'runtime/light_curtain'
            logs = runtime / 'logs'
            logs.mkdir(parents=True, exist_ok=True)
            os.environ['ROS_LOG_DIR'] = str(logs)
            private_config = runtime / 'config/gc_modbus.local.yaml'
            if private_config.is_file():
                config = private_config
            break
    return str(config)
