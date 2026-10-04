#include "widgets.hpp"
#include "audio_visualizer.hpp"
#include "imgui.h"
#include "raylib.h"
#include "raymath.h"
#include "rlgl.h"
#include <cmath>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <algorithm>

// --- Color Helpers ---

static std::string ColorToHex(Color c) {
    char buf[16];
    snprintf(buf, sizeof(buf), "#%02X%02X%02X%02X", c.r, c.g, c.b, c.a);
    return std::string(buf);
}

static Color HexToColor(const std::string& hex, Color def) {
    if (hex.empty()) return def;
    const char* str = hex.c_str();
    if (str[0] == '#') str++;
    unsigned int val = 0;
    if (sscanf(str, "%x", &val) == 1) {
        if (hex.length() <= 7) { // #RRGGBB
            return Color{
                (uint8_t)((val >> 16) & 0xFF),
                (uint8_t)((val >> 8) & 0xFF),
                (uint8_t)(val & 0xFF),
                255
            };
        }
        // #RRGGBBAA
        return Color{
            (uint8_t)((val >> 24) & 0xFF),
            (uint8_t)((val >> 16) & 0xFF),
            (uint8_t)((val >> 8) & 0xFF),
            (uint8_t)(val & 0xFF)
        };
    }
    return def;
}

// --- FontCollection Implementation ---

void FontCollection::Load() {
    const char* font_candidates[] = {
        "C:\\Windows\\Fonts\\segoeuib.ttf",
        "C:\\Windows\\Fonts\\arialbd.ttf",
        "C:\\Windows\\Fonts\\segoeui.ttf",
        "C:\\Windows\\Fonts\\arial.ttf"
    };

    const char* chosen = nullptr;
    for (const char* candidate : font_candidates) {
        if (FileExists(candidate)) {
            chosen = candidate;
            break;
        }
    }

    if (chosen) {
        int codepoints[256];
        for (int i = 0; i < 224; i++) {
            codepoints[i] = 32 + i; // 32..255 (includes Latin-1)
        }

        font_large = LoadFontEx(chosen, 48, codepoints, 224);
        font_medium = LoadFontEx(chosen, 32, codepoints, 224);
        font_small = LoadFontEx(chosen, 24, codepoints, 224);

        SetTextureFilter(font_large.texture, TEXTURE_FILTER_BILINEAR);
        SetTextureFilter(font_medium.texture, TEXTURE_FILTER_BILINEAR);
        SetTextureFilter(font_small.texture, TEXTURE_FILTER_BILINEAR);

        has_custom = true;
    } else {
        has_custom = false;
    }
}

void FontCollection::Unload() {
    if (has_custom) {
        UnloadFont(font_large);
        UnloadFont(font_medium);
        UnloadFont(font_small);
        has_custom = false;
    }
}

Font FontCollection::GetFontForSize(float size) const {
    if (!has_custom) return GetFontDefault();
    if (size >= 38.0f) return font_large;
    if (size >= 22.0f) return font_medium;
    return font_small;
}

// Styled text drawing with halo / dropshadow for contrast against animated backgrounds
static void DrawStyledText(Font font, const char* text, Vector2 pos, float font_size, Color color, bool center_x, bool center_y) {
    if (!text || text[0] == '\0') return;

    Vector2 sz = MeasureTextEx(font, text, font_size, 1.0f);
    float dx = pos.x;
    float dy = pos.y;
    if (center_x) dx -= sz.x * 0.5f;
    if (center_y) dy -= sz.y * 0.5f;

    Vector2 p = { dx, dy };
    Color shadow = { 0, 15, 35, 120 };
    Color highlight = { 255, 255, 255, 60 };

    DrawTextEx(font, text, Vector2{ p.x + 1.5f, p.y + 1.5f }, font_size, 1.0f, shadow);
    DrawTextEx(font, text, Vector2{ p.x - 1.0f, p.y - 1.0f }, font_size, 1.0f, highlight);
    DrawTextEx(font, text, p, font_size, 1.0f, color);
}

// --- Widget Base Implementation ---

void Widget::SaveToIni(tortellini::ini& ini) const {
    const std::string& id = GetId();
    ini[id]["enabled"] = m_enabled;
    ini[id]["x"] = m_pos.x;
    ini[id]["y"] = m_pos.y;
    ini[id]["size"] = m_size;
    ini[id]["color"] = ColorToHex(m_color);
}

void Widget::LoadFromIni(tortellini::ini& ini) {
    const std::string& id = GetId();
    m_enabled = ini[id]["enabled"] | m_defEnabled;
    m_pos.x = (float)(ini[id]["x"] | (double)m_defPos.x);
    m_pos.y = (float)(ini[id]["y"] | (double)m_defPos.y);
    m_size = (float)(ini[id]["size"] | (double)m_defSize);
    std::string hex = ini[id]["color"] | ColorToHex(m_defColor);
    m_color = HexToColor(hex, m_defColor);
}

void Widget::ResetDefaults() {
    m_enabled = m_defEnabled;
    m_pos = m_defPos;
    m_size = m_defSize;
    m_color = m_defColor;
}

// --- TextWidget Implementation ---

TextWidget::TextWidget(std::string id, std::string name, std::string category,
                       Vector2 defPos, float defSize, Color defColor, bool defEnabled,
                       TextSupplier supplier, bool isTemp, bool centerX, bool centerY)
    : m_id(std::move(id)), m_name(std::move(name)), m_category(std::move(category)),
      m_supplier(std::move(supplier)), m_isTemp(isTemp), m_centerX(centerX), m_centerY(centerY) {
    m_defPos = defPos;
    m_defSize = defSize;
    m_defColor = defColor;
    m_defEnabled = defEnabled;
    ResetDefaults();
}

Rectangle TextWidget::GetBounds(const FontCollection& fonts, const HardwareStats& stats) const {
    std::string text = (m_useCustomText && !m_customText.empty()) ? m_customText : (m_supplier ? m_supplier(stats) : "");
    if (m_isTemp && !m_useCustomText) {
        text += " °C";
    }

    Font f = fonts.GetFontForSize(m_size);
    Vector2 sz = MeasureTextEx(f, text.c_str(), m_size, 1.0f);

    float bx = m_pos.x;
    float by = m_pos.y;
    if (m_centerX) bx -= sz.x * 0.5f;
    if (m_centerY) by -= sz.y * 0.5f;

    // Generous padding for easy selection
    return Rectangle{ bx - 4.0f, by - 4.0f, sz.x + 8.0f, sz.y + 8.0f };
}

void TextWidget::Render(const RenderContext& ctx) {
    if (!m_enabled) return;

    std::string text = (m_useCustomText && !m_customText.empty()) ? m_customText : (m_supplier ? m_supplier(ctx.stats) : "");
    Font f = ctx.fonts.GetFontForSize(m_size);

    if (m_isTemp && !m_useCustomText) {
        // Temperature format: Vector degree ring + 'C'
        Vector2 num_sz = MeasureTextEx(f, text.c_str(), m_size, 1.0f);
        float deg_outer = m_size * 0.12f;
        float deg_inner = m_size * 0.06f;
        if (deg_outer < 2.5f) deg_outer = 2.5f;
        if (deg_inner < 1.0f) deg_inner = 1.0f;
        float gap = m_size * 0.08f;
        Vector2 c_sz = MeasureTextEx(f, "C", m_size, 1.0f);
        float total_w = num_sz.x + gap + (deg_outer * 2.0f) + gap + c_sz.x;

        float start_x = m_pos.x - (total_w * 0.5f);
        float base_y = m_pos.y - (num_sz.y * 0.5f);

        DrawStyledText(f, text.c_str(), Vector2{ start_x, base_y }, m_size, m_color, false, false);

        Vector2 deg_center = {
            start_x + num_sz.x + gap + deg_outer,
            base_y + m_size * 0.28f
        };
        DrawRing(Vector2{ deg_center.x + 1.2f, deg_center.y + 1.2f }, deg_inner, deg_outer, 0.0f, 360.0f, 24, Color{ 0, 15, 35, 120 });
        DrawRing(deg_center, deg_inner, deg_outer, 0.0f, 360.0f, 24, m_color);

        float c_x = deg_center.x + deg_outer + gap;
        DrawStyledText(f, "C", Vector2{ c_x, base_y }, m_size, m_color, false, false);
    } else {
        DrawStyledText(f, text.c_str(), m_pos, m_size, m_color, m_centerX, m_centerY);
    }
}

bool TextWidget::DrawInspectorUI() {
    bool changed = false;

    if (ImGui::Checkbox("Enabled", &m_enabled)) changed = true;

    if (ImGui::DragFloat2("Position", &m_pos.x, 1.0f, 0.0f, 960.0f, "%.1f")) changed = true;
    if (ImGui::SliderFloat("Font Size", &m_size, 8.0f, 72.0f, "%.1f")) changed = true;

    float col[4] = { m_color.r / 255.0f, m_color.g / 255.0f, m_color.b / 255.0f, m_color.a / 255.0f };
    if (ImGui::ColorEdit4("Color", col)) {
        m_color = Color{
            (uint8_t)(col[0] * 255.0f + 0.5f),
            (uint8_t)(col[1] * 255.0f + 0.5f),
            (uint8_t)(col[2] * 255.0f + 0.5f),
            (uint8_t)(col[3] * 255.0f + 0.5f)
        };
        changed = true;
    }

    if (m_id == "cpu_header" || m_id == "gpu_header") {
        ImGui::Separator();
        if (ImGui::Checkbox("Custom Label", &m_useCustomText)) changed = true;
        if (m_useCustomText) {
            char buf[64];
            strncpy(buf, m_customText.c_str(), sizeof(buf));
            buf[sizeof(buf) - 1] = '\0';
            if (ImGui::InputText("Label Text", buf, sizeof(buf))) {
                m_customText = buf;
                // If user deletes all text, immediately revert to auto-detected hardware name!
                if (m_customText.empty()) {
                    m_useCustomText = false;
                }
                changed = true;
            }
        } else {
            ImGui::TextDisabled("Auto-detect: %s", m_id == "cpu_header" ? "Detected CPU" : "Detected GPU");
        }
    }

    ImGui::Spacing();
    if (ImGui::Button("Reset to Default")) {
        ResetDefaults();
        changed = true;
    }

    return changed;
}

void TextWidget::SaveToIni(tortellini::ini& ini) const {
    Widget::SaveToIni(ini);
    if (m_useCustomText) {
        ini[GetId()]["custom_text"] = m_customText;
    }
}

void TextWidget::LoadFromIni(tortellini::ini& ini) {
    Widget::LoadFromIni(ini);
    const std::string& id = GetId();
    std::string custom = ini[id]["custom_text"] | "";
    if (!custom.empty()) {
        m_customText = custom;
        m_useCustomText = true;
    }
}

void TextWidget::ResetDefaults() {
    Widget::ResetDefaults();
    m_useCustomText = false;
    m_customText.clear();
}

// --- ArcGaugeWidget Implementation ---

ArcGaugeWidget::ArcGaugeWidget(std::string id, std::string name, std::string category,
                               Vector2 defPos, float defRadius, Color defColor, bool defEnabled,
                               ArcType arcType, ValueSupplier supplier, float minVal, float maxVal)
    : m_id(std::move(id)), m_name(std::move(name)), m_category(std::move(category)),
      m_arcType(arcType), m_supplier(std::move(supplier)), m_minVal(minVal), m_maxVal(maxVal) {
    m_defPos = defPos;
    m_defSize = defRadius;
    m_defColor = defColor;
    m_defEnabled = defEnabled;
    ResetDefaults();
}

Rectangle ArcGaugeWidget::GetBounds(const FontCollection& /*fonts*/, const HardwareStats& /*stats*/) const {
    float r = m_size;
    if (m_arcType == ArcType::Dome) {
        // Dome sweeps upper half above center
        return Rectangle{ m_pos.x - r - 4.0f, m_pos.y - r - 4.0f, (r * 2.0f) + 8.0f, r + 8.0f };
    }
    // Full circle
    return Rectangle{ m_pos.x - r - 4.0f, m_pos.y - r - 4.0f, (r * 2.0f) + 8.0f, (r * 2.0f) + 8.0f };
}

void ArcGaugeWidget::Render(const RenderContext& ctx) {
    if (!m_enabled) return;

    float val = m_supplier ? m_supplier(ctx.stats) : 0.0f;
    float outer_r = m_size;
    if (outer_r < 12.0f) outer_r = 12.0f;

    if (m_arcType == ArcType::FullCircle) {
        float inner_r = outer_r * (54.0f / 74.0f);
        int segments = 64;

        // Semi-transparent background track
        Color track_color = { (uint8_t)(m_color.r / 3), (uint8_t)(m_color.g / 3), (uint8_t)(m_color.b / 3), 90 };
        DrawRing(m_pos, inner_r, outer_r, 0.0f, 360.0f, segments, track_color);

        // Active progress arc starting from top (270 degrees)
        float clamped = std::clamp(val, m_minVal, m_maxVal);
        float pct = (clamped - m_minVal) / (m_maxVal - m_minVal);
        float sweep = pct * 360.0f;
        if (sweep > 0.5f) {
            DrawRing(m_pos, inner_r, outer_r, 270.0f, 270.0f + sweep, segments, m_color);
        }
    } else {
        // Dome arc (180 to 360 degrees, sweeping over the top)
        float inner_r = outer_r * (46.0f / 58.0f);
        int segments = 48;

        Color track_color = { (uint8_t)(m_color.r / 3), (uint8_t)(m_color.g / 3), (uint8_t)(m_color.b / 3), 90 };
        DrawRing(m_pos, inner_r, outer_r, 180.0f, 360.0f, segments, track_color);

        float clamped = std::clamp(val, m_minVal, m_maxVal);
        float pct = (clamped - m_minVal) / (m_maxVal - m_minVal);
        float sweep = pct * 180.0f;
        if (sweep > 0.5f) {
            DrawRing(m_pos, inner_r, outer_r, 180.0f, 180.0f + sweep, segments, m_color);
        }
    }
}

bool ArcGaugeWidget::DrawInspectorUI() {
    bool changed = false;

    if (ImGui::Checkbox("Enabled", &m_enabled)) changed = true;
    if (ImGui::DragFloat2("Position", &m_pos.x, 1.0f, 0.0f, 960.0f, "%.1f")) changed = true;
    if (ImGui::SliderFloat("Radius", &m_size, 15.0f, 150.0f, "%.1f")) changed = true;

    float col[4] = { m_color.r / 255.0f, m_color.g / 255.0f, m_color.b / 255.0f, m_color.a / 255.0f };
    if (ImGui::ColorEdit4("Arc Color", col)) {
        m_color = Color{
            (uint8_t)(col[0] * 255.0f + 0.5f),
            (uint8_t)(col[1] * 255.0f + 0.5f),
            (uint8_t)(col[2] * 255.0f + 0.5f),
            (uint8_t)(col[3] * 255.0f + 0.5f)
        };
        changed = true;
    }

    ImGui::Spacing();
    if (ImGui::Button("Reset to Default")) {
        ResetDefaults();
        changed = true;
    }

    return changed;
}

void ArcGaugeWidget::SaveToIni(tortellini::ini& ini) const {
    Widget::SaveToIni(ini);
}

void ArcGaugeWidget::LoadFromIni(tortellini::ini& ini) {
    Widget::LoadFromIni(ini);
}

void ArcGaugeWidget::ResetDefaults() {
    Widget::ResetDefaults();
}

// --- VisualizerWidget Implementation ---

VisualizerWidget::VisualizerWidget(std::string id, std::string name, std::string category,
                                   Vector2 defPos, float defW, float defH, int defBars, Color defColor, bool defEnabled)
    : m_id(std::move(id)), m_name(std::move(name)), m_category(std::move(category)),
      m_width(defW), m_height(defH), m_bars(defBars),
      m_defWidth(defW), m_defHeight(defH), m_defBars(defBars) {
    m_defPos = defPos;
    m_defSize = defH;
    m_defColor = defColor;
    m_defEnabled = defEnabled;
    ResetDefaults();
}

Rectangle VisualizerWidget::GetBounds(const FontCollection& /*fonts*/, const HardwareStats& /*stats*/) const {
    return Rectangle{ m_pos.x - 4.0f, m_pos.y - 4.0f, m_width + 8.0f, m_height + 8.0f };
}

void VisualizerWidget::Render(const RenderContext& /*ctx*/) {
    if (!m_enabled) return;

    float bar_vals[64] = {0};
    int count = std::clamp(m_bars, 8, 64);
    float dt = GetFrameTime();
    audio_visualizer_get_bars(count, bar_vals, nullptr, dt);

    float spacing = 2.0f;
    float bar_w = (m_width - (spacing * (count - 1))) / count;
    if (bar_w < 1.0f) bar_w = 1.0f;

    for (int i = 0; i < count; i++) {
        float h = bar_vals[i] * m_height;
        if (h < 2.0f) h = 2.0f; // Minimal floor bar

        float bx = m_pos.x + i * (bar_w + spacing);
        float by = m_pos.y + m_height - h;

        Rectangle bar_rec = { bx, by, bar_w, h };
        if (m_style == 0) {
            DrawRectangleRounded(bar_rec, 0.4f, 4, m_color);
        } else if (m_style == 1) {
            DrawRectangleRec(bar_rec, m_color);
        } else {
            DrawRectangleLinesEx(bar_rec, 1.0f, m_color);
        }
    }
}

bool VisualizerWidget::DrawInspectorUI() {
    bool changed = false;

    if (ImGui::Checkbox("Enabled", &m_enabled)) changed = true;
    if (ImGui::DragFloat2("Position", &m_pos.x, 1.0f, 0.0f, 960.0f, "%.1f")) changed = true;
    if (ImGui::SliderFloat("Width", &m_width, 100.0f, 960.0f, "%.1f")) changed = true;
    if (ImGui::SliderFloat("Height", &m_height, 20.0f, 300.0f, "%.1f")) changed = true;
    if (ImGui::SliderInt("Bar Count", &m_bars, 8, 64)) changed = true;

    const char* styles[] = { "Rounded Bars", "Solid Rectangles", "Outlines" };
    if (ImGui::Combo("Style", &m_style, styles, 3)) changed = true;

    float col[4] = { m_color.r / 255.0f, m_color.g / 255.0f, m_color.b / 255.0f, m_color.a / 255.0f };
    if (ImGui::ColorEdit4("Visualizer Color", col)) {
        m_color = Color{
            (uint8_t)(col[0] * 255.0f + 0.5f),
            (uint8_t)(col[1] * 255.0f + 0.5f),
            (uint8_t)(col[2] * 255.0f + 0.5f),
            (uint8_t)(col[3] * 255.0f + 0.5f)
        };
        changed = true;
    }

    ImGui::Spacing();
    if (ImGui::Button("Reset to Default")) {
        ResetDefaults();
        changed = true;
    }

    return changed;
}

void VisualizerWidget::SaveToIni(tortellini::ini& ini) const {
    Widget::SaveToIni(ini);
    const std::string& id = GetId();
    ini[id]["width"] = m_width;
    ini[id]["height"] = m_height;
    ini[id]["bars"] = m_bars;
    ini[id]["style"] = m_style;
}

void VisualizerWidget::LoadFromIni(tortellini::ini& ini) {
    Widget::LoadFromIni(ini);
    const std::string& id = GetId();
    m_width = (float)(ini[id]["width"] | (double)m_defWidth);
    m_height = (float)(ini[id]["height"] | (double)m_defHeight);
    m_bars = (int)(ini[id]["bars"] | (long long)m_defBars);
    m_style = (int)(ini[id]["style"] | (long long)m_defStyle);
}

void VisualizerWidget::ResetDefaults() {
    Widget::ResetDefaults();
    m_width = m_defWidth;
    m_height = m_defHeight;
    m_bars = m_defBars;
    m_style = m_defStyle;
}

// --- WidgetManager Implementation ---

WidgetManager::WidgetManager() {
    InitDefaultWidgets();
}

void WidgetManager::InitDefaultWidgets() {
    m_widgets.clear();

    // 1-5: CPU Widgets
    m_widgets.push_back(std::make_unique<TextWidget>(
        "cpu_header", "CPU Header", "CPU",
        Vector2{ 155.0f, 65.0f }, 30.0f, Color{ 16, 26, 42, 255 }, true,
        [](const HardwareStats& s) { return !s.cpu_name.empty() ? s.cpu_name : "CPU"; }, false, true, true
    ));

    m_widgets.push_back(std::make_unique<ArcGaugeWidget>(
        "cpu_temp_arc", "CPU Temp Arc", "CPU",
        Vector2{ 155.0f, 195.0f }, 74.0f, Color{ 30, 230, 215, 255 }, true,
        ArcType::FullCircle, [](const HardwareStats& s) { return s.cpu_temp; }, 0.0f, 100.0f
    ));

    m_widgets.push_back(std::make_unique<TextWidget>(
        "cpu_temp_val", "CPU Temp Value", "CPU",
        Vector2{ 155.0f, 195.0f }, 36.0f, Color{ 245, 245, 250, 255 }, true,
        [](const HardwareStats& s) {
            char buf[16];
            snprintf(buf, sizeof(buf), "%.0f", s.cpu_temp);
            return std::string(buf);
        }, true, true, true
    ));

    m_widgets.push_back(std::make_unique<ArcGaugeWidget>(
        "cpu_util_arc", "CPU Util Arc", "CPU",
        Vector2{ 155.0f, 365.0f }, 58.0f, Color{ 30, 230, 215, 255 }, true,
        ArcType::Dome, [](const HardwareStats& s) { return s.cpu_util; }, 0.0f, 100.0f
    ));

    m_widgets.push_back(std::make_unique<TextWidget>(
        "cpu_util_val", "CPU Util Value", "CPU",
        Vector2{ 155.0f, 343.0f }, 28.0f, Color{ 245, 245, 250, 255 }, true,
        [](const HardwareStats& s) {
            char buf[16];
            snprintf(buf, sizeof(buf), "%.0f%%", s.cpu_util);
            return std::string(buf);
        }, false, true, true
    ));

    // 6-10: GPU Widgets
    m_widgets.push_back(std::make_unique<TextWidget>(
        "gpu_header", "GPU Header", "GPU",
        Vector2{ 805.0f, 65.0f }, 30.0f, Color{ 16, 26, 42, 255 }, true,
        [](const HardwareStats& s) { return !s.gpu_name.empty() ? s.gpu_name : "GPU"; }, false, true, true
    ));

    m_widgets.push_back(std::make_unique<ArcGaugeWidget>(
        "gpu_temp_arc", "GPU Temp Arc", "GPU",
        Vector2{ 805.0f, 195.0f }, 74.0f, Color{ 30, 230, 215, 255 }, true,
        ArcType::FullCircle, [](const HardwareStats& s) { return s.gpu_temp; }, 0.0f, 100.0f
    ));

    m_widgets.push_back(std::make_unique<TextWidget>(
        "gpu_temp_val", "GPU Temp Value", "GPU",
        Vector2{ 805.0f, 195.0f }, 36.0f, Color{ 245, 245, 250, 255 }, true,
        [](const HardwareStats& s) {
            char buf[16];
            snprintf(buf, sizeof(buf), "%.0f", s.gpu_temp);
            return std::string(buf);
        }, true, true, true
    ));

    m_widgets.push_back(std::make_unique<ArcGaugeWidget>(
        "gpu_util_arc", "GPU Util Arc", "GPU",
        Vector2{ 805.0f, 365.0f }, 58.0f, Color{ 30, 230, 215, 255 }, true,
        ArcType::Dome, [](const HardwareStats& s) { return s.gpu_util; }, 0.0f, 100.0f
    ));

    m_widgets.push_back(std::make_unique<TextWidget>(
        "gpu_util_val", "GPU Util Value", "GPU",
        Vector2{ 805.0f, 343.0f }, 28.0f, Color{ 245, 245, 250, 255 }, true,
        [](const HardwareStats& s) {
            char buf[16];
            snprintf(buf, sizeof(buf), "%.0f%%", s.gpu_util);
            return std::string(buf);
        }, false, true, true
    ));

    // 11-13: RAM Widgets (all independent)
    m_widgets.push_back(std::make_unique<ArcGaugeWidget>(
        "ram_arc", "RAM Arc", "Memory & Storage",
        Vector2{ 480.0f, 200.0f }, 56.0f, Color{ 46, 204, 113, 255 }, false,
        ArcType::FullCircle, [](const HardwareStats& s) { return s.ram_util; }, 0.0f, 100.0f
    ));

    m_widgets.push_back(std::make_unique<TextWidget>(
        "ram_text", "RAM Value", "Memory & Storage",
        Vector2{ 480.0f, 200.0f }, 28.0f, Color{ 245, 245, 250, 255 }, false,
        [](const HardwareStats& s) {
            char buf[16];
            snprintf(buf, sizeof(buf), "%.0f%%", s.ram_util);
            return std::string(buf);
        }, false, true, true
    ));

    m_widgets.push_back(std::make_unique<TextWidget>(
        "ram_subtext", "RAM Subtext", "Memory & Storage",
        Vector2{ 480.0f, 220.0f }, 13.0f, Color{ 245, 245, 250, 255 }, false,
        [](const HardwareStats& s) {
            char buf[32];
            snprintf(buf, sizeof(buf), "%.1f / %.1f GB", s.ram_used_gb, s.ram_total_gb);
            return std::string(buf);
        }, false, true, true
    ));

    // 14-16: Disk Widgets
    m_widgets.push_back(std::make_unique<ArcGaugeWidget>(
        "disk_arc", "Disk Arc", "Memory & Storage",
        Vector2{ 320.0f, 230.0f }, 52.0f, Color{ 255, 165, 0, 255 }, false,
        ArcType::FullCircle, [](const HardwareStats& s) { return s.disk_util; }, 0.0f, 100.0f
    ));

    m_widgets.push_back(std::make_unique<TextWidget>(
        "disk_text", "Disk Value", "Memory & Storage",
        Vector2{ 320.0f, 230.0f }, 26.0f, Color{ 245, 245, 250, 255 }, false,
        [](const HardwareStats& s) {
            char buf[16];
            snprintf(buf, sizeof(buf), "%.0f%%", s.disk_util);
            return std::string(buf);
        }, false, true, true
    ));

    m_widgets.push_back(std::make_unique<TextWidget>(
        "disk_subtext", "Disk Subtext", "Memory & Storage",
        Vector2{ 320.0f, 252.0f }, 11.0f, Color{ 245, 245, 250, 255 }, false,
        [](const HardwareStats& s) {
            char buf[32];
            snprintf(buf, sizeof(buf), "%.1f GB Free", s.disk_free_gb);
            return std::string(buf);
        }, false, true, true
    ));

    // 17-19: VRAM Widgets
    m_widgets.push_back(std::make_unique<ArcGaugeWidget>(
        "vram_arc", "VRAM Arc", "Memory & Storage",
        Vector2{ 640.0f, 230.0f }, 52.0f, Color{ 155, 89, 182, 255 }, false,
        ArcType::FullCircle, [](const HardwareStats& s) { return s.vram_util; }, 0.0f, 100.0f
    ));

    m_widgets.push_back(std::make_unique<TextWidget>(
        "vram_text", "VRAM Value", "Memory & Storage",
        Vector2{ 640.0f, 230.0f }, 26.0f, Color{ 245, 245, 250, 255 }, false,
        [](const HardwareStats& s) {
            char buf[16];
            snprintf(buf, sizeof(buf), "%.0f%%", s.vram_util);
            return std::string(buf);
        }, false, true, true
    ));

    m_widgets.push_back(std::make_unique<TextWidget>(
        "vram_subtext", "VRAM Subtext", "Memory & Storage",
        Vector2{ 640.0f, 252.0f }, 11.0f, Color{ 245, 245, 250, 255 }, false,
        [](const HardwareStats& s) {
            char buf[32];
            snprintf(buf, sizeof(buf), "%.1f / %.1f GB", s.vram_used_gb, s.vram_total_gb);
            return std::string(buf);
        }, false, true, true
    ));

    // 20-23: Time & Status Widgets
    m_widgets.push_back(std::make_unique<TextWidget>(
        "clock", "Digital Clock", "Time & Status",
        Vector2{ 480.0f, 55.0f }, 32.0f, Color{ 245, 245, 250, 255 }, false,
        [](const HardwareStats& /*s*/) {
            time_t raw = time(nullptr);
            struct tm* t = localtime(&raw);
            char buf[32];
            strftime(buf, sizeof(buf), "%H:%M:%S", t);
            return std::string(buf);
        }, false, true, true
    ));

    m_widgets.push_back(std::make_unique<TextWidget>(
        "clock_date", "Date Display", "Time & Status",
        Vector2{ 480.0f, 86.0f }, 14.0f, Color{ 245, 245, 250, 255 }, false,
        [](const HardwareStats& /*s*/) {
            time_t raw = time(nullptr);
            struct tm* t = localtime(&raw);
            char buf[64];
            strftime(buf, sizeof(buf), "%a %d %b %Y", t);
            return std::string(buf);
        }, false, true, true
    ));

    m_widgets.push_back(std::make_unique<TextWidget>(
        "fps_counter", "FPS Counter", "Time & Status",
        Vector2{ 900.0f, 20.0f }, 15.0f, Color{ 0, 255, 136, 255 }, false,
        [](const HardwareStats& s) {
            char buf[16];
            snprintf(buf, sizeof(buf), "%d FPS", s.fps);
            return std::string(buf);
        }, false, true, true
    ));

    m_widgets.push_back(std::make_unique<TextWidget>(
        "uptime", "System Uptime", "Time & Status",
        Vector2{ 480.0f, 455.0f }, 13.0f, Color{ 136, 204, 255, 255 }, false,
        [](const HardwareStats& s) {
            char buf[32];
            snprintf(buf, sizeof(buf), "Up: %lldh %lldm", (long long)(s.uptime_seconds / 3600), (long long)((s.uptime_seconds % 3600) / 60));
            return std::string(buf);
        }, false, true, true
    ));

    // 24: Visualizer Widget
    m_widgets.push_back(std::make_unique<VisualizerWidget>(
        "visualizer", "Audio Visualizer", "Audio",
        Vector2{ 220.0f, 390.0f }, 520.0f, 75.0f, 24, Color{ 0, 229, 255, 255 }, true
    ));
}

void WidgetManager::RenderAll(const RenderContext& ctx) {
    for (const auto& w : m_widgets) {
        if (w->IsEnabled()) {
            w->Render(ctx);
        }
    }
}

void WidgetManager::RenderEditorOverlay(const RenderContext& ctx) {
    // 1. Draw snap lines if active
    if (m_snapXActive) {
        DrawLineEx(Vector2{ m_snapX, 0.0f }, Vector2{ m_snapX, 480.0f }, 1.0f, Color{ 30, 230, 215, 180 });
    }
    if (m_snapYActive) {
        DrawLineEx(Vector2{ 0.0f, m_snapY }, Vector2{ 960.0f, m_snapY }, 1.0f, Color{ 30, 230, 215, 180 });
    }

    if (m_selectedWidgets.empty()) return;

    for (Widget* w : m_selectedWidgets) {
        if (!w || !w->IsEnabled()) continue;

        Rectangle b = w->GetBounds(ctx.fonts, ctx.stats);
        DrawRectangleLinesEx(b, 1.5f, Color{ 30, 230, 215, 230 });

        // Draw resize handles (4 corners + 4 edges) on all selected widgets
        const float hs = 10.0f;
        const float hs_half = hs * 0.5f;
        Color handle_fill = Color{ 30, 230, 215, 255 };
        Color handle_border = Color{ 10, 20, 35, 255 };

        Rectangle corners[4] = {
            { b.x - hs_half, b.y - hs_half, hs, hs },
            { b.x + b.width - hs_half, b.y - hs_half, hs, hs },
            { b.x - hs_half, b.y + b.height - hs_half, hs, hs },
            { b.x + b.width - hs_half, b.y + b.height - hs_half, hs, hs }
        };

        for (int i = 0; i < 4; i++) {
            DrawRectangleRec(corners[i], handle_fill);
            DrawRectangleLinesEx(corners[i], 1.0f, handle_border);
        }

        Rectangle edges[4] = {
            { b.x + b.width * 0.5f - 8.0f, b.y - 3.0f, 16.0f, 6.0f },              // Top
            { b.x + b.width * 0.5f - 8.0f, b.y + b.height - 3.0f, 16.0f, 6.0f },   // Bottom
            { b.x - 3.0f, b.y + b.height * 0.5f - 8.0f, 6.0f, 16.0f },              // Left
            { b.x + b.width - 3.0f, b.y + b.height * 0.5f - 8.0f, 6.0f, 16.0f }    // Right
        };

        for (int i = 0; i < 4; i++) {
            DrawRectangleRec(edges[i], handle_fill);
            DrawRectangleLinesEx(edges[i], 1.0f, handle_border);
        }

        // Display name and size tag for each selected widget
        char tag_str[128];
        if (w->GetType() == WidgetType::Visualizer) {
            auto* vis = static_cast<VisualizerWidget*>(w);
            snprintf(tag_str, sizeof(tag_str), "%s (%dx%d)", w->GetName().c_str(), (int)vis->GetWidth(), (int)vis->GetHeight());
        } else if (w->GetType() == WidgetType::ArcGauge) {
            snprintf(tag_str, sizeof(tag_str), "%s (R: %d)", w->GetName().c_str(), (int)w->GetSize());
        } else {
            snprintf(tag_str, sizeof(tag_str), "%s (Sz: %d)", w->GetName().c_str(), (int)w->GetSize());
        }

        int tw = MeasureText(tag_str, 10);
        float tag_x = b.x;
        float tag_y = b.y - 15.0f;
        if (tag_y < 2.0f) tag_y = b.y + b.height + 3.0f;
        DrawRectangleRec(Rectangle{ tag_x - 2.0f, tag_y, (float)tw + 8.0f, 14.0f }, Color{ 10, 25, 45, 230 });
        DrawRectangleLinesEx(Rectangle{ tag_x - 2.0f, tag_y, (float)tw + 8.0f, 14.0f }, 1.0f, Color{ 30, 230, 215, 180 });
        DrawText(tag_str, (int)tag_x + 2, (int)tag_y + 2, 10, Color{ 30, 230, 215, 255 });
    }
}

Widget* WidgetManager::HitTest(Vector2 pos, const FontCollection& fonts, const HardwareStats& stats) {
    // Reverse iteration so topmost drawn element gets selected first
    for (auto it = m_widgets.rbegin(); it != m_widgets.rend(); ++it) {
        if ((*it)->IsEnabled()) {
            Rectangle b = (*it)->GetBounds(fonts, stats);
            if (CheckCollisionPointRec(pos, b)) {
                return it->get();
            }
        }
    }
    return nullptr;
}

Widget* WidgetManager::FindWidget(const std::string& id) const {
    for (const auto& w : m_widgets) {
        if (w->GetId() == id) return w.get();
    }
    return nullptr;
}

void WidgetManager::SelectWidget(Widget* w, bool multiSelect) {
    if (!multiSelect) {
        m_selectedWidgets.clear();
        if (w) {
            m_selectedWidgets.push_back(w);
        }
    } else {
        if (!w) return;
        auto it = std::find(m_selectedWidgets.begin(), m_selectedWidgets.end(), w);
        if (it != m_selectedWidgets.end()) {
            m_selectedWidgets.erase(it); // Toggle off if already selected
        } else {
            m_selectedWidgets.push_back(w); // Add to multi-selection
        }
    }
}

void WidgetManager::SelectAll() {
    m_selectedWidgets.clear();
    for (const auto& w : m_widgets) {
        if (w->IsEnabled()) {
            m_selectedWidgets.push_back(w.get());
        }
    }
}

bool WidgetManager::IsSelected(const Widget* w) const {
    for (const auto* sel : m_selectedWidgets) {
        if (sel == w) return true;
    }
    return false;
}

void WidgetManager::StartDrag(Vector2 mousePos) {
    if (m_selectedWidgets.empty()) return;
    m_isDragging = true;
    m_dragStartMouse = mousePos;
    m_dragStartPositions.clear();
    for (Widget* w : m_selectedWidgets) {
        m_dragStartPositions.emplace_back(w, w->GetPos());
    }
    m_snapXActive = false;
    m_snapYActive = false;
}

void WidgetManager::UpdateDrag(Vector2 mousePos) {
    if (!m_isDragging || m_selectedWidgets.empty() || m_dragStartPositions.empty()) return;

    Vector2 rawDelta = { mousePos.x - m_dragStartMouse.x, mousePos.y - m_dragStartMouse.y };

    m_snapXActive = false;
    m_snapYActive = false;

    // Use primary (last selected) widget for alignment snapping
    Widget* primary = m_selectedWidgets.back();
    Vector2 primaryStartPos = m_dragStartPositions.back().second;
    Vector2 primaryTargetPos = { primaryStartPos.x + rawDelta.x, primaryStartPos.y + rawDelta.y };

    const float snapThreshold = 6.0f;

    // Snapping against non-selected enabled widgets
    for (const auto& w : m_widgets) {
        if (IsSelected(w.get()) || !w->IsEnabled()) continue;

        Vector2 other = w->GetPos();

        // Snap X center
        if (std::abs(primaryTargetPos.x - other.x) < snapThreshold) {
            rawDelta.x = other.x - primaryStartPos.x;
            m_snapXActive = true;
            m_snapX = other.x;
        }

        // Snap Y center
        if (std::abs(primaryTargetPos.y - other.y) < snapThreshold) {
            rawDelta.y = other.y - primaryStartPos.y;
            m_snapYActive = true;
            m_snapY = other.y;
        }
    }

    // Canvas center snapping (480, 240)
    if (std::abs(primaryTargetPos.x - 480.0f) < snapThreshold) {
        rawDelta.x = 480.0f - primaryStartPos.x;
        m_snapXActive = true;
        m_snapX = 480.0f;
    }
    if (std::abs(primaryTargetPos.y - 240.0f) < snapThreshold) {
        rawDelta.y = 240.0f - primaryStartPos.y;
        m_snapYActive = true;
        m_snapY = 240.0f;
    }

    // Move all selected widgets synchronously by rawDelta
    for (const auto& item : m_dragStartPositions) {
        item.first->SetPos(Vector2{ item.second.x + rawDelta.x, item.second.y + rawDelta.y });
    }
}

void WidgetManager::EndDrag() {
    m_isDragging = false;
    m_dragStartPositions.clear();
    m_snapXActive = false;
    m_snapYActive = false;
}

WidgetResizeHandle WidgetManager::HitTestHandle(Vector2 mousePos, const FontCollection& fonts, const HardwareStats& stats) const {
    if (m_selectedWidgets.empty()) {
        m_handleTargetWidget = nullptr;
        return WidgetResizeHandle::None;
    }

    const float hs = 12.0f;
    const float hs_half = hs * 0.5f;

    // Check selected widgets in reverse order (topmost first)
    for (auto it = m_selectedWidgets.rbegin(); it != m_selectedWidgets.rend(); ++it) {
        Widget* w = *it;
        if (!w || !w->IsEnabled()) continue;

        Rectangle b = w->GetBounds(fonts, stats);

        // Check 4 corners first
        if (CheckCollisionPointRec(mousePos, Rectangle{ b.x - hs_half, b.y - hs_half, hs, hs })) {
            m_handleTargetWidget = w;
            return WidgetResizeHandle::CornerTL;
        }
        if (CheckCollisionPointRec(mousePos, Rectangle{ b.x + b.width - hs_half, b.y - hs_half, hs, hs })) {
            m_handleTargetWidget = w;
            return WidgetResizeHandle::CornerTR;
        }
        if (CheckCollisionPointRec(mousePos, Rectangle{ b.x - hs_half, b.y + b.height - hs_half, hs, hs })) {
            m_handleTargetWidget = w;
            return WidgetResizeHandle::CornerBL;
        }
        if (CheckCollisionPointRec(mousePos, Rectangle{ b.x + b.width - hs_half, b.y + b.height - hs_half, hs, hs })) {
            m_handleTargetWidget = w;
            return WidgetResizeHandle::CornerBR;
        }

        // Check 4 edges (thickness 8px)
        if (CheckCollisionPointRec(mousePos, Rectangle{ b.x + hs_half, b.y - 4.0f, std::max(b.width - hs, 1.0f), 8.0f })) {
            m_handleTargetWidget = w;
            return WidgetResizeHandle::EdgeTop;
        }
        if (CheckCollisionPointRec(mousePos, Rectangle{ b.x + hs_half, b.y + b.height - 4.0f, std::max(b.width - hs, 1.0f), 8.0f })) {
            m_handleTargetWidget = w;
            return WidgetResizeHandle::EdgeBottom;
        }
        if (CheckCollisionPointRec(mousePos, Rectangle{ b.x - 4.0f, b.y + hs_half, 8.0f, std::max(b.height - hs, 1.0f) })) {
            m_handleTargetWidget = w;
            return WidgetResizeHandle::EdgeLeft;
        }
        if (CheckCollisionPointRec(mousePos, Rectangle{ b.x + b.width - 4.0f, b.y + hs_half, 8.0f, std::max(b.height - hs, 1.0f) })) {
            m_handleTargetWidget = w;
            return WidgetResizeHandle::EdgeRight;
        }
    }

    m_handleTargetWidget = nullptr;
    return WidgetResizeHandle::None;
}

void WidgetManager::StartResize(WidgetResizeHandle handle, Vector2 mousePos, const FontCollection& fonts, const HardwareStats& stats) {
    if (m_selectedWidgets.empty() || handle == WidgetResizeHandle::None) return;

    if (!m_handleTargetWidget || !IsSelected(m_handleTargetWidget)) {
        m_handleTargetWidget = m_selectedWidgets.back();
    }

    Widget* target = m_handleTargetWidget;
    m_isResizing = true;
    m_activeResizeHandle = handle;
    m_resizeStartMouse = mousePos;
    m_resizeStartPos = target->GetPos();
    m_resizeStartSize = target->GetSize();
    m_resizeStartBounds = target->GetBounds(fonts, stats);
    m_resizeCenter = Vector2{
        m_resizeStartBounds.x + m_resizeStartBounds.width * 0.5f,
        m_resizeStartBounds.y + m_resizeStartBounds.height * 0.5f
    };

    if (target->GetType() == WidgetType::Visualizer) {
        auto* vis = static_cast<VisualizerWidget*>(target);
        m_resizeStartWidth = vis->GetWidth();
        m_resizeStartHeight = vis->GetHeight();
    }

    m_resizeStartItems.clear();
    for (Widget* w : m_selectedWidgets) {
        if (!w || !w->IsEnabled()) continue;
        ResizeStartItem item;
        item.widget = w;
        item.startSize = w->GetSize();
        item.startPos = w->GetPos();
        if (w->GetType() == WidgetType::Visualizer) {
            auto* vis = static_cast<VisualizerWidget*>(w);
            item.startWidth = vis->GetWidth();
            item.startHeight = vis->GetHeight();
        }
        m_resizeStartItems.push_back(item);
    }
}

void WidgetManager::UpdateResize(Vector2 mousePos) {
    if (!m_isResizing || m_selectedWidgets.empty() || m_activeResizeHandle == WidgetResizeHandle::None) return;

    Widget* target = m_handleTargetWidget;
    if (!target) return;

    float newTargetSize = m_resizeStartSize;

    if (target->GetType() == WidgetType::ArcGauge) {
        // Radial distance from widget center sets arc radius
        Vector2 center = target->GetPos();
        float dist = Vector2Distance(center, mousePos);
        newTargetSize = std::clamp(dist, 15.0f, 220.0f);
        target->SetSize(newTargetSize);
    } else if (target->GetType() == WidgetType::Text) {
        // Vector projection along initial vector from center to mouse
        Vector2 v0 = { m_resizeStartMouse.x - m_resizeCenter.x, m_resizeStartMouse.y - m_resizeCenter.y };
        float len2 = v0.x * v0.x + v0.y * v0.y;
        if (len2 > 4.0f) {
            Vector2 v1 = { mousePos.x - m_resizeCenter.x, mousePos.y - m_resizeCenter.y };
            float proj = (v1.x * v0.x + v1.y * v0.y) / len2;
            newTargetSize = std::clamp(m_resizeStartSize * proj, 8.0f, 100.0f);
            target->SetSize(std::round(newTargetSize));
        }
    } else if (target->GetType() == WidgetType::Visualizer) {
        auto* vis = static_cast<VisualizerWidget*>(target);
        float dx = mousePos.x - m_resizeStartMouse.x;
        float dy = mousePos.y - m_resizeStartMouse.y;

        switch (m_activeResizeHandle) {
            case WidgetResizeHandle::EdgeRight:
            case WidgetResizeHandle::CornerTR:
            case WidgetResizeHandle::CornerBR:
                vis->SetWidth(std::clamp(m_resizeStartWidth + dx, 60.0f, 960.0f));
                break;
            case WidgetResizeHandle::EdgeLeft:
            case WidgetResizeHandle::CornerTL:
            case WidgetResizeHandle::CornerBL: {
                float newW = std::clamp(m_resizeStartWidth - dx, 60.0f, 960.0f);
                float actualDx = m_resizeStartWidth - newW;
                vis->SetPos(Vector2{ m_resizeStartPos.x + actualDx, vis->GetPos().y });
                vis->SetWidth(newW);
                break;
            }
            default: break;
        }

        switch (m_activeResizeHandle) {
            case WidgetResizeHandle::EdgeBottom:
            case WidgetResizeHandle::CornerBL:
            case WidgetResizeHandle::CornerBR:
                vis->SetHeight(std::clamp(m_resizeStartHeight + dy, 20.0f, 300.0f));
                break;
            case WidgetResizeHandle::EdgeTop:
            case WidgetResizeHandle::CornerTL:
            case WidgetResizeHandle::CornerTR: {
                float newH = std::clamp(m_resizeStartHeight - dy, 20.0f, 300.0f);
                float actualDy = m_resizeStartHeight - newH;
                vis->SetPos(Vector2{ vis->GetPos().x, m_resizeStartPos.y + actualDy });
                vis->SetHeight(newH);
                break;
            }
            default: break;
        }
    }

    // If multiple widgets are selected, scale other selected widgets proportionally
    if (m_resizeStartItems.size() > 1 && m_resizeStartSize > 0.001f) {
        float scaleFactor = (target->GetType() == WidgetType::Visualizer)
            ? (static_cast<VisualizerWidget*>(target)->GetWidth() / std::max(m_resizeStartWidth, 1.0f))
            : (newTargetSize / m_resizeStartSize);

        for (const auto& item : m_resizeStartItems) {
            if (item.widget == target || !item.widget) continue;

            if (item.widget->GetType() == WidgetType::ArcGauge) {
                item.widget->SetSize(std::clamp(item.startSize * scaleFactor, 15.0f, 220.0f));
            } else if (item.widget->GetType() == WidgetType::Text) {
                item.widget->SetSize(std::round(std::clamp(item.startSize * scaleFactor, 8.0f, 100.0f)));
            } else if (item.widget->GetType() == WidgetType::Visualizer) {
                auto* v = static_cast<VisualizerWidget*>(item.widget);
                v->SetWidth(std::clamp(item.startWidth * scaleFactor, 60.0f, 960.0f));
                v->SetHeight(std::clamp(item.startHeight * scaleFactor, 20.0f, 300.0f));
            }
        }
    }
}

void WidgetManager::EndResize() {
    m_isResizing = false;
    m_activeResizeHandle = WidgetResizeHandle::None;
    m_handleTargetWidget = nullptr;
    m_resizeStartItems.clear();
}

void WidgetManager::ResizeSelectedByWheel(float wheelDelta) {
    if (m_selectedWidgets.empty() || wheelDelta == 0.0f) return;

    for (Widget* w : m_selectedWidgets) {
        if (w->GetType() == WidgetType::Text) {
            float step = (w->GetSize() >= 24.0f) ? 2.0f : 1.0f;
            float new_sz = w->GetSize() + (wheelDelta > 0.0f ? step : -step);
            w->SetSize(std::clamp(new_sz, 8.0f, 100.0f));
        } else if (w->GetType() == WidgetType::ArcGauge) {
            float step = 2.0f;
            float new_r = w->GetSize() + (wheelDelta > 0.0f ? step : -step);
            w->SetSize(std::clamp(new_r, 15.0f, 220.0f));
        } else if (w->GetType() == WidgetType::Visualizer) {
            auto* vis = static_cast<VisualizerWidget*>(w);
            float factor = (wheelDelta > 0.0f) ? 1.05f : 0.95f;
            vis->SetWidth(std::clamp(vis->GetWidth() * factor, 60.0f, 960.0f));
            vis->SetHeight(std::clamp(vis->GetHeight() * factor, 20.0f, 300.0f));
        }
    }
}

void WidgetManager::NudgeSelected(float dx, float dy) {
    for (Widget* w : m_selectedWidgets) {
        Vector2 p = w->GetPos();
        p.x += dx;
        p.y += dy;
        w->SetPos(p);
    }
}

void WidgetManager::SaveToIni(tortellini::ini& ini) const {
    for (const auto& w : m_widgets) {
        w->SaveToIni(ini);
    }
}

void WidgetManager::LoadFromIni(tortellini::ini& ini) {
    for (const auto& w : m_widgets) {
        w->LoadFromIni(ini);
    }
}

void WidgetManager::ResetAll() {
    for (const auto& w : m_widgets) {
        w->ResetDefaults();
    }
}

bool WidgetManager::DrawManagerWindow(bool* p_open) {
    bool changed = false;
    if (p_open && !*p_open) return false;

    ImGui::SetNextWindowPos(ImVec2(15, 35), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(320, 420), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("Widget Manager", p_open)) {
        if (ImGui::BeginTabBar("Categories")) {
            const char* cats[] = { "All", "CPU", "GPU", "Memory & Storage", "Time & Status", "Audio" };
            for (const char* cat : cats) {
                if (ImGui::BeginTabItem(cat)) {
                    ImGui::BeginChild("WidgetList", ImVec2(0, -35), true);
                    for (const auto& w : m_widgets) {
                        if (strcmp(cat, "All") != 0 && w->GetCategory() != cat) continue;

                        ImGui::PushID(w->GetId().c_str());
                        bool enabled = w->IsEnabled();
                        if (ImGui::Checkbox("##en", &enabled)) {
                            w->SetEnabled(enabled);
                            changed = true;
                        }
                        ImGui::SameLine();

                        bool is_sel = IsSelected(w.get());
                        if (ImGui::Selectable(w->GetName().c_str(), is_sel)) {
                            bool ctrl_held = ImGui::GetIO().KeyCtrl;
                            SelectWidget(w.get(), ctrl_held);
                        }
                        ImGui::PopID();
                    }
                    ImGui::EndChild();
                    ImGui::EndTabItem();
                }
            }
            ImGui::EndTabBar();
        }

        ImGui::Separator();
        if (ImGui::Button("Select All")) {
            SelectAll();
        }
        ImGui::SameLine();
        if (ImGui::Button("Deselect All")) {
            ClearSelection();
        }
        ImGui::SameLine();
        if (ImGui::Button("Enable All")) {
            for (const auto& w : m_widgets) w->SetEnabled(true);
            changed = true;
        }
        ImGui::SameLine();
        if (ImGui::Button("Disable All")) {
            for (const auto& w : m_widgets) w->SetEnabled(false);
            changed = true;
        }
        ImGui::SameLine();
        if (ImGui::Button("Reset Defaults")) {
            ResetAll();
            changed = true;
        }
    }
    ImGui::End();

    return changed;
}

bool WidgetManager::DrawInspectorWindow(bool* p_open, float* p_pan_x, float* p_pan_y, float* p_zoom, int* p_fill_mode) {
    bool changed = false;
    if (p_open && !*p_open) return false;

    ImGui::SetNextWindowPos(ImVec2(625, 35), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(320, 420), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("Properties Inspector", p_open)) {
        if (m_selectedWidgets.size() == 1) {
            Widget* single = m_selectedWidgets[0];
            ImGui::TextColored(ImVec4(0.12f, 0.90f, 0.85f, 1.0f), "%s", single->GetName().c_str());
            ImGui::TextDisabled("Category: %s | ID: %s", single->GetCategory().c_str(), single->GetId().c_str());
            ImGui::Separator();

            if (single->DrawInspectorUI()) {
                changed = true;
            }
        } else if (m_selectedWidgets.size() > 1) {
            ImGui::TextColored(ImVec4(0.12f, 0.90f, 0.85f, 1.0f), "Multi-Selection (%zu widgets)", m_selectedWidgets.size());
            ImGui::TextDisabled("Hold Ctrl and click on canvas to add/remove items");
            ImGui::Separator();

            if (ImGui::Button("Deselect All")) {
                ClearSelection();
            }
            ImGui::SameLine();
            if (ImGui::Button("Disable Selected")) {
                for (Widget* w : m_selectedWidgets) w->SetEnabled(false);
                changed = true;
            }
            ImGui::SameLine();
            if (ImGui::Button("Enable Selected")) {
                for (Widget* w : m_selectedWidgets) w->SetEnabled(true);
                changed = true;
            }

            ImGui::Spacing();
            ImGui::Text("Selected Widgets:");
            ImGui::BeginChild("SelectedList", ImVec2(0, 110), true);
            for (Widget* w : m_selectedWidgets) {
                char dim_buf[64] = "";
                if (w->GetType() == WidgetType::Visualizer) {
                    auto* vis = static_cast<VisualizerWidget*>(w);
                    snprintf(dim_buf, sizeof(dim_buf), "%dx%d", (int)vis->GetWidth(), (int)vis->GetHeight());
                } else if (w->GetType() == WidgetType::ArcGauge) {
                    snprintf(dim_buf, sizeof(dim_buf), "Radius: %d", (int)w->GetSize());
                } else {
                    snprintf(dim_buf, sizeof(dim_buf), "Size: %d", (int)w->GetSize());
                }
                ImGui::BulletText("%s (%s) — %s", w->GetName().c_str(), w->GetCategory().c_str(), dim_buf);
            }
            ImGui::EndChild();

            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Text("Bulk Alignment Tools:");
            if (ImGui::Button("Align Center X")) {
                float avg_x = 0.0f;
                for (Widget* w : m_selectedWidgets) avg_x += w->GetPos().x;
                avg_x /= (float)m_selectedWidgets.size();
                for (Widget* w : m_selectedWidgets) {
                    Vector2 p = w->GetPos();
                    p.x = avg_x;
                    w->SetPos(p);
                }
                changed = true;
            }
            ImGui::SameLine();
            if (ImGui::Button("Align Center Y")) {
                float avg_y = 0.0f;
                for (Widget* w : m_selectedWidgets) avg_y += w->GetPos().y;
                avg_y /= (float)m_selectedWidgets.size();
                for (Widget* w : m_selectedWidgets) {
                    Vector2 p = w->GetPos();
                    p.y = avg_y;
                    w->SetPos(p);
                }
                changed = true;
            }
        } else if (p_pan_x && p_pan_y && p_zoom) {
            ImGui::TextColored(ImVec4(0.12f, 0.90f, 0.85f, 1.0f), "Background Canvas");
            ImGui::TextDisabled("Drag empty canvas to pan | Mouse wheel to zoom");
            ImGui::Separator();

            if (ImGui::DragFloat2("Pan (X, Y)", p_pan_x, 1.0f, -480.0f, 480.0f, "%.1f")) changed = true;
            if (ImGui::SliderFloat("Zoom", p_zoom, 0.2f, 5.0f, "%.2fx")) changed = true;

            if (p_fill_mode) {
                const char* fill_names[] = { "Crop (Cover)", "Letterbox (Fit)", "Adapt (Stretch)" };
                if (ImGui::Combo("Fill Mode", p_fill_mode, fill_names, 3)) changed = true;
            }

            ImGui::Spacing();
            if (ImGui::Button("Reset Pan & Zoom")) {
                *p_pan_x = 0.0f;
                *p_pan_y = 0.0f;
                *p_zoom = 1.0f;
                changed = true;
            }
        } else {
            ImGui::TextDisabled("No widget selected.");
            ImGui::TextWrapped("Click any widget on the canvas, or select one from the Widget Manager to inspect and edit its properties.");
        }
    }
    ImGui::End();

    return changed;
}
