# Hand detection models

## Current TAO deployment (2026-10-22)

The configured Jetson now uses the user's **TAO EfficientDet-D0 run4 epoch 80**
export, replacing the public YOLOX model. Deployment files are under:

```text
~/kosen/runtime/hand_detect/tao/hand_efficientdet_d0_run4_e080_20261022/
```

Epoch 35 deployment files are removed from Jetson after switching to epoch 80.
The bundle contains `hand_efficientdet_d0_e080.onnx`, its own DeepStream parser,
labels, configuration template, and model provenance. ONNX SHA256:
`8bdc4fc2ec3a2186e8ad35d46fe314d1294494b06a0140d6c0010bf8cdf3b600`.

The external model input is RGB FP32 `[1,3,480,640]` in pixel range 0..255.
TAO normalization, resize/padding to the trained 640x384 canvas, anchors,
hand-channel selection, and box decoding are embedded in ONNX. The output is
`detections` `[1,46035,5]`, with xyxy coordinates in the external 640x480 canvas
and a hand probability. DeepStream performs NMS using the dedicated
`NvDsInferParseTaoHand` parser. The source model's training-mode BatchNorm
statistics are preserved using standard arithmetic operations.

Use **hand_class_id=0**, confidence 0.30, and a 640x480 pipeline canvas.
The active config remains `runtime/hand_detect/config/infer.local.txt`; the
private `camera.local.yaml` selects the matching class and threshold. Existing
camera credentials and ROI settings are preserved. The old YOLOX weights and
engine remain available, with previous private configs backed up under
`runtime/hand_detect/config/backups/`.

This model uses a different parser and preprocessing from YOLOX. Do not run
the legacy `setup_model.py` unless deliberately switching back: it writes a
YOLOX inference configuration. TensorRT engines must be built on the Jetson.
Packaging/deployment does not establish model accuracy or real-time throughput.

## Legacy public fallback

The initial detector is **YOLOX-Nano Body/Head/Hand**, trained and published by
Katsuya Hyodo (PINTO), not a TAO-trained model. It detects hands directly in
full-frame images; class IDs are body=0, head=1, hand=2. The source project lists
an Apache-2.0 license. Source and weights:

- [Model card, license, and demos](https://github.com/PINTO0309/PINTO_model_zoo/tree/main/426_YOLOX-Body-Head-Hand)
- [Author's download script](https://github.com/PINTO0309/PINTO_model_zoo/blob/main/426_YOLOX-Body-Head-Hand/download_n.sh)
- [ONNX bundle](https://s3.ap-northeast-2.wasabisys.com/pinto-model-zoo/426_YOLOX-Body-Head-Hand/resources_n.tar.gz)

`setup_model.py` extracts only
`yolox_n_body_head_hand_0461_0.4428_1x3x320x320.onnx`. It records a SHA256
and generates the deployment config outside Git. No trained weights or
hardware-specific TensorRT engines belong in version control. Deployment models
are stored under `runtime/hand_detect/models/`, the generated nvinfer config
under `runtime/hand_detect/config/`, and downloads under `runtime/hand_detect/cache/`.

Verified model SHA256:
`e69376d5b7baec2bf6073b568bbaa1da8db8068cafa6cc6f8ef92ec94ae8f21c`.
The setup script rejects a different file rather than silently using changed
upstream weights.

The parser is specific to this model's output contract. Do not use the
`post` variant, a quantized variant, or another YOLO model with this parser.
Preprocessing uses BGR, scale 1, and direct resize (no letterbox).
DeepStream applies NMS after the custom output parser.

## Earlier TAO pretrained-model research

TAO was the preferred source. The verified NVIDIA options did not provide a
drop-in, downloadable full-frame hand detector for this target:

- [GestureNet](https://catalog.ngc.nvidia.com/orgs/nvidia/tao/models/gesturenet)
  classifies gestures from **already-cropped hands**. It is not a hand detector.
- NVIDIA's [TLT/TAO hand detection example](https://github.com/NVIDIA-AI-IOT/gesture_recognition_tlt_deepstream)
  fine-tunes a detector on EgoHands before deploying it with DeepStream.
  The example is no longer maintained and targets older JetPack/DeepStream.
  Its training workflow is useful, but no compatible ready-to-use detector
  artifact was verified here.
- NVIDIA's [RT-DETR Hand Detector](https://huggingface.co/nvidia/RT-DETR-Hand-Detector-v1)
  has gated model files requiring account access and acceptance of its terms.
  It was not downloaded, and its model card does not establish TAO provenance
  or compatibility with this Jetson.

These findings are not a claim that no TAO hand detector exists anywhere.
The available ONNX detector lets the pipeline run now; a future TAO detector
can use the same ROS node and probe.

## Replacing it with your own model

1. Export a full-frame object detector compatible with the target's TensorRT.
2. Create its own nvinfer config with the correct normalization, color order,
   dimensions, labels, parser, and engine path. Do not reuse YOLOX preprocessing
   for a different model.
3. Set `network-type=0`, `process-mode=1`, `batch-size=1`, `interval=0`,
   `gie-unique-id=1`, and the actual `num-detected-classes`.
4. Supply `infer_config` and set `hand_class_id` for the new label map.
5. Validate known hand/no-hand images and real camera scenes before trusting
   the labels or threshold. Missing inference must remain UNKNOWN.

See [NVIDIA's nvinfer documentation](https://docs.nvidia.com/metropolis/deepstream/7.1/text/DS_plugin_gst-nvinfer.html)
for custom parsers and detector metadata. Classification or pose-only models
need a different interpretation and are not interchangeable with this detector.
