#pragma once

#include <string>
#include <vector>
#include <cstdint>

struct StreamWindowItem {
    void* hwnd = nullptr;
    std::string title;
};

class WindowStreamer {
public:
    WindowStreamer();
    ~WindowStreamer();

    std::vector<StreamWindowItem> GetAvailableWindows() const;
    void ShowPickerMenu(void* parent_hwnd);
    bool Start(void* hwnd);
    void Stop();
    bool IsActive() const;
    const std::string& GetActiveTitle() const;
    bool CaptureFrame(uint8_t** out_pixels, int* out_w, int* out_h);
    void Shutdown();

private:
    void ReleaseCaptureDc();

    void* m_target_hwnd = nullptr;
    std::string m_target_title;

    void* m_mem_dc = nullptr;
    void* m_dib_bitmap = nullptr;
    void* m_old_bitmap = nullptr;
    void* m_dib_bits = nullptr;

    uint8_t* m_pixel_buffer = nullptr;
    int m_buf_w = 0;
    int m_buf_h = 0;
};
