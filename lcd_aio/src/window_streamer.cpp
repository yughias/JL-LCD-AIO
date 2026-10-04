#include "window_streamer.hpp"
#include <windows.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#ifndef PW_RENDERFULLCONTENT
#define PW_RENDERFULLCONTENT 0x00000002
#endif

namespace {

struct EnumCtx {
    std::vector<StreamWindowItem>* list;
};

BOOL CALLBACK EnumWindowsProc(HWND hwnd, LPARAM lParam) {
    auto* ctx = reinterpret_cast<EnumCtx*>(lParam);

    if (!IsWindowVisible(hwnd)) return TRUE;
    if (IsIconic(hwnd)) return TRUE;

    LONG exStyle = GetWindowLongA(hwnd, GWL_EXSTYLE);
    if (exStyle & WS_EX_TOOLWINDOW) return TRUE;

    char title[256];
    int len = GetWindowTextA(hwnd, title, sizeof(title));
    if (len <= 0) return TRUE;

    if (strcmp(title, "Program Manager") == 0) return TRUE;

    RECT rc;
    GetClientRect(hwnd, &rc);
    if ((rc.right - rc.left) < 32 || (rc.bottom - rc.top) < 32) return TRUE;

    ctx->list->push_back({ (void*)hwnd, std::string(title) });
    return TRUE;
}

} // namespace

WindowStreamer::WindowStreamer() = default;

WindowStreamer::~WindowStreamer() {
    Shutdown();
}

std::vector<StreamWindowItem> WindowStreamer::GetAvailableWindows() const {
    std::vector<StreamWindowItem> list;
    EnumCtx ctx{ &list };
    EnumWindows(EnumWindowsProc, reinterpret_cast<LPARAM>(&ctx));
    return list;
}

void WindowStreamer::ReleaseCaptureDc() {
    if (m_mem_dc) {
        if (m_old_bitmap) {
            SelectObject((HDC)m_mem_dc, (HGDIOBJ)m_old_bitmap);
            m_old_bitmap = nullptr;
        }
        DeleteDC((HDC)m_mem_dc);
        m_mem_dc = nullptr;
    }
    if (m_dib_bitmap) {
        DeleteObject((HBITMAP)m_dib_bitmap);
        m_dib_bitmap = nullptr;
        m_dib_bits = nullptr;
    }
}

void WindowStreamer::Shutdown() {
    Stop();
    ReleaseCaptureDc();
    if (m_pixel_buffer) {
        free(m_pixel_buffer);
        m_pixel_buffer = nullptr;
        m_buf_w = 0;
        m_buf_h = 0;
    }
}

bool WindowStreamer::Start(void* hwnd) {
    if (!hwnd || !IsWindow((HWND)hwnd)) return false;

    char title[256] = {0};
    GetWindowTextA((HWND)hwnd, title, sizeof(title) - 1);

    m_target_hwnd = hwnd;
    m_target_title = title;
    printf("[WindowStreamer] Streaming window: '%s' (HWND: %p)\n", m_target_title.c_str(), hwnd);
    return true;
}

void WindowStreamer::Stop() {
    if (m_target_hwnd) {
        printf("[WindowStreamer] Stopped streaming '%s'\n", m_target_title.c_str());
        m_target_hwnd = nullptr;
        m_target_title.clear();
    }
}

bool WindowStreamer::IsActive() const {
    return m_target_hwnd != nullptr && IsWindow((HWND)m_target_hwnd);
}

const std::string& WindowStreamer::GetActiveTitle() const {
    return m_target_title;
}

void WindowStreamer::ShowPickerMenu(void* parent_hwnd) {
    auto windows = GetAvailableWindows();

    POINT pt;
    GetCursorPos(&pt);

    HMENU hMenu = CreatePopupMenu();
    AppendMenuA(hMenu, MF_STRING, 1, "[ Restore Video Background ]");
    AppendMenuA(hMenu, MF_SEPARATOR, 0, nullptr);

    int id_base = 100;
    for (size_t i = 0; i < windows.size() && i < 30; i++) {
        char item_text[160];
        snprintf(item_text, sizeof(item_text), "%d. %.70s", (int)(i + 1), windows[i].title.c_str());
        AppendMenuA(hMenu, MF_STRING, id_base + (int)i, item_text);
    }

    HWND hwnd = parent_hwnd ? (HWND)parent_hwnd : GetForegroundWindow();
    SetForegroundWindow(hwnd);
    int cmd = TrackPopupMenu(hMenu, TPM_RIGHTBUTTON | TPM_RETURNCMD | TPM_NONOTIFY, pt.x, pt.y, 0, hwnd, nullptr);
    DestroyMenu(hMenu);

    if (cmd == 1) {
        Stop();
    } else if (cmd >= id_base) {
        size_t idx = (size_t)(cmd - id_base);
        if (idx < windows.size()) {
            Start(windows[idx].hwnd);
        }
    }
}

bool WindowStreamer::CaptureFrame(uint8_t** out_pixels, int* out_w, int* out_h) {
    if (!IsActive()) return false;

    HWND target = (HWND)m_target_hwnd;
    RECT rc;
    if (!GetClientRect(target, &rc)) {
        Stop();
        return false;
    }

    int w = rc.right - rc.left;
    int h = rc.bottom - rc.top;
    if (w < 4 || h < 4) return false;

    w = (w + 3) & ~3;

    if (!m_mem_dc || w != m_buf_w || h != m_buf_h) {
        ReleaseCaptureDc();

        HDC screen_dc = GetDC(nullptr);
        m_mem_dc = (void*)CreateCompatibleDC(screen_dc);

        BITMAPINFO bmi{};
        bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        bmi.bmiHeader.biWidth = w;
        bmi.bmiHeader.biHeight = -h; // Top-down DIB
        bmi.bmiHeader.biPlanes = 1;
        bmi.bmiHeader.biBitCount = 32;
        bmi.bmiHeader.biCompression = BI_RGB;

        void* bits = nullptr;
        m_dib_bitmap = (void*)CreateDIBSection(screen_dc, &bmi, DIB_RGB_COLORS, &bits, nullptr, 0);
        ReleaseDC(nullptr, screen_dc);

        if (!m_dib_bitmap || !bits) {
            ReleaseCaptureDc();
            return false;
        }

        m_dib_bits = bits;
        m_old_bitmap = (void*)SelectObject((HDC)m_mem_dc, (HBITMAP)m_dib_bitmap);

        if (w * h * 4 > m_buf_w * m_buf_h * 4) {
            m_pixel_buffer = (uint8_t*)realloc(m_pixel_buffer, (size_t)w * (size_t)h * 4);
        }
        m_buf_w = w;
        m_buf_h = h;
    }

    BOOL ok = PrintWindow(target, (HDC)m_mem_dc, PW_RENDERFULLCONTENT);
    if (!ok) {
        HDC target_dc = GetDC(target);
        if (target_dc) {
            ok = BitBlt((HDC)m_mem_dc, 0, 0, w, h, target_dc, 0, 0, SRCCOPY);
            ReleaseDC(target, target_dc);
        }
    }

    if (!ok) return false;

    const uint8_t* src_bgra = (const uint8_t*)m_dib_bits;
    uint32_t* dst_rgba = (uint32_t*)m_pixel_buffer;
    int total_pixels = w * h;

    for (int i = 0; i < total_pixels; i++) {
        int s = i * 4;
        uint32_t b = src_bgra[s + 0];
        uint32_t g = src_bgra[s + 1];
        uint32_t r = src_bgra[s + 2];
        dst_rgba[i] = 0xFF000000 | (r << 0) | (g << 8) | (b << 16);
    }

    if (out_pixels) *out_pixels = m_pixel_buffer;
    if (out_w) *out_w = w;
    if (out_h) *out_h = h;
    return true;
}
