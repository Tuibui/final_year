#pragma once

#include <chrono>
#include <cmath>
#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <utility>

namespace hand_detect
{
inline std::string classify(bool enabled, bool inferred, unsigned hands, bool stale)
{
  if (!enabled || !inferred || stale) {return "UNKNOWN";}
  return "hand = " + std::to_string(hands);
}

inline bool accepted_hand(int class_id, int expected_id, float confidence, float threshold)
{
  return class_id == expected_id && std::isfinite(confidence) &&
         confidence >= threshold && confidence <= 1.0F;
}

// Use a half-open ROI: centers on the bottom or right boundary are outside.
inline bool center_in_top_region(
  float left, float top, float width, float height,
  float frame_width, float frame_height, double top_fraction)
{
  if (!std::isfinite(left) || !std::isfinite(top) || !std::isfinite(width) ||
    !std::isfinite(height) || !std::isfinite(frame_width) || !std::isfinite(frame_height) ||
    !std::isfinite(top_fraction) || width <= 0 || height <= 0 ||
    frame_width <= 0 || frame_height <= 0 || top_fraction <= 0 || top_fraction > 1)
  {
    return false;
  }
  const double x = static_cast<double>(left) + static_cast<double>(width) / 2;
  const double y = static_cast<double>(top) + static_cast<double>(height) / 2;
  return x >= 0 && x < frame_width && y >= 0 && y < frame_height * top_fraction;
}

// A single-slot mailbox bounds memory and discards superseded results.
// The producer never waits for the consumer's mutex.
template<typename T>
class Latest
{
public:
  bool put(T value)
  {
    std::unique_lock<std::mutex> lock(mutex_, std::try_to_lock);
    if (!lock.owns_lock()) {return false;}
    if (value_) {++overwritten_;}
    value_ = std::move(value);
    return true;
  }
  std::optional<T> take()
  {
    std::lock_guard<std::mutex> lock(mutex_);
    auto result = std::move(value_);
    value_.reset();
    return result;
  }
  uint64_t overwritten()
  {
    std::lock_guard<std::mutex> lock(mutex_);
    return overwritten_;
  }
private:
  std::mutex mutex_;
  std::optional<T> value_;
  uint64_t overwritten_{0};
};
}  // namespace hand_detect
