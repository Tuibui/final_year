#include <nvdsinfer_custom_impl.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>

// This parser accepts the PINTO YOLOX-Nano raw grid output [N, 8].
// Classes are body=0, head=1, hand=2. DeepStream applies class-wise NMS.
extern "C" bool NvDsInferParseHandYolox(
  const std::vector<NvDsInferLayerInfo> & layers,
  const NvDsInferNetworkInfo & network,
  const NvDsInferParseDetectionParams & parameters,
  std::vector<NvDsInferObjectDetectionInfo> & objects)
{
  if (layers.size() != 1 || layers[0].dataType != FLOAT || !layers[0].buffer ||
    parameters.numClassesConfigured != 3 || parameters.perClassPreclusterThreshold.size() < 3)
  {
    std::fprintf(stderr, "Hand YOLOX parser: expected one FP32 output and three classes\n");
    return false;
  }
  size_t expected = 0;
  for (unsigned stride : {8U, 16U, 32U}) {
    expected += (network.width / stride) * (network.height / stride);
  }
  if (layers[0].inferDims.numElements != expected * 8) {
    std::fprintf(stderr, "Hand YOLOX parser: unexpected output tensor size\n");
    return false;
  }
  const float * values = static_cast<const float *>(layers[0].buffer);
  size_t row = 0;
  for (unsigned stride : {8U, 16U, 32U}) {
    for (unsigned y = 0; y < network.height / stride; ++y) {
      for (unsigned x = 0; x < network.width / stride; ++x, ++row) {
        const float * p = values + row * 8;
        if (!std::all_of(p, p + 8, [](float v) {return std::isfinite(v);})) {continue;}
        unsigned label = 0;
        for (unsigned k = 1; k < 3; ++k) {
          if (p[5 + k] > p[5 + label]) {label = k;}
        }
        float score = p[4] * p[5 + label];
        if (score < parameters.perClassPreclusterThreshold[label] || score > 1.0F) {continue;}
        float cx = (p[0] + x) * stride;
        float cy = (p[1] + y) * stride;
        float w = std::exp(p[2]) * stride;
        float h = std::exp(p[3]) * stride;
        if (!std::isfinite(w) || !std::isfinite(h)) {continue;}
        float left = std::clamp(cx - w / 2, 0.0F, static_cast<float>(network.width));
        float top = std::clamp(cy - h / 2, 0.0F, static_cast<float>(network.height));
        float right = std::clamp(cx + w / 2, 0.0F, static_cast<float>(network.width));
        float bottom = std::clamp(cy + h / 2, 0.0F, static_cast<float>(network.height));
        if (right <= left || bottom <= top) {continue;}
        NvDsInferObjectDetectionInfo object{};
        object.classId = label;
        object.detectionConfidence = score;
        object.left = left;
        object.top = top;
        object.width = right - left;
        object.height = bottom - top;
        objects.push_back(object);
      }
    }
  }
  return true;
}
CHECK_CUSTOM_PARSE_FUNC_PROTOTYPE(NvDsInferParseHandYolox);
