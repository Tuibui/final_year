#!/usr/bin/env python3
"""Download the public hand detector and write a machine-local nvinfer config."""

import argparse
import hashlib
from pathlib import Path
import shutil
import tarfile
import urllib.request

from ament_index_python.packages import get_package_prefix
from hand_detect_support import prepare_runtime

BUNDLE = (
    "https://s3.ap-northeast-2.wasabisys.com/pinto-model-zoo/"
    "426_YOLOX-Body-Head-Hand/resources_n.tar.gz"
)
MODEL = "yolox_n_body_head_hand_0461_0.4428_1x3x320x320.onnx"
SHA256 = "e69376d5b7baec2bf6073b568bbaa1da8db8068cafa6cc6f8ef92ec94ae8f21c"


def main():
    runtime = prepare_runtime()
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--directory", type=Path,
        default=runtime / "models",
    )
    parser.add_argument(
        "--archive", type=Path,
        default=runtime / "cache/resources_n.tar.gz",
    )
    args = parser.parse_args()
    directory = args.directory.expanduser().resolve()
    directory.mkdir(parents=True, exist_ok=True)
    model = directory / MODEL
    if not model.exists():
        archive = args.archive.expanduser()
        archive.parent.mkdir(parents=True, exist_ok=True)
        if not archive.exists():
            print("Downloading the upstream model bundle (approximately 240 MiB).")
            partial = archive.with_suffix(".part")
            urllib.request.urlretrieve(BUNDLE, partial)
            partial.replace(archive)
        # Extract exactly one regular file; never unpack arbitrary archive paths.
        with tarfile.open(archive, "r:gz") as bundle:
            member = bundle.getmember(MODEL)
            if not member.isfile() or member.size > 50_000_000:
                raise RuntimeError("Unexpected model archive entry")
            with bundle.extractfile(member) as src, model.with_suffix(".part").open("wb") as dst:
                shutil.copyfileobj(src, dst)
            model.with_suffix(".part").replace(model)
    digest = hashlib.sha256(model.read_bytes()).hexdigest()
    if digest != SHA256:
        raise RuntimeError(
            "Model SHA256 mismatch. Do not use this file with the bundled parser."
        )
    (directory / "model.sha256").write_text(f"{digest}  {MODEL}\n")
    library = Path(get_package_prefix("hand_detect")) / "lib/libhand_yolox_parser.so"
    if not library.is_file():
        raise RuntimeError("Build and source hand_detect before running setup_model.py")
    labels = directory / "labels.txt"
    labels.write_text("body\nhead\nhand\n")
    config = runtime / "config/infer.local.txt"
    config.write_text(f"""# Generated for the PINTO YOLOX-Nano body/head/hand model.
# The input is BGR float32 in [0,255], stretched to 320x320.
[property]
gpu-id=0
onnx-file={model}
model-engine-file={directory / 'hand_yolox_n_b1_fp16.engine'}
labelfile-path={labels}
custom-lib-path={library}
parse-bbox-func-name=NvDsInferParseHandYolox
net-scale-factor=1.0
model-color-format=1
network-mode=2
network-type=0
process-mode=1
batch-size=1
gie-unique-id=1
interval=0
num-detected-classes=3
maintain-aspect-ratio=0
cluster-mode=2

[class-attrs-all]
pre-cluster-threshold=0.25
nms-iou-threshold=0.45
topk=100
""")
    print(f"Model SHA256: {digest}")
    print(f"Inference configuration: {config}")
    if (directory / "hand_yolox_n_b1_fp16.engine").is_file():
        print("The existing FP16 engine is available at the relocated model path.")
    else:
        print("The FP16 engine will be built on this Jetson on first startup.")


if __name__ == "__main__":
    main()
