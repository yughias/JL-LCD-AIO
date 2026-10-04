#include "video_player.hpp"
#include "video_cropper.hpp"

#include <windows.h>
#include <cguid.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdint>

static const GUID GUID_IMF2DBuffer = 
    { 0x7dc9d5f9, 0xdedf, 0x4865, { 0x88, 0xe9, 0x7e, 0x64, 0x70, 0x3e, 0x95, 0x76 } };

struct video_player {
    IMFSourceReader* pReader = nullptr;
    int native_width = 0;
    int native_height = 0;
    int width = 0;
    int height = 0;
    LONG default_stride = 0;
    float fps = 30.0f;
    float frame_duration = 1.0f / 30.0f;
    float timer = 0.0f;
    uint8_t* pixel_buffer = nullptr; // 32-bit RGBA
    bool is_active = false;
    bool has_new_frame = false;
    int* lut_x = nullptr;
    int* lut_y = nullptr;
};
using video_player_t = struct video_player;

static bool file_exists_a(const char* path) {
    DWORD attr = GetFileAttributesA(path);
    return (attr != INVALID_FILE_ATTRIBUTES && !(attr & FILE_ATTRIBUTE_DIRECTORY));
}

static void read_one_frame(video_player_t* vp);

video_player_t* video_player_open(const char* preferred_filename) {
    char target_path[MAX_PATH] = {0};

    if (preferred_filename && file_exists_a(preferred_filename)) {
        strncpy(target_path, preferred_filename, sizeof(target_path) - 1);
    } else if (file_exists_a("video.mp4")) {
        strncpy(target_path, "video.mp4", sizeof(target_path) - 1);
    } else {
        return nullptr;
    }

    printf("[VideoPlayer] Found video: %s\n", target_path);

    WCHAR wpath[MAX_PATH] = {0};
    MultiByteToWideChar(CP_UTF8, 0, target_path, -1, wpath, MAX_PATH);

    MFStartup(MF_VERSION, MFSTARTUP_FULL);

    IMFAttributes* pAttr = nullptr;
    if (FAILED(MFCreateAttributes(&pAttr, 2))) return nullptr;
    pAttr->SetUINT32(MF_READWRITE_ENABLE_HARDWARE_TRANSFORMS, TRUE);
    pAttr->SetUINT32(MF_SOURCE_READER_ENABLE_VIDEO_PROCESSING, TRUE);

    IMFSourceReader* pReader = nullptr;
    HRESULT hr = MFCreateSourceReaderFromURL(wpath, pAttr, &pReader);
    pAttr->Release();
    if (FAILED(hr)) {
        printf("[VideoPlayer] MFCreateSourceReaderFromURL failed: 0x%08lX\n", (unsigned long)hr);
        return nullptr;
    }

    int native_w = 960, native_h = 480;
    float fps = 30.0f;
    IMFMediaType* pNative = nullptr;
    if (SUCCEEDED(pReader->GetNativeMediaType((DWORD)MF_SOURCE_READER_FIRST_VIDEO_STREAM, 0, &pNative))) {
        UINT64 frame_size = 0;
        if (SUCCEEDED(pNative->GetUINT64(MF_MT_FRAME_SIZE, &frame_size))) {
            native_w = (int)(frame_size >> 32);
            native_h = (int)(frame_size & 0xFFFFFFFF);
        }
        UINT64 frame_rate = 0;
        if (SUCCEEDED(pNative->GetUINT64(MF_MT_FRAME_RATE, &frame_rate))) {
            UINT32 num = (UINT32)(frame_rate >> 32);
            UINT32 den = (UINT32)(frame_rate & 0xFFFFFFFF);
            if (den > 0) fps = (float)num / (float)den;
        }
        pNative->Release();
        pNative = nullptr;
    }

    int out_w = native_w;
    int out_h = native_h;
    if (native_w > 960 || native_h > 480) {
        float sw = 960.0f / (float)native_w;
        float sh = 480.0f / (float)native_h;
        float s = (sw < sh) ? sw : sh;
        out_w = (int)(native_w * s);
        out_h = (int)(native_h * s);
    }
    out_w = (out_w + 1) & ~1;
    out_h = (out_h + 1) & ~1;
    if (out_w < 2) out_w = 2;
    if (out_h < 2) out_h = 2;

    IMFMediaType* pType = nullptr;
    MFCreateMediaType(&pType);
    pType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
    pType->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_RGB32);
    hr = pReader->SetCurrentMediaType((DWORD)MF_SOURCE_READER_FIRST_VIDEO_STREAM, nullptr, pType);
    pType->Release();
    if (FAILED(hr)) {
        printf("[VideoPlayer] SetCurrentMediaType RGB32 failed: 0x%08lX\n", (unsigned long)hr);
        pReader->Release();
        return nullptr;
    }

    IMFMediaType* pCur = nullptr;
    hr = pReader->GetCurrentMediaType((DWORD)MF_SOURCE_READER_FIRST_VIDEO_STREAM, &pCur);
    LONG def_stride = 0;
    if (SUCCEEDED(hr)) {
        UINT64 frame_size = 0;
        if (SUCCEEDED(pCur->GetUINT64(MF_MT_FRAME_SIZE, &frame_size))) {
            native_w = (int)(frame_size >> 32);
            native_h = (int)(frame_size & 0xFFFFFFFF);
            if (native_w <= 960 && native_h <= 480) {
                out_w = native_w;
                out_h = native_h;
            }
        }
        UINT32 st = 0;
        if (SUCCEEDED(pCur->GetUINT32(MF_MT_DEFAULT_STRIDE, &st))) {
            def_stride = (LONG)st;
        }
        pCur->Release();
    }

    auto* vp = (video_player_t*)malloc(sizeof(video_player_t));
    memset(vp, 0, sizeof(video_player_t));
    vp->pReader = pReader;
    vp->native_width = native_w;
    vp->native_height = native_h;
    vp->width = out_w;
    vp->height = out_h;
    vp->default_stride = def_stride;
    vp->fps = (fps > 1.0f && fps <= 120.0f) ? fps : 30.0f;
    vp->frame_duration = 1.0f / vp->fps;
    vp->timer = 0.0f;
    vp->pixel_buffer = (uint8_t*)malloc(vp->width * vp->height * 4);
    vp->is_active = true;
    vp->has_new_frame = false;

    vp->lut_x = (int*)malloc(vp->width * sizeof(int));
    for (int x = 0; x < vp->width; x++) {
        vp->lut_x[x] = (x * vp->native_width) / vp->width;
    }
    vp->lut_y = (int*)malloc(vp->height * sizeof(int));
    for (int y = 0; y < vp->height; y++) {
        vp->lut_y[y] = (y * vp->native_height) / vp->height;
    }

    printf("[VideoPlayer] Opened: %dx%d (native %dx%d) @ %.2f FPS\n",
           vp->width, vp->height, vp->native_width, vp->native_height, vp->fps);

    read_one_frame(vp);
    return vp;
}

static void read_one_frame(video_player_t* vp) {
    DWORD streamIndex = 0, flags = 0;
    LONGLONG ts = 0;
    IMFSample* pSample = nullptr;

    HRESULT hr = vp->pReader->ReadSample(
        (DWORD)MF_SOURCE_READER_FIRST_VIDEO_STREAM,
        0,
        &streamIndex,
        &flags,
        &ts,
        &pSample
    );

    if (flags & MF_SOURCE_READERF_ENDOFSTREAM) {
        PROPVARIANT var;
        PropVariantInit(&var);
        var.vt = VT_I8;
        var.hVal.QuadPart = 0;
        vp->pReader->SetCurrentPosition(GUID_NULL, var);
        PropVariantClear(&var);
        return;
    }

    if (SUCCEEDED(hr) && pSample) {
        IMFMediaBuffer* pBuffer = nullptr;
        if (SUCCEEDED(pSample->ConvertToContiguousBuffer(&pBuffer))) {
            IMF2DBuffer* p2D = nullptr;
            BYTE* scan0 = nullptr;
            LONG pitch = 0;
            HRESULT hr2d = pBuffer->QueryInterface(GUID_IMF2DBuffer, (void**)&p2D);

            if (SUCCEEDED(hr2d) && p2D && SUCCEEDED(p2D->Lock2D(&scan0, &pitch))) {
                uint32_t* dst_u32 = (uint32_t*)vp->pixel_buffer;
                int vw = vp->width;
                int vh = vp->height;
                const int* lx = vp->lut_x;
                const int* ly = vp->lut_y;

                for (int y = 0; y < vh; y++) {
                    const BYTE* row = scan0 + ((ptrdiff_t)ly[y] * (ptrdiff_t)pitch);
                    uint32_t* dst_row = dst_u32 + (y * vw);
                    for (int x = 0; x < vw; x++) {
                        int s_idx = lx[x] * 4;
                        uint32_t b = row[s_idx + 0];
                        uint32_t g = row[s_idx + 1];
                        uint32_t r = row[s_idx + 2];
                        dst_row[x] = 0xFF000000 | (b << 16) | (g << 8) | r;
                    }
                }
                vp->has_new_frame = true;
                p2D->Unlock2D();
                p2D->Release();
            } else {
                if (p2D) p2D->Release();

                BYTE* src = nullptr;
                DWORD cur_len = 0;
                if (SUCCEEDED(pBuffer->Lock(&src, nullptr, &cur_len))) {
                    LONG pitch = (vp->default_stride != 0 ? vp->default_stride : (vp->native_width * 4));
                    if (vp->native_height > 0 && cur_len >= (size_t)vp->native_height * (size_t)vp->native_width * 4) {
                        pitch = (LONG)(cur_len / (size_t)vp->native_height);
                    }

                    uint32_t* dst_u32 = (uint32_t*)vp->pixel_buffer;
                    int vw = vp->width;
                    int vh = vp->height;
                    const int* lx = vp->lut_x;
                    const int* ly = vp->lut_y;

                    for (int y = 0; y < vh; y++) {
                        const BYTE* src_row = src + ((ptrdiff_t)ly[y] * (ptrdiff_t)pitch);
                        uint32_t* dst_row = dst_u32 + (y * vw);
                        for (int x = 0; x < vw; x++) {
                            int s_idx = lx[x] * 4;
                            uint32_t b = src_row[s_idx + 0];
                            uint32_t g = src_row[s_idx + 1];
                            uint32_t r = src_row[s_idx + 2];
                            dst_row[x] = 0xFF000000 | (b << 16) | (g << 8) | r;
                        }
                    }
                    vp->has_new_frame = true;
                    pBuffer->Unlock();
                }
            }
            pBuffer->Release();
        }
        pSample->Release();
    }
}

bool video_player_update(video_player_t* vp, float delta_time) {
    if (!vp || !vp->is_active || !vp->pReader) return false;

    vp->has_new_frame = false;
    vp->timer += delta_time;

    if (vp->timer >= vp->frame_duration) {
        while (vp->timer >= vp->frame_duration) {
            vp->timer -= vp->frame_duration;
        }
        read_one_frame(vp);
    }

    return vp->has_new_frame;
}

const uint8_t* video_player_get_pixels(const video_player_t* vp, int* out_w, int* out_h) {
    if (!vp) return nullptr;
    if (out_w) *out_w = vp->width;
    if (out_h) *out_h = vp->height;
    return vp->pixel_buffer;
}

int video_player_get_native_width(const video_player_t* vp) {
    return vp ? vp->native_width : 0;
}

int video_player_get_native_height(const video_player_t* vp) {
    return vp ? vp->native_height : 0;
}

float video_player_get_fps(const video_player_t* vp) {
    return vp ? vp->fps : 0.0f;
}

bool video_player_is_active(const video_player_t* vp) {
    return vp && vp->is_active;
}

void video_player_close(video_player_t* vp) {
    if (!vp) return;
    if (vp->pReader) {
        vp->pReader->Release();
        vp->pReader = nullptr;
    }
    if (vp->pixel_buffer) {
        free(vp->pixel_buffer);
        vp->pixel_buffer = nullptr;
    }
    if (vp->lut_x) {
        free(vp->lut_x);
        vp->lut_x = nullptr;
    }
    if (vp->lut_y) {
        free(vp->lut_y);
        vp->lut_y = nullptr;
    }
    free(vp);
    MFShutdown();
}

VideoPlayer::VideoPlayer() = default;
VideoPlayer::~VideoPlayer() { Close(); }

bool VideoPlayer::Open(const std::string& path) {
    Close();
    m_handle = video_player_open(path.c_str());
    return m_handle != nullptr;
}

void VideoPlayer::Close() {
    if (m_handle) {
        video_player_close(m_handle);
        m_handle = nullptr;
    }
}

bool VideoPlayer::Update(float dt) {
    return m_handle ? video_player_update(m_handle, dt) : false;
}

const uint8_t* VideoPlayer::GetPixels(int* out_w, int* out_h) const {
    return m_handle ? video_player_get_pixels(m_handle, out_w, out_h) : nullptr;
}

int VideoPlayer::GetNativeWidth() const {
    return m_handle ? video_player_get_native_width(m_handle) : 0;
}

int VideoPlayer::GetNativeHeight() const {
    return m_handle ? video_player_get_native_height(m_handle) : 0;
}

float VideoPlayer::GetFps() const {
    return m_handle ? video_player_get_fps(m_handle) : 0.0f;
}

bool VideoPlayer::IsActive() const {
    return m_handle ? video_player_is_active(m_handle) : false;
}

bool VideoCache::GetOptimizedPath(const std::string& input_path, std::string& out_cached_path) {
    char buf[MAX_PATH] = {0};
    if (video_cache_get_optimized_path(input_path.c_str(), buf, sizeof(buf))) {
        out_cached_path = buf;
        return true;
    }
    return false;
}

bool VideoCache::NeedsOptimization(const std::string& input_path) {
    return video_cache_needs_optimization(input_path.c_str());
}

void VideoCache::StartBackground(const std::string& input_path) {
    video_cache_start_background(input_path.c_str());
}

bool VideoCache::PollCompleted(std::string& out_cached_path) {
    char buf[MAX_PATH] = {0};
    if (video_cache_poll_completed(buf, sizeof(buf))) {
        out_cached_path = buf;
        return true;
    }
    return false;
}

void VideoCache::Cancel() {
    video_cache_cancel();
}
