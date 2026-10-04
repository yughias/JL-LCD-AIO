#pragma once

#include <string>
#include <cstddef>

#ifdef __cplusplus
extern "C" {
#endif

bool video_cache_get_optimized_path(const char* input_path, char* out_cached_path, size_t max_len);
bool video_cache_needs_optimization(const char* input_path);
void video_cache_start_background(const char* input_path);
bool video_cache_poll_completed(char* out_cached_path, size_t max_len);
void video_cache_cancel(void);

#ifdef __cplusplus
}
#endif
