// include/deepstream_ros2/deepstream_pipeline.h
#ifndef DEEPSTREAM_PIPELINE_H
#define DEEPSTREAM_PIPELINE_H

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*FrameCallback)(int frame_id);

void start_deepstream_pipeline(FrameCallback cb);

// --- ADD THIS LINE ---
void stop_deepstream_pipeline(); 
// --------------------

#ifdef __cplusplus
}
#endif

#endif
