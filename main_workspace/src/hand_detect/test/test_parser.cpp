#include <gtest/gtest.h>
#include <nvdsinfer_custom_impl.h>
#include <cmath>
#include <limits>
#include <vector>

extern "C" bool NvDsInferParseHandYolox(
  const std::vector<NvDsInferLayerInfo> &,
  const NvDsInferNetworkInfo &,
  const NvDsInferParseDetectionParams &,
  std::vector<NvDsInferObjectDetectionInfo> &);

TEST(Parser, DecodesHandGridAndConfidence)
{
  std::vector<float> data(2100 * 8, 0.0F);
  // Row zero, stride 8: center=(16,16), size=(16,16).
  data[0] = 2.0F;
  data[1] = 2.0F;
  data[2] = std::log(2.0F);
  data[3] = std::log(2.0F);
  data[4] = 0.8F;
  data[7] = 0.9F;
  NvDsInferLayerInfo layer{};
  layer.dataType = FLOAT;
  layer.buffer = data.data();
  layer.inferDims.numElements = data.size();
  NvDsInferNetworkInfo network{320, 320, 3};
  NvDsInferParseDetectionParams parameters{};
  parameters.numClassesConfigured = 3;
  parameters.perClassPreclusterThreshold = {0.25F, 0.25F, 0.25F};
  std::vector<NvDsInferObjectDetectionInfo> objects;
  ASSERT_TRUE(NvDsInferParseHandYolox({layer}, network, parameters, objects));
  ASSERT_EQ(objects.size(), 1U);
  EXPECT_EQ(objects[0].classId, 2U);
  EXPECT_NEAR(objects[0].detectionConfidence, 0.72F, 1e-5F);
  EXPECT_NEAR(objects[0].left, 8.0F, 1e-5F);
  EXPECT_NEAR(objects[0].width, 16.0F, 1e-5F);
  data[7] = std::numeric_limits<float>::quiet_NaN();
  objects.clear();
  EXPECT_TRUE(NvDsInferParseHandYolox({layer}, network, parameters, objects));
  EXPECT_TRUE(objects.empty());
  layer.inferDims.numElements = 10;
  EXPECT_FALSE(NvDsInferParseHandYolox({layer}, network, parameters, objects));
}
