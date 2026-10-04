#pragma once

#include "raylib.h"
#include "hw_monitor.hpp"
#include "tortellini.hh"
#include <string>
#include <vector>
#include <memory>
#include <functional>

// Font manager for high-DPI Segoe UI / Arial typography
class FontCollection {
public:
    Font font_large = {0};
    Font font_medium = {0};
    Font font_small = {0};
    bool has_custom = false;

    void Load();
    void Unload();
    Font GetFontForSize(float size) const;
};

// Render context passed during drawing
struct RenderContext {
    const HardwareStats& stats;
    const FontCollection& fonts;
    float time_sec = 0.0f;
    bool is_editor_active = false;
};

enum class WidgetType {
    Text,
    ArcGauge,
    Visualizer
};

// Handle locations for interactive widget resizing
enum class WidgetResizeHandle {
    None,
    CornerTL,
    CornerTR,
    CornerBL,
    CornerBR,
    EdgeTop,
    EdgeBottom,
    EdgeLeft,
    EdgeRight
};

// Base polymorphic Widget class
class Widget {
public:
    virtual ~Widget() = default;

    virtual const std::string& GetId() const = 0;
    virtual const std::string& GetName() const = 0;
    virtual const std::string& GetCategory() const = 0;
    virtual WidgetType GetType() const = 0;

    bool IsEnabled() const { return m_enabled; }
    void SetEnabled(bool enabled) { m_enabled = enabled; }

    Vector2 GetPos() const { return m_pos; }
    void SetPos(Vector2 pos) { m_pos = pos; }

    float GetSize() const { return m_size; }
    void SetSize(float size) { m_size = size; }

    Color GetColor() const { return m_color; }
    void SetColor(Color color) { m_color = color; }

    virtual Rectangle GetBounds(const FontCollection& fonts, const HardwareStats& stats) const = 0;
    virtual void Render(const RenderContext& ctx) = 0;
    virtual bool DrawInspectorUI() = 0; // Dear ImGui inspector; returns true if modified

    virtual void SaveToIni(tortellini::ini& ini) const;
    virtual void LoadFromIni(tortellini::ini& ini);
    virtual void ResetDefaults();

protected:
    bool m_enabled = true;
    Vector2 m_pos = {0, 0};
    float m_size = 20.0f;
    Color m_color = WHITE;

    bool m_defEnabled = true;
    Vector2 m_defPos = {0, 0};
    float m_defSize = 20.0f;
    Color m_defColor = WHITE;
};

// Text Widget for labels, telemetry numbers, clock, fps, etc.
class TextWidget : public Widget {
public:
    using TextSupplier = std::function<std::string(const HardwareStats&)>;

    TextWidget(std::string id, std::string name, std::string category,
               Vector2 defPos, float defSize, Color defColor, bool defEnabled,
               TextSupplier supplier, bool isTemp = false, bool centerX = true, bool centerY = true);

    const std::string& GetId() const override { return m_id; }
    const std::string& GetName() const override { return m_name; }
    const std::string& GetCategory() const override { return m_category; }
    WidgetType GetType() const override { return WidgetType::Text; }

    Rectangle GetBounds(const FontCollection& fonts, const HardwareStats& stats) const override;
    void Render(const RenderContext& ctx) override;
    bool DrawInspectorUI() override;

    void SetCustomText(const std::string& text) { m_customText = text; }
    const std::string& GetCustomText() const { return m_customText; }
    void SetUseCustomText(bool use) { m_useCustomText = use; }
    bool GetUseCustomText() const { return m_useCustomText; }

    void SaveToIni(tortellini::ini& ini) const override;
    void LoadFromIni(tortellini::ini& ini) override;
    void ResetDefaults() override;

private:
    std::string m_id;
    std::string m_name;
    std::string m_category;
    TextSupplier m_supplier;
    bool m_isTemp = false;
    bool m_centerX = true;
    bool m_centerY = true;

    bool m_useCustomText = false;
    std::string m_customText;
};

// Arc Gauge Widget (Circle Arc or Dome Arc)
enum class ArcType {
    FullCircle,
    Dome
};

class ArcGaugeWidget : public Widget {
public:
    using ValueSupplier = std::function<float(const HardwareStats&)>;

    ArcGaugeWidget(std::string id, std::string name, std::string category,
                   Vector2 defPos, float defRadius, Color defColor, bool defEnabled,
                   ArcType arcType, ValueSupplier supplier, float minVal = 0.0f, float maxVal = 100.0f);

    const std::string& GetId() const override { return m_id; }
    const std::string& GetName() const override { return m_name; }
    const std::string& GetCategory() const override { return m_category; }
    WidgetType GetType() const override { return WidgetType::ArcGauge; }

    Rectangle GetBounds(const FontCollection& fonts, const HardwareStats& stats) const override;
    void Render(const RenderContext& ctx) override;
    bool DrawInspectorUI() override;

    void SaveToIni(tortellini::ini& ini) const override;
    void LoadFromIni(tortellini::ini& ini) override;
    void ResetDefaults() override;

private:
    std::string m_id;
    std::string m_name;
    std::string m_category;
    ArcType m_arcType;
    ValueSupplier m_supplier;
    float m_minVal = 0.0f;
    float m_maxVal = 100.0f;
};

// CAVA Audio Visualizer Widget
class VisualizerWidget : public Widget {
public:
    VisualizerWidget(std::string id, std::string name, std::string category,
                     Vector2 defPos, float defW, float defH, int defBars, Color defColor, bool defEnabled);

    const std::string& GetId() const override { return m_id; }
    const std::string& GetName() const override { return m_name; }
    const std::string& GetCategory() const override { return m_category; }
    WidgetType GetType() const override { return WidgetType::Visualizer; }

    Rectangle GetBounds(const FontCollection& fonts, const HardwareStats& stats) const override;
    void Render(const RenderContext& ctx) override;
    bool DrawInspectorUI() override;

    void SaveToIni(tortellini::ini& ini) const override;
    void LoadFromIni(tortellini::ini& ini) override;
    void ResetDefaults() override;

    float GetWidth() const { return m_width; }
    void SetWidth(float w) { m_width = w; }
    float GetHeight() const { return m_height; }
    void SetHeight(float h) { m_height = h; }
    int GetBars() const { return m_bars; }
    void SetBars(int b) { m_bars = b; }
    int GetStyle() const { return m_style; }
    void SetStyle(int s) { m_style = s; }

private:
    std::string m_id;
    std::string m_name;
    std::string m_category;
    float m_width = 520.0f;
    float m_height = 75.0f;
    int m_bars = 24;
    int m_style = 0; // 0: rounded bars, 1: sharp bars, 2: lines

    float m_defWidth = 520.0f;
    float m_defHeight = 75.0f;
    int m_defBars = 24;
    int m_defStyle = 0;
};

// Widget Manager encapsulating the full list of widgets
class WidgetManager {
public:
    WidgetManager();

    void InitDefaultWidgets();
    void RenderAll(const RenderContext& ctx);
    void RenderEditorOverlay(const RenderContext& ctx);

    Widget* HitTest(Vector2 pos, const FontCollection& fonts, const HardwareStats& stats);
    Widget* FindWidget(const std::string& id) const;
    const std::vector<std::unique_ptr<Widget>>& GetWidgets() const { return m_widgets; }

    // Selection & Multi-Selection (Ctrl + Click)
    Widget* GetSelectedWidget() const { return m_selectedWidgets.empty() ? nullptr : m_selectedWidgets.back(); }
    const std::vector<Widget*>& GetSelectedWidgets() const { return m_selectedWidgets; }
    void SelectWidget(Widget* w, bool multiSelect = false);
    void ClearSelection() { m_selectedWidgets.clear(); }
    void SelectAll();
    bool IsSelected(const Widget* w) const;
    bool HasSelection() const { return !m_selectedWidgets.empty(); }
    size_t GetSelectedCount() const { return m_selectedWidgets.size(); }

    // Interactive Dragging & Snapping (supports moving multiple widgets together)
    void StartDrag(Vector2 mousePos);
    void UpdateDrag(Vector2 mousePos);
    void EndDrag();
    bool IsDragging() const { return m_isDragging; }

    // Interactive Resizing & Handles
    WidgetResizeHandle HitTestHandle(Vector2 mousePos, const FontCollection& fonts, const HardwareStats& stats) const;
    void StartResize(WidgetResizeHandle handle, Vector2 mousePos, const FontCollection& fonts, const HardwareStats& stats);
    void UpdateResize(Vector2 mousePos);
    void EndResize();
    bool IsResizing() const { return m_isResizing; }
    WidgetResizeHandle GetActiveResizeHandle() const { return m_activeResizeHandle; }
    void ResizeSelectedByWheel(float wheelDelta);

    // Arrow keys nudge
    void NudgeSelected(float dx, float dy);

    // Persistence
    void SaveToIni(tortellini::ini& ini) const;
    void LoadFromIni(tortellini::ini& ini);
    void ResetAll();

    // Dear ImGui Inspector & Widget Manager Window
    bool DrawManagerWindow(bool* p_open = nullptr);
    bool DrawInspectorWindow(bool* p_open = nullptr, float* p_pan_x = nullptr, float* p_pan_y = nullptr, float* p_zoom = nullptr, int* p_fill_mode = nullptr);

private:
    std::vector<std::unique_ptr<Widget>> m_widgets;
    std::vector<Widget*> m_selectedWidgets;
    bool m_isDragging = false;
    Vector2 m_dragStartMouse = {0, 0};
    std::vector<std::pair<Widget*, Vector2>> m_dragStartPositions;

    // Resizing state
    bool m_isResizing = false;
    WidgetResizeHandle m_activeResizeHandle = WidgetResizeHandle::None;
    Vector2 m_resizeStartMouse = {0, 0};
    Vector2 m_resizeStartPos = {0, 0};
    float m_resizeStartSize = 0.0f;
    float m_resizeStartWidth = 0.0f;
    float m_resizeStartHeight = 0.0f;
    Rectangle m_resizeStartBounds = {0, 0, 0, 0};
    Vector2 m_resizeCenter = {0, 0};

    struct ResizeStartItem {
        Widget* widget = nullptr;
        float startSize = 0.0f;
        float startWidth = 0.0f;
        float startHeight = 0.0f;
        Vector2 startPos = {0, 0};
    };
    std::vector<ResizeStartItem> m_resizeStartItems;
    mutable Widget* m_handleTargetWidget = nullptr;

    // Dynamic snap alignment guides
    bool m_snapXActive = false;
    float m_snapX = 0.0f;
    bool m_snapYActive = false;
    float m_snapY = 0.0f;
};
