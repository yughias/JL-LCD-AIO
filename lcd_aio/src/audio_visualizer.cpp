#include "audio_visualizer.hpp"

#include <windows.h>
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <cmath>
#include <vector>
#include <algorithm>
#include <cstdint>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace {

static const GUID MY_KSDATAFORMAT_SUBTYPE_IEEE_FLOAT = 
    { 0x00000003, 0x0000, 0x0010, { 0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71 } };

const int FFT_SIZE = 512;
const int RING_SIZE = 4096;

// Ring buffer for captured audio samples
float s_ring_buffer[RING_SIZE] = {0};
int s_ring_write_idx = 0;
CRITICAL_SECTION s_ring_cs;
bool s_cs_initialized = false;

// Worker thread control
HANDLE s_hThread = NULL;
HANDLE s_hStopEvent = NULL;
bool s_running = false;
bool s_has_active_audio = false;
bool s_capture_enabled = true;

// WASAPI COM pointers
IMMDeviceEnumerator* s_pEnumerator = NULL;
IMMDevice* s_pDevice = NULL;
IAudioClient* s_pAudioClient = NULL;
IAudioCaptureClient* s_pCaptureClient = NULL;
WAVEFORMATEX* s_pwfx = NULL;

// Bar physics state
float s_bars[VISUALIZER_MAX_BARS] = {0};
float s_peaks[VISUALIZER_MAX_BARS] = {0};
float s_fall_vel[VISUALIZER_MAX_BARS] = {0};
float s_peak_vel[VISUALIZER_MAX_BARS] = {0};
float s_peak_energy = 0.05f;
float s_silence_timer = 0.0f;

// Radix-2 in-place Cooley-Tukey FFT
void Fft(float* real, float* imag, int n) {
    // Bit reversal
    int j = 0;
    for (int i = 0; i < n - 1; ++i) {
        if (i < j) {
            std::swap(real[i], real[j]);
            std::swap(imag[i], imag[j]);
        }
        int k = n >> 1;
        while (k <= j) {
            j -= k;
            k >>= 1;
        }
        j += k;
    }

    // Butterfly stages
    for (int len = 2; len <= n; len <<= 1) {
        float angle = -2.0f * (float)M_PI / (float)len;
        float wlen_re = cosf(angle);
        float wlen_im = sinf(angle);
        int half = len >> 1;

        for (int i = 0; i < n; i += len) {
            float w_re = 1.0f;
            float w_im = 0.0f;
            for (int k = 0; k < half; ++k) {
                float u_re = real[i + k];
                float u_im = imag[i + k];
                float v_re = real[i + k + half] * w_re - imag[i + k + half] * w_im;
                float v_im = real[i + k + half] * w_im + imag[i + k + half] * w_re;

                real[i + k] = u_re + v_re;
                imag[i + k] = u_im + v_im;
                real[i + k + half] = u_re - v_re;
                imag[i + k + half] = u_im - v_im;

                float next_w_re = w_re * wlen_re - w_im * wlen_im;
                float next_w_im = w_re * wlen_im + w_im * wlen_re;
                w_re = next_w_re;
                w_im = next_w_im;
            }
        }
    }
}

void PushSamples(const float* samples, int count) {
    EnterCriticalSection(&s_ring_cs);
    for (int i = 0; i < count; ++i) {
        s_ring_buffer[s_ring_write_idx] = samples[i];
        s_ring_write_idx = (s_ring_write_idx + 1) % RING_SIZE;
    }
    LeaveCriticalSection(&s_ring_cs);
}

void PullSamples(float* dst, int count) {
    EnterCriticalSection(&s_ring_cs);
    int read_idx = (s_ring_write_idx - count + RING_SIZE) % RING_SIZE;
    for (int i = 0; i < count; ++i) {
        dst[i] = s_ring_buffer[read_idx];
        read_idx = (read_idx + 1) % RING_SIZE;
    }
    LeaveCriticalSection(&s_ring_cs);
}

bool InitWasapiLoopback() {
    HRESULT hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), NULL, CLSCTX_ALL,
                                  __uuidof(IMMDeviceEnumerator), (void**)&s_pEnumerator);
    if (FAILED(hr) || !s_pEnumerator) return false;

    hr = s_pEnumerator->GetDefaultAudioEndpoint(eRender, eConsole, &s_pDevice);
    if (FAILED(hr) || !s_pDevice) return false;

    hr = s_pDevice->Activate(__uuidof(IAudioClient), CLSCTX_ALL, NULL, (void**)&s_pAudioClient);
    if (FAILED(hr) || !s_pAudioClient) return false;

    hr = s_pAudioClient->GetMixFormat(&s_pwfx);
    if (FAILED(hr) || !s_pwfx) return false;

    // Initialize loopback capture on default rendering endpoint
    hr = s_pAudioClient->Initialize(
        AUDCLNT_SHAREMODE_SHARED,
        AUDCLNT_STREAMFLAGS_LOOPBACK,
        1000000, // 100ms buffer
        0,
        s_pwfx,
        NULL
    );
    if (FAILED(hr)) return false;

    hr = s_pAudioClient->GetService(__uuidof(IAudioCaptureClient), (void**)&s_pCaptureClient);
    if (FAILED(hr) || !s_pCaptureClient) return false;

    hr = s_pAudioClient->Start();
    return SUCCEEDED(hr);
}

void CleanupWasapi() {
    if (s_pAudioClient) {
        s_pAudioClient->Stop();
    }
    if (s_pCaptureClient) {
        s_pCaptureClient->Release();
        s_pCaptureClient = NULL;
    }
    if (s_pAudioClient) {
        s_pAudioClient->Release();
        s_pAudioClient = NULL;
    }
    if (s_pwfx) {
        CoTaskMemFree(s_pwfx);
        s_pwfx = NULL;
    }
    if (s_pDevice) {
        s_pDevice->Release();
        s_pDevice = NULL;
    }
    if (s_pEnumerator) {
        s_pEnumerator->Release();
        s_pEnumerator = NULL;
    }
}

DWORD WINAPI AudioCaptureThread(LPVOID param) {
    (void)param;
    CoInitializeEx(NULL, COINIT_MULTITHREADED);

    bool initialized = InitWasapiLoopback();

    std::vector<float> mono_buf;
    mono_buf.reserve(2048);

    while (WaitForSingleObject(s_hStopEvent, 16) == WAIT_TIMEOUT) {
        if (!s_capture_enabled) {
            WaitForSingleObject(s_hStopEvent, 100);
            continue;
        }

        if (!initialized) {
            // Retry initializing in case audio device becomes available
            CleanupWasapi();
            initialized = InitWasapiLoopback();
            if (!initialized) {
                Sleep(500);
                continue;
            }
        }

        UINT32 packet_length = 0;
        HRESULT hr = s_pCaptureClient->GetNextPacketSize(&packet_length);
        if (FAILED(hr)) {
            initialized = false;
            continue;
        }

        while (packet_length > 0) {
            BYTE* pData = NULL;
            UINT32 numFramesRead = 0;
            DWORD flags = 0;

            hr = s_pCaptureClient->GetBuffer(&pData, &numFramesRead, &flags, NULL, NULL);
            if (FAILED(hr)) {
                initialized = false;
                break;
            }

            if (numFramesRead > 0) {
                mono_buf.resize(numFramesRead);

                if (flags & AUDCLNT_BUFFERFLAGS_SILENT) {
                    std::fill(mono_buf.begin(), mono_buf.end(), 0.0f);
                } else if (pData) {
                    int channels = s_pwfx->nChannels;
                    bool is_float = (s_pwfx->wFormatTag == WAVE_FORMAT_IEEE_FLOAT) ||
                                    (s_pwfx->wFormatTag == WAVE_FORMAT_EXTENSIBLE &&
                                     reinterpret_cast<WAVEFORMATEXTENSIBLE*>(s_pwfx)->SubFormat == MY_KSDATAFORMAT_SUBTYPE_IEEE_FLOAT);

                    if (is_float) {
                        const float* fData = reinterpret_cast<const float*>(pData);
                        for (UINT32 i = 0; i < numFramesRead; ++i) {
                            float sum = 0.0f;
                            for (int c = 0; c < channels; ++c) {
                                sum += fData[i * channels + c];
                            }
                            mono_buf[i] = sum / (float)channels;
                        }
                    } else if (s_pwfx->wBitsPerSample == 16) {
                        const int16_t* iData = reinterpret_cast<const int16_t*>(pData);
                        for (UINT32 i = 0; i < numFramesRead; ++i) {
                            float sum = 0.0f;
                            for (int c = 0; c < channels; ++c) {
                                sum += (float)iData[i * channels + c] / 32768.0f;
                            }
                            mono_buf[i] = sum / (float)channels;
                        }
                    }
                }

                PushSamples(mono_buf.data(), (int)numFramesRead);
            }

            s_pCaptureClient->ReleaseBuffer(numFramesRead);
            hr = s_pCaptureClient->GetNextPacketSize(&packet_length);
            if (FAILED(hr)) break;
        }
    }

    CleanupWasapi();
    CoUninitialize();
    return 0;
}

} // namespace

bool audio_visualizer_init(void) {
    if (s_running) return true;

    if (!s_cs_initialized) {
        InitializeCriticalSection(&s_ring_cs);
        s_cs_initialized = true;
    }

    s_hStopEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
    s_running = true;
    s_hThread = CreateThread(NULL, 0, AudioCaptureThread, NULL, 0, NULL);

    return (s_hThread != NULL);
}

void audio_visualizer_shutdown(void) {
    if (!s_running) return;
    s_running = false;

    if (s_hStopEvent) {
        SetEvent(s_hStopEvent);
    }
    if (s_hThread) {
        WaitForSingleObject(s_hThread, 1000);
        CloseHandle(s_hThread);
        s_hThread = NULL;
    }
    if (s_hStopEvent) {
        CloseHandle(s_hStopEvent);
        s_hStopEvent = NULL;
    }
    if (s_cs_initialized) {
        DeleteCriticalSection(&s_ring_cs);
        s_cs_initialized = false;
    }
}

void audio_visualizer_get_bars(int num_bars, float* out_bars, float* out_peaks, float dt) {
    if (num_bars <= 0 || !out_bars) return;
    if (num_bars > VISUALIZER_MAX_BARS) num_bars = VISUALIZER_MAX_BARS;

    float real[FFT_SIZE];
    float imag[FFT_SIZE] = {0};

    PullSamples(real, FFT_SIZE);

    // Apply Hann window and measure root-mean-square energy
    float rms = 0.0f;
    for (int i = 0; i < FFT_SIZE; ++i) {
        rms += real[i] * real[i];
        float w = 0.5f * (1.0f - cosf(2.0f * (float)M_PI * (float)i / (float)(FFT_SIZE - 1)));
        real[i] *= w;
    }
    rms = sqrtf(rms / (float)FFT_SIZE);

    bool active = (rms > 0.002f);
    s_has_active_audio = active;

    if (active) {
        s_silence_timer = 0.0f;
    } else {
        s_silence_timer += dt;
    }

    // Run FFT
    Fft(real, imag, FFT_SIZE);

    // Compute magnitudes for first half (positive spectrum up to Nyquist)
    const int half_fft = FFT_SIZE / 2; // 256 bins
    float mag[half_fft];
    float max_mag_in_frame = 0.001f;

    for (int i = 0; i < half_fft; ++i) {
        mag[i] = sqrtf(real[i] * real[i] + imag[i] * imag[i]);
        if (mag[i] > max_mag_in_frame) max_mag_in_frame = mag[i];
    }

    // Dynamic AGC (Auto Gain Control) to keep bars lively across volume levels
    if (max_mag_in_frame > s_peak_energy) {
        s_peak_energy = s_peak_energy * 0.7f + max_mag_in_frame * 0.3f;
    } else {
        s_peak_energy = (std::max)(0.08f, s_peak_energy * 0.995f);
    }

    // CAVA logarithmic frequency band grouping (from ~45 Hz to ~14500 Hz)
    float sample_rate = (s_pwfx && s_pwfx->nSamplesPerSec > 0) ? (float)s_pwfx->nSamplesPerSec : 48000.0f;
    float bin_hz = (sample_rate / 2.0f) / (float)half_fft;

    float min_freq = 45.0f;
    float max_freq = 14500.0f;

    for (int b = 0; b < num_bars; ++b) {
        float f_start = min_freq * powf(max_freq / min_freq, (float)b / (float)num_bars);
        float f_end   = min_freq * powf(max_freq / min_freq, (float)(b + 1) / (float)num_bars);

        int bin_start = (int)(f_start / bin_hz);
        int bin_end   = (int)(f_end / bin_hz);

        if (bin_start < 1) bin_start = 1;
        if (bin_end <= bin_start) bin_end = bin_start + 1;
        if (bin_end > half_fft) bin_end = half_fft;

        float band_max = 0.0f;
        for (int i = bin_start; i < bin_end; ++i) {
            if (mag[i] > band_max) band_max = mag[i];
        }

        // Apply pink noise high-frequency tilt (+3 dB/octave) so treble frequencies aren't dwarfed by bass
        float tilt = 1.0f + 2.8f * ((float)b / (float)num_bars);
        float target = (band_max / s_peak_energy) * tilt;

        // Soft compression curve (smooth logarithmic response like human hearing)
        target = logf(1.0f + target * 4.0f) / logf(5.0f);
        if (target < 0.0f) target = 0.0f;
        if (target > 1.0f) target = 1.0f;

        // CAVA Physics: Fast attack, realistic gravity falloff
        if (target > s_bars[b]) {
            // Instant punchy attack
            s_bars[b] = s_bars[b] * 0.25f + target * 0.75f;
            s_fall_vel[b] = 0.0f;
        } else {
            // Gravity acceleration on fall
            s_fall_vel[b] += 3.5f * dt;
            s_bars[b] -= s_fall_vel[b] * dt;
            if (s_bars[b] < 0.0f) {
                s_bars[b] = 0.0f;
                s_fall_vel[b] = 0.0f;
            }
        }

        // CAVA Floating peak dot physics
        if (s_bars[b] >= s_peaks[b]) {
            s_peaks[b] = s_bars[b];
            s_peak_vel[b] = 0.0f;
        } else {
            s_peak_vel[b] += 2.2f * dt;
            s_peaks[b] -= s_peak_vel[b] * dt;
            if (s_peaks[b] < 0.0f) {
                s_peaks[b] = 0.0f;
                s_peak_vel[b] = 0.0f;
            }
        }

        // Gentle idle wave when silence persists > 2.0s
        if (s_silence_timer > 2.0f) {
            float wave_time = (float)GetTickCount() * 0.003f;
            float idle_h = 0.04f + 0.03f * sinf(wave_time + (float)b * 0.35f);
            s_bars[b] = (std::max)(s_bars[b], idle_h);
            s_peaks[b] = (std::max)(s_peaks[b], s_bars[b]);
        }

        out_bars[b] = s_bars[b];
        if (out_peaks) {
            out_peaks[b] = s_peaks[b];
        }
    }
}

bool audio_visualizer_is_active(void) {
    return s_has_active_audio;
}

void audio_visualizer_set_active(bool active) {
    s_capture_enabled = active;
}

AudioVisualizer::AudioVisualizer() = default;

AudioVisualizer::~AudioVisualizer() {
    Shutdown();
}

bool AudioVisualizer::Init() {
    if (m_initialized) return true;
    m_initialized = audio_visualizer_init();
    return m_initialized;
}

void AudioVisualizer::Shutdown() {
    if (m_initialized) {
        audio_visualizer_shutdown();
        m_initialized = false;
    }
}

void AudioVisualizer::GetBars(int num_bars, float* out_bars, float* out_peaks, float dt) {
    audio_visualizer_get_bars(num_bars, out_bars, out_peaks, dt);
}

void AudioVisualizer::SetActive(bool active) {
    audio_visualizer_set_active(active);
}

bool AudioVisualizer::IsActive() const {
    return audio_visualizer_is_active();
}
