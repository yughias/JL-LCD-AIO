#include "video_cropper.hpp"
#include <windows.h>
#include <cguid.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

static const GUID GUID_IMF2DBuffer = 
    { 0x7dc9d5f9, 0xdedf, 0x4865, { 0x88, 0xe9, 0x7e, 0x64, 0x70, 0x3e, 0x95, 0x76 } };

static HANDLE s_thread = nullptr;
static volatile bool s_cancel = false;
static volatile bool s_completed = false;
static char s_completed_path[MAX_PATH] = {0};
static char s_current_input[MAX_PATH] = {0};
static CRITICAL_SECTION s_cs;
static bool s_cs_init = false;

static void ensure_cs(void) {
    if (!s_cs_init) {
        InitializeCriticalSection(&s_cs);
        s_cs_init = true;
    }
}

static void get_cache_dir(char* out_dir, size_t max_len) {
    char exe_path[MAX_PATH] = {0};
    DWORD len = GetModuleFileNameA(nullptr, exe_path, MAX_PATH);
    if (len > 0 && len < MAX_PATH) {
        char* last_slash = strrchr(exe_path, '\\');
        char* last_fwd = strrchr(exe_path, '/');
        if (last_fwd && (!last_slash || last_fwd > last_slash)) {
            last_slash = last_fwd;
        }
        if (last_slash) {
            *last_slash = '\0';
            snprintf(out_dir, max_len, "%s\\cache", exe_path);
            return;
        }
    }
    snprintf(out_dir, max_len, "cache");
}

static void get_cache_file_path(const char* filename, char* out_path, size_t max_len) {
    char cache_dir[MAX_PATH] = {0};
    get_cache_dir(cache_dir, sizeof(cache_dir));
    snprintf(out_path, max_len, "%s\\%s", cache_dir, filename);
}

static void get_cache_slug(const char* input_path, char* out_slug, size_t max_len) {
    const char* base = strrchr(input_path, '\\');
    if (!base) base = strrchr(input_path, '/');
    base = base ? base + 1 : input_path;

    WIN32_FILE_ATTRIBUTE_DATA fad;
    DWORD sz = 0, time_low = 0;
    if (GetFileAttributesExA(input_path, GetFileExInfoStandard, &fad)) {
        sz = fad.nFileSizeLow;
        time_low = fad.ftLastWriteTime.dwLowDateTime;
    }

    uint32_t hash = 2166136261u;
    for (const char* p = input_path; *p; p++) {
        hash ^= (uint8_t)(*p);
        hash *= 16777619u;
    }

    snprintf(out_slug, max_len, "bg_opt_%s_%08X_%08X_%08X.mp4", base, (unsigned int)sz, (unsigned int)time_low, hash);
}

bool video_cache_get_optimized_path(const char* input_path, char* out_cached_path, size_t max_len) {
    if (!input_path || !input_path[0]) return false;

    char slug[MAX_PATH] = {0};
    get_cache_slug(input_path, slug, sizeof(slug));

    char full_cache[MAX_PATH] = {0};
    get_cache_file_path(slug, full_cache, sizeof(full_cache));

    DWORD attr = GetFileAttributesA(full_cache);
    if (attr != INVALID_FILE_ATTRIBUTES && !(attr & FILE_ATTRIBUTE_DIRECTORY)) {
        if (out_cached_path) {
            strncpy(out_cached_path, full_cache, max_len - 1);
        }
        return true;
    }
    return false;
}

bool video_cache_needs_optimization(const char* input_path) {
    if (!input_path || !input_path[0]) return false;

    char slug[MAX_PATH] = {0};
    get_cache_slug(input_path, slug, sizeof(slug));
    char full_cache[MAX_PATH] = {0};
    get_cache_file_path(slug, full_cache, sizeof(full_cache));
    if (GetFileAttributesA(full_cache) != INVALID_FILE_ATTRIBUTES) {
        return false;
    }

    WCHAR wpath[MAX_PATH] = {0};
    MultiByteToWideChar(CP_UTF8, 0, input_path, -1, wpath, MAX_PATH);

    MFStartup(MF_VERSION, MFSTARTUP_FULL);

    IMFSourceReader* pReader = nullptr;
    HRESULT hr = MFCreateSourceReaderFromURL(wpath, nullptr, &pReader);
    if (FAILED(hr)) {
        MFShutdown();
        return false;
    }

    int native_w = 0, native_h = 0;
    float fps = 30.0f;

    IMFMediaType* pCur = nullptr;
    if (SUCCEEDED(pReader->GetNativeMediaType((DWORD)MF_SOURCE_READER_FIRST_VIDEO_STREAM, 0, &pCur))) {
        UINT64 frame_size = 0, frame_rate = 0;
        if (SUCCEEDED(pCur->GetUINT64(MF_MT_FRAME_SIZE, &frame_size))) {
            native_w = (int)(frame_size >> 32);
            native_h = (int)(frame_size & 0xFFFFFFFF);
        }
        if (SUCCEEDED(pCur->GetUINT64(MF_MT_FRAME_RATE, &frame_rate))) {
            UINT32 num = (UINT32)(frame_rate >> 32);
            UINT32 den = (UINT32)(frame_rate & 0xFFFFFFFF);
            if (den > 0) fps = (float)num / (float)den;
        }
        pCur->Release();
    }
    pReader->Release();
    MFShutdown();

    if (native_w <= 960 && native_h <= 480 && fps <= 30.5f) {
        return false;
    }
    return true;
}

static DWORD WINAPI transcode_thread_proc(LPVOID lpParam) {
    char* in_path = (char*)lpParam;
    if (!in_path) return 0;

    char cache_dir[MAX_PATH] = {0};
    get_cache_dir(cache_dir, sizeof(cache_dir));
    CreateDirectoryA(cache_dir, nullptr);

    char slug[MAX_PATH] = {0};
    get_cache_slug(in_path, slug, sizeof(slug));

    char final_out[MAX_PATH] = {0};
    get_cache_file_path(slug, final_out, sizeof(final_out));

    char temp_out[MAX_PATH] = {0};
    char temp_slug[MAX_PATH] = {0};
    snprintf(temp_slug, sizeof(temp_slug), "tmp_%u_%s", (unsigned int)GetTickCount(), slug);
    get_cache_file_path(temp_slug, temp_out, sizeof(temp_out));

    printf("[VideoCache] Starting background optimization: '%s' -> '%s' (960x480 @ 30 FPS)\n", in_path, final_out);

    WCHAR win_path[MAX_PATH] = {0};
    MultiByteToWideChar(CP_UTF8, 0, in_path, -1, win_path, MAX_PATH);

    WCHAR wtemp_path[MAX_PATH] = {0};
    MultiByteToWideChar(CP_UTF8, 0, temp_out, -1, wtemp_path, MAX_PATH);

    MFStartup(MF_VERSION, MFSTARTUP_FULL);

    IMFAttributes* pReaderAttr = nullptr;
    MFCreateAttributes(&pReaderAttr, 2);
    pReaderAttr->SetUINT32(MF_READWRITE_ENABLE_HARDWARE_TRANSFORMS, TRUE);
    pReaderAttr->SetUINT32(MF_SOURCE_READER_ENABLE_VIDEO_PROCESSING, TRUE);

    IMFSourceReader* pReader = nullptr;
    HRESULT hr = MFCreateSourceReaderFromURL(win_path, pReaderAttr, &pReader);
    pReaderAttr->Release();
    if (FAILED(hr)) {
        printf("[VideoCache] Failed to open source video reader: 0x%08lX\n", (unsigned long)hr);
        free(in_path);
        MFShutdown();
        return 0;
    }

    int src_w = 960, src_h = 480;
    IMFMediaType* pCur = nullptr;
    if (SUCCEEDED(pReader->GetCurrentMediaType((DWORD)MF_SOURCE_READER_FIRST_VIDEO_STREAM, &pCur))) {
        UINT64 frame_size = 0;
        if (SUCCEEDED(pCur->GetUINT64(MF_MT_FRAME_SIZE, &frame_size))) {
            src_w = (int)(frame_size >> 32);
            src_h = (int)(frame_size & 0xFFFFFFFF);
        }
        pCur->Release();
    }

    int target_w = src_w;
    int target_h = src_h;
    if (src_w > 960 || src_h > 480) {
        float sw = 960.0f / (float)src_w;
        float sh = 480.0f / (float)src_h;
        float s = (sw < sh) ? sw : sh;
        target_w = (int)(src_w * s);
        target_h = (int)(src_h * s);
    }
    target_w = (target_w + 1) & ~1;
    target_h = (target_h + 1) & ~1;
    if (target_w < 2) target_w = 2;
    if (target_h < 2) target_h = 2;
    int out_fps = 30;

    IMFMediaType* pRgbType = nullptr;
    MFCreateMediaType(&pRgbType);
    pRgbType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
    pRgbType->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_RGB32);
    hr = pReader->SetCurrentMediaType((DWORD)MF_SOURCE_READER_FIRST_VIDEO_STREAM, nullptr, pRgbType);
    pRgbType->Release();
    if (FAILED(hr)) {
        pReader->Release();
        free(in_path);
        MFShutdown();
        return 0;
    }

    LONG def_stride = 0;
    IMFMediaType* pCurOut = nullptr;
    if (SUCCEEDED(pReader->GetCurrentMediaType((DWORD)MF_SOURCE_READER_FIRST_VIDEO_STREAM, &pCurOut))) {
        UINT32 st = 0;
        if (SUCCEEDED(pCurOut->GetUINT32(MF_MT_DEFAULT_STRIDE, &st))) {
            def_stride = (LONG)st;
        }
        pCurOut->Release();
    }

    IMFSinkWriter* pWriter = nullptr;
    IMFAttributes* pWAttr = nullptr;
    MFCreateAttributes(&pWAttr, 1);
    pWAttr->SetUINT32(MF_READWRITE_ENABLE_HARDWARE_TRANSFORMS, TRUE);
    hr = MFCreateSinkWriterFromURL(wtemp_path, nullptr, pWAttr, &pWriter);
    pWAttr->Release();
    if (FAILED(hr)) {
        pReader->Release();
        free(in_path);
        MFShutdown();
        return 0;
    }

    UINT64 out_rate = ((UINT64)out_fps << 32) | 1;
    IMFMediaType* pOutType = nullptr;
    MFCreateMediaType(&pOutType);
    pOutType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
    pOutType->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_H264);
    pOutType->SetUINT32(MF_MT_AVG_BITRATE, 4000000);
    pOutType->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive);
    pOutType->SetUINT64(MF_MT_FRAME_SIZE, ((UINT64)target_w << 32) | target_h);
    pOutType->SetUINT64(MF_MT_FRAME_RATE, out_rate);
    pOutType->SetUINT64(MF_MT_PIXEL_ASPECT_RATIO, ((UINT64)1 << 32) | 1);

    DWORD sIndex = 0;
    pWriter->AddStream(pOutType, &sIndex);
    pOutType->Release();

    IMFMediaType* pInType = nullptr;
    MFCreateMediaType(&pInType);
    pInType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
    pInType->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_RGB32);
    pInType->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive);
    pInType->SetUINT64(MF_MT_FRAME_SIZE, ((UINT64)target_w << 32) | target_h);
    pInType->SetUINT64(MF_MT_FRAME_RATE, out_rate);
    pInType->SetUINT64(MF_MT_PIXEL_ASPECT_RATIO, ((UINT64)1 << 32) | 1);

    pWriter->SetInputMediaType(sIndex, pInType, nullptr);
    pInType->Release();
    pWriter->BeginWriting();

    DWORD dst_cb = target_w * target_h * 4;
    auto* dst_pixels = (uint32_t*)malloc(dst_cb);

    auto* lut_x = (int*)malloc((size_t)target_w * sizeof(int));
    auto* lut_y = (int*)malloc((size_t)target_h * sizeof(int));
    for (int x = 0; x < target_w; x++) {
        int sx = (target_w == src_w) ? x : ((x * src_w) / target_w);
        lut_x[x] = (sx < src_w) ? sx : (src_w - 1);
    }
    for (int y = 0; y < target_h; y++) {
        int sy = (target_h == src_h) ? y : ((y * src_h) / target_h);
        lut_y[y] = (sy < src_h) ? sy : (src_h - 1);
    }

    LONGLONG ts = 0;
    DWORD flags = 0;
    IMFSample* sample = nullptr;
    LONGLONG target_step = 10000000 / out_fps;
    LONGLONG next_ts = 0, out_ts = 0;
    int frames = 0;
    bool aborted = false;

    while (!s_cancel) {
        DWORD sIdx = 0;
        hr = pReader->ReadSample((DWORD)MF_SOURCE_READER_FIRST_VIDEO_STREAM, 0, &sIdx, &flags, &ts, &sample);
        if (flags & MF_SOURCE_READERF_ENDOFSTREAM) break;

        if (SUCCEEDED(hr) && sample) {
            if (ts >= next_ts || frames == 0) {
                IMFMediaBuffer* buf = nullptr;
                if (SUCCEEDED(sample->ConvertToContiguousBuffer(&buf))) {
                    IMF2DBuffer* p2D = nullptr;
                    BYTE* scan0 = nullptr;
                    LONG pitch = 0;
                    HRESULT hr2d = buf->QueryInterface(GUID_IMF2DBuffer, (void**)&p2D);

                    if (SUCCEEDED(hr2d) && p2D && SUCCEEDED(p2D->Lock2D(&scan0, &pitch))) {
                        for (int dy = 0; dy < target_h; dy++) {
                            int sy = lut_y[dy];
                            const BYTE* src_row = scan0 + ((ptrdiff_t)sy * (ptrdiff_t)pitch);
                            int dest_row = target_h - 1 - dy;
                            uint32_t* dst_row = dst_pixels + (dest_row * target_w);
                            for (int dx = 0; dx < target_w; dx++) {
                                int s_idx = lut_x[dx] * 4;
                                uint32_t b = src_row[s_idx + 0];
                                uint32_t g = src_row[s_idx + 1];
                                uint32_t r = src_row[s_idx + 2];
                                dst_row[dx] = 0xFF000000 | (r << 16) | (g << 8) | b;
                            }
                        }
                        p2D->Unlock2D();
                        p2D->Release();
                    } else {
                        if (p2D) p2D->Release();
                        BYTE* src = nullptr;
                        DWORD cur_len = 0;
                        if (SUCCEEDED(buf->Lock(&src, nullptr, &cur_len))) {
                            LONG pitch = (def_stride != 0) ? def_stride : (src_w * 4);
                            if (src_h > 0 && cur_len >= (size_t)src_h * (size_t)src_w * 4) {
                                pitch = (LONG)(cur_len / (size_t)src_h);
                            }
                            for (int dy = 0; dy < target_h; dy++) {
                                int sy = lut_y[dy];
                                const BYTE* src_row = src + ((ptrdiff_t)sy * (ptrdiff_t)pitch);
                                int dest_row = target_h - 1 - dy;
                                uint32_t* dst_row = dst_pixels + (dest_row * target_w);
                                for (int dx = 0; dx < target_w; dx++) {
                                    int s_idx = lut_x[dx] * 4;
                                    uint32_t b = src_row[s_idx + 0];
                                    uint32_t g = src_row[s_idx + 1];
                                    uint32_t r = src_row[s_idx + 2];
                                    dst_row[dx] = 0xFF000000 | (r << 16) | (g << 8) | b;
                                }
                            }
                            buf->Unlock();
                        }
                    }

                    IMFMediaBuffer* out_buf = nullptr;
                    MFCreateMemoryBuffer(dst_cb, &out_buf);
                    BYTE* out_p = nullptr;
                    out_buf->Lock(&out_p, nullptr, nullptr);
                    memcpy(out_p, dst_pixels, dst_cb);
                    out_buf->Unlock();
                    out_buf->SetCurrentLength(dst_cb);

                    IMFSample* out_sample = nullptr;
                    MFCreateSample(&out_sample);
                    out_sample->AddBuffer(out_buf);
                    out_sample->SetSampleTime(out_ts);
                    out_sample->SetSampleDuration(target_step);

                    pWriter->WriteSample(sIndex, out_sample);

                    out_sample->Release();
                    out_buf->Release();

                    out_ts += target_step;
                    next_ts += target_step;
                    frames++;

                    buf->Release();
                }
            }
            sample->Release();
        }
    }

    if (s_cancel) {
        aborted = true;
    }

    free(lut_x);
    free(lut_y);
    free(dst_pixels);
    pWriter->Finalize();
    pWriter->Release();
    pReader->Release();
    MFShutdown();

    if (aborted) {
        DeleteFileA(temp_out);
        printf("[VideoCache] Background optimization cancelled for '%s'.\n", in_path);
    } else {
        MoveFileExA(temp_out, final_out, MOVEFILE_REPLACE_EXISTING);
        printf("[VideoCache] Optimization completed: '%s' (%d frames) -> Ready!\n", final_out, frames);

        ensure_cs();
        EnterCriticalSection(&s_cs);
        strncpy(s_completed_path, final_out, sizeof(s_completed_path) - 1);
        s_completed = true;
        LeaveCriticalSection(&s_cs);
    }

    free(in_path);
    s_thread = nullptr;
    return 0;
}

void video_cache_start_background(const char* input_path) {
    if (!input_path || !input_path[0]) return;

    if (s_thread != nullptr) {
        DWORD exitCode = 0;
        if (GetExitCodeThread(s_thread, &exitCode) && exitCode == STILL_ACTIVE) {
            return;
        }
        CloseHandle(s_thread);
        s_thread = nullptr;
    }

    ensure_cs();
    EnterCriticalSection(&s_cs);
    s_cancel = false;
    s_completed = false;
    s_completed_path[0] = '\0';
    strncpy(s_current_input, input_path, sizeof(s_current_input) - 1);
    LeaveCriticalSection(&s_cs);

    char* heap_path = _strdup(input_path);
    s_thread = CreateThread(nullptr, 0, transcode_thread_proc, heap_path, 0, nullptr);
    if (s_thread) {
        SetThreadPriority(s_thread, THREAD_PRIORITY_BELOW_NORMAL);
    }
}

bool video_cache_poll_completed(char* out_cached_path, size_t max_len) {
    bool done = false;
    ensure_cs();
    EnterCriticalSection(&s_cs);
    if (s_completed) {
        done = true;
        s_completed = false;
        if (out_cached_path && max_len > 0) {
            strncpy(out_cached_path, s_completed_path, max_len - 1);
        }
    }
    LeaveCriticalSection(&s_cs);
    return done;
}

void video_cache_cancel(void) {
    s_cancel = true;
    if (s_thread != nullptr) {
        WaitForSingleObject(s_thread, 1000);
        CloseHandle(s_thread);
        s_thread = nullptr;
    }
}
