# Stamp is the host ROS time at the post-inference probe, not camera exposure time.
std_msgs/Header header
uint64 frame_number
uint64 stream_pts_ns
bool pts_valid
bool inference_done
# Valid frames use "hand = N", where N is the ROI hand count in this frame.
# UNKNOWN means disabled inference, missing metadata, stale stream, or an error.
string status
string reason
# Counts and confidence include only hands accepted inside the ROI.
uint32 hand_count
float32 max_confidence
float64 probe_us
float64 queue_delay_ms
# Age from the input queue to the probe; excludes camera/network/decode delay.
float64 pipeline_age_ms
uint64 overwritten_results
