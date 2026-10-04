#pragma once

#include <cstdint>
#include <string>

struct jl_panel;

class JLPanel {
public:
    JLPanel();
    ~JLPanel();

    bool Open(const std::string& port_override = "");
    void Close();
    bool IsConnected() const;
    bool IsOpen() const { return IsConnected(); }

    void PushSurfaceAsync(const void* bgra, int width, int height);
    void SetBrightness(int brightness);
    void SetJpegQuality(float quality);
    void Show(bool visible);

    static void TransformFrame(const uint8_t* src_rgba, int w, int h,
                               uint8_t* dst_bgra,
                               int rotation, bool flip_x, bool flip_y,
                               bool invert_v = true);

private:
    jl_panel* m_panel = nullptr;
};
