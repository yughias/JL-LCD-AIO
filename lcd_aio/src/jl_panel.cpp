#define JL_PANEL_IMPLEMENTATION
#include "jl_lcd.h"
#include "jl_panel.hpp"

JLPanel::JLPanel() = default;

JLPanel::~JLPanel() {
    Close();
}

bool JLPanel::Open(const std::string& port_override) {
    if (!jl_init()) return false;
    const char* port = port_override.empty() ? nullptr : port_override.c_str();
    m_panel = jl_open(port);
    return m_panel != nullptr;
}

void JLPanel::Close() {
    if (m_panel) {
        jl_close(m_panel);
        m_panel = nullptr;
    }
}

bool JLPanel::IsConnected() const {
    return m_panel != nullptr;
}

void JLPanel::PushSurfaceAsync(const void* bgra, int width, int height) {
    if (m_panel) {
        jl_push_surface_async(m_panel, bgra, width, height);
    }
}

void JLPanel::SetBrightness(int brightness) {
    if (m_panel) {
        jl_set_brightness(m_panel, brightness);
    }
}

void JLPanel::SetJpegQuality(float quality) {
    if (m_panel) {
        jl_set_jpeg_quality(m_panel, quality);
    }
}

void JLPanel::Show(bool visible) {
    if (m_panel) {
        jl_show(m_panel, visible);
    }
}

bool JLPanel::IsShown() const {
    if (m_panel) {
        return jl_is_shown(m_panel);
    }
    return false;
}

void JLPanel::TransformFrame(const uint8_t* src_rgba, int w, int h,
                             uint8_t* dst_bgra,
                             int rotation, bool flip_x, bool flip_y,
                             bool invert_v) {
    if (!src_rgba || !dst_bgra) return;

    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            int sx = x;
            int sy = y;

            if (invert_v) {
                sy = (h - 1) - sy;
            }

            if (flip_x) {
                sx = (w - 1) - sx;
            }
            if (flip_y) {
                sy = (h - 1) - sy;
            }

            int rot = (rotation % 360 + 360) % 360;
            if (rot == 90) {
                int nx = (w - 1) - sy;
                int ny = sx;
                sx = nx; sy = ny;
            } else if (rot == 180) {
                sx = (w - 1) - sx;
                sy = (h - 1) - sy;
            } else if (rot == 270) {
                int nx = sy;
                int ny = (h - 1) - sx;
                sx = nx; sy = ny;
            }

            if (sx < 0) sx = 0;
            if (sx >= w) sx = w - 1;
            if (sy < 0) sy = 0;
            if (sy >= h) sy = h - 1;

            int src_idx = (sy * w + sx) * 4;
            int dst_idx = (y * w + x) * 4;

            // Convert Raylib RGBA to panel BGRA
            dst_bgra[dst_idx + 0] = src_rgba[src_idx + 2]; // Blue
            dst_bgra[dst_idx + 1] = src_rgba[src_idx + 1]; // Green
            dst_bgra[dst_idx + 2] = src_rgba[src_idx + 0]; // Red
            dst_bgra[dst_idx + 3] = 0xFF;                  // Alpha
        }
    }
}
