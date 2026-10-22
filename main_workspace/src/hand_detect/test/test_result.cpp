#include <gtest/gtest.h>
#include <limits>
#include "hand_detect/result.hpp"

TEST(Inspection, ReportsTheExactFrameHandCount)
{
  EXPECT_EQ(hand_detect::classify(true, true, 0, false), "hand = 0");
  EXPECT_EQ(hand_detect::classify(true, true, 1, false), "hand = 1");
  EXPECT_EQ(hand_detect::classify(true, true, 2, false), "hand = 2");
  EXPECT_EQ(hand_detect::classify(true, true, 3, false), "hand = 3");
  EXPECT_EQ(hand_detect::classify(true, true, 10, false), "hand = 10");
}
TEST(Inspection, UnavailableDataDoesNotClaimZeroHands)
{
  EXPECT_EQ(hand_detect::classify(false, false, 0, false), "UNKNOWN");
  EXPECT_EQ(hand_detect::classify(true, false, 0, false), "UNKNOWN");
  EXPECT_EQ(hand_detect::classify(true, true, 1, true), "UNKNOWN");
}
TEST(Inspection, FilterClassesAndConfidence)
{
  EXPECT_TRUE(hand_detect::accepted_hand(2, 2, 0.5F, 0.5F));
  EXPECT_FALSE(hand_detect::accepted_hand(0, 2, 0.99F, 0.5F));
  EXPECT_FALSE(hand_detect::accepted_hand(2, 2, 0.49F, 0.5F));
  EXPECT_FALSE(hand_detect::accepted_hand(2, 2, -0.1F, 0.5F));
  EXPECT_FALSE(hand_detect::accepted_hand(
    2, 2, std::numeric_limits<float>::quiet_NaN(), 0.5F));
}
TEST(Latest, KeepsNewestWithoutBuildingABacklog)
{
  hand_detect::Latest<int> queue;
  EXPECT_FALSE(queue.take());
  EXPECT_TRUE(queue.put(1));
  EXPECT_TRUE(queue.put(2));
  EXPECT_EQ(queue.overwritten(), 1U);
  EXPECT_EQ(queue.take().value(), 2);
  EXPECT_FALSE(queue.take());
}

TEST(Inspection, TopRegionUsesBoundingBoxCenterAndExcludesBoundary)
{
  using hand_detect::center_in_top_region;
  EXPECT_TRUE(center_in_top_region(100, 100, 40, 40, 640, 480, 0.75));
  // The box crosses y=360, but its center is y=350 and still counts.
  EXPECT_TRUE(center_in_top_region(100, 330, 40, 40, 640, 480, 0.75));
  EXPECT_FALSE(center_in_top_region(100, 340, 40, 40, 640, 480, 0.75));
  EXPECT_FALSE(center_in_top_region(100, 400, 40, 40, 640, 480, 0.75));
  EXPECT_FALSE(center_in_top_region(620, 100, 40, 40, 640, 480, 0.75));
  EXPECT_FALSE(center_in_top_region(100, -50, 40, 40, 640, 480, 0.75));
  EXPECT_FALSE(center_in_top_region(100, 100, 0, 40, 640, 480, 0.75));
  EXPECT_FALSE(center_in_top_region(100, 100, 40, 40, 0, 480, 0.75));
  EXPECT_FALSE(center_in_top_region(100, 100, 40, 40, 640, 480, 0));
  EXPECT_FALSE(center_in_top_region(100, 100, 40, 40, 640, 480, 1.1));
  EXPECT_FALSE(center_in_top_region(100, 100, 40,
    std::numeric_limits<float>::quiet_NaN(), 640, 480, 0.75));
  EXPECT_TRUE(center_in_top_region(100, 400, 40, 40, 640, 480, 1.0));
}
