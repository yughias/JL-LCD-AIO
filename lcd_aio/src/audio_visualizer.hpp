#pragma once

#include <vector>
#include <cstdint>

constexpr int VISUALIZER_MAX_BARS = 64;

#ifdef __cplusplus
extern "C" {
#endif

bool audio_visualizer_init(void);
void audio_visualizer_shutdown(void);
void audio_visualizer_get_bars(int num_bars, float* out_bars, float* out_peaks, float dt);
bool audio_visualizer_is_active(void);
void audio_visualizer_set_active(bool active);

#ifdef __cplusplus
}
#endif

class AudioVisualizer {
public:
    AudioVisualizer();
    ~AudioVisualizer();

    bool Init();
    void Shutdown();
    void GetBars(int num_bars, float* out_bars, float* out_peaks, float dt);
    void SetActive(bool active);
    bool IsActive() const;

private:
    bool m_initialized = false;
};
