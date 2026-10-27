#include "raylib.h"
#include "rlgl.h"
#include "rlImGui.h"
#include "imgui.h"

#include "widgets.hpp"
#include "config_manager.hpp"
#include "undo_manager.hpp"
#include "hw_monitor.hpp"
#include "jl_panel.hpp"
#include "tray_manager.hpp"
#include "audio_visualizer.hpp"
#include "video_player.hpp"
#include "window_streamer.hpp"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <memory>
#include <algorithm>

// Atmospheric starry particle background for procedural fallback
static constexpr int STAR_COUNT = 64;
struct Star {
    float x, y;
    float speed;
    float size;
    float phase;
};
static Star s_stars[STAR_COUNT];

static void InitStars() {
    for (int i = 0; i < STAR_COUNT; i++) {
        s_stars[i].x = (float)(rand() % 960);
        s_stars[i].y = (float)(rand() % 480);
        s_stars[i].speed = 0.5f + (float)(rand() % 100) / 70.0f;
        s_stars[i].size = 1.0f + (float)(rand() % 100) / 60.0f;
        s_stars[i].phase = (float)(rand() % 100) / 15.0f;
    }
}

static void DrawProceduralBackground(float time_sec) {
    // Twilight cosmic gradient
    DrawRectangleGradientV(0, 0, 960, 480, Color{ 12, 32, 70, 255 }, Color{ 55, 135, 225, 255 });

    // Nebulae glow
    DrawCircleGradient(480, 240, 320, Color{ 80, 180, 255, 45 }, Color{ 0, 0, 0, 0 });
    DrawCircleGradient(200, 160, 220, Color{ 100, 210, 255, 30 }, Color{ 0, 0, 0, 0 });
    DrawCircleGradient(780, 160, 220, Color{ 100, 210, 255, 30 }, Color{ 0, 0, 0, 0 });

    // Twinkling stars
    for (int i = 0; i < STAR_COUNT; i++) {
        float alpha_pulse = (sinf(time_sec * s_stars[i].speed + s_stars[i].phase) + 1.0f) * 0.5f;
        Color star_col = Color{ 230, 245, 255, (uint8_t)(140 + alpha_pulse * 115) };
        DrawCircle((int)s_stars[i].x, (int)s_stars[i].y, s_stars[i].size, star_col);
    }
}

static void DrawToastNotification(const UndoManager& undo) {
    float timer = undo.GetToastTimer();
    const std::string& text = undo.GetToastText();
    if (timer <= 0.0f || text.empty()) return;

    float alpha = 1.0f;
    if (timer < 0.5f) {
        alpha = timer / 0.5f;
    }

    int tw = MeasureText(text.c_str(), 14);
    float box_w = (float)tw + 32.0f;
    float box_h = 28.0f;
    float box_x = (960.0f - box_w) * 0.5f;
    float box_y = 38.0f;

    Rectangle box = { box_x, box_y, box_w, box_h };
    Color bg = { 12, 24, 40, (uint8_t)(235 * alpha) };
    Color border = { 30, 230, 215, (uint8_t)(255 * alpha) };
    Color txt = { 220, 245, 255, (uint8_t)(255 * alpha) };

    DrawRectangleRounded(box, 0.45f, 6, bg);
    DrawRectangleRoundedLines(box, 0.45f, 6, border);
    DrawText(text.c_str(), (int)box_x + 16, (int)box_y + 7, 14, txt);
}

// Forward declaration for static callback
class App;
static App* s_appInstance = nullptr;

class App {
public:
    App() = default;
    ~App() { Shutdown(); }

    bool Init(int argc, char** argv) {
        s_appInstance = this;

        // Command line flags
        for (int i = 1; i < argc; i++) {
            if (strcmp(argv[i], "--no-panel") == 0) {
                m_noPanel = true;
            } else if (strcmp(argv[i], "--editor") == 0) {
                m_editorActive = true;
            } else if (strcmp(argv[i], "--minimized") == 0 || strcmp(argv[i], "--minimize") == 0 || strcmp(argv[i], "-m") == 0) {
                m_startMinimized = true;
            } else if (strcmp(argv[i], "--screenshot") == 0 && i + 1 < argc) {
                m_screenshotPath = argv[++i];
            }
        }

        // Initialize raylib window
        SetConfigFlags(FLAG_WINDOW_RESIZABLE | FLAG_VSYNC_HINT | (m_startMinimized ? FLAG_WINDOW_HIDDEN : 0));
        InitWindow(m_cfg.window_width, m_cfg.window_height, m_editorActive ? "JL LCD AIO - [EDIT MODE] - Hardware Monitor & Visualizer" : "JL LCD AIO - Hardware Monitor & Visualizer");
        SetWindowMinSize(480, 240);
        SetTargetFPS(60);

        InitStars();
        m_fonts.Load();

        // Offscreen 960x480 render target for LCD Panel
        m_renderTarget = LoadRenderTexture(960, 480);
        SetTextureFilter(m_renderTarget.texture, TEXTURE_FILTER_BILINEAR);

        // Frame buffer for physical display transfer
        m_panelPixels = (uint8_t*)malloc(960 * 480 * 4);

        // Load configuration and widgets
        ConfigManager::Load(m_cfg, m_widgets);

        // Setup Dear ImGui
        rlImGuiSetup(true);
        ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

        // Apply dark futuristic theme accents
        ImGuiStyle& style = ImGui::GetStyle();
        style.WindowRounding = 6.0f;
        style.FrameRounding = 4.0f;
        style.Colors[ImGuiCol_TitleBg] = ImVec4(0.06f, 0.12f, 0.20f, 1.0f);
        style.Colors[ImGuiCol_TitleBgActive] = ImVec4(0.08f, 0.18f, 0.30f, 1.0f);
        style.Colors[ImGuiCol_Header] = ImVec4(0.10f, 0.26f, 0.40f, 0.7f);
        style.Colors[ImGuiCol_HeaderHovered] = ImVec4(0.12f, 0.35f, 0.55f, 0.8f);
        style.Colors[ImGuiCol_Button] = ImVec4(0.08f, 0.22f, 0.35f, 0.8f);
        style.Colors[ImGuiCol_ButtonHovered] = ImVec4(0.12f, 0.32f, 0.50f, 1.0f);
        style.Colors[ImGuiCol_CheckMark] = ImVec4(0.12f, 0.90f, 0.85f, 1.0f);
        style.Colors[ImGuiCol_SliderGrab] = ImVec4(0.12f, 0.90f, 0.85f, 1.0f);

        // Hardware Monitor telemetry
        m_hwMon.Init();

        // Audio Visualizer loopback capture
        audio_visualizer_init();

        // JL LCD Panel serial USB connection
        if (!m_noPanel) {
            m_panel.Open();
            m_panel.SetBrightness(m_cfg.brightness);
            m_panel.SetJpegQuality(m_cfg.jpeg_quality);
        }

        // System tray icon
        m_tray.Init(GetWindowHandle(), [](const std::string& path) {
            if (s_appInstance && !path.empty()) {
                s_appInstance->LoadMedia(path);
            }
        });
        if (m_cfg.brightness == 0) {
            SetDisplaySleep(true);
        }

        // Load background media
        LoadMedia(m_cfg.background_path);

        // Save initial clean snapshot in Undo history
        m_undo.Push(ConfigManager::SerializeToString(m_cfg, m_widgets), "Initial State");

        if (m_startMinimized) {
            m_tray.MinimizeWindow();
        }

        return true;
    }

    void Run() {
        int frame_count = 0;

        while (!WindowShouldClose()) {
            float dt = GetFrameTime();
            if (dt > 0.1f) dt = 0.1f;

            // Handle system tray and sleep / wake
            m_tray.Update();
            if (m_tray.ConsumeToggleDisplay()) {
                ToggleDisplaySleep();
            }

            // Process user input and shortcuts
            ProcessInput(dt);

            // Update telemetry metrics
            UpdateTelemetry(dt);

            // Update video or window stream frames
            UpdateBackground(dt);

            // Render UI to 960x480 offscreen surface
            RenderOffscreenTarget();

            // Render to desktop window with scaling & Dear ImGui
            RenderWindow();

            // Send frame to physical panel
            PushFrameToPanel(dt);

            // Automated screenshot taking (for testing)
            if (!m_screenshotPath.empty() && ++frame_count >= 5) {
                TakeScreenshot(m_screenshotPath.c_str());
                break;
            }
        }
    }

    void Shutdown() {
        ConfigManager::Save(m_cfg, m_widgets);

        VideoCache::Cancel();
        m_tray.Shutdown();
        m_streamer.Shutdown();
        audio_visualizer_shutdown();
        m_hwMon.Shutdown();
        m_panel.Close();

        if (m_vp) {
            m_vp->Close();
            m_vp.reset();
        }

        if (m_mediaTexture.id > 0) {
            UnloadTexture(m_mediaTexture);
            m_mediaTexture = {0};
        }

        if (m_streamTexture.id > 0) {
            UnloadTexture(m_streamTexture);
            m_streamTexture = {0};
        }

        if (m_renderTarget.id > 0) {
            UnloadRenderTexture(m_renderTarget);
            m_renderTarget = {0};
        }

        if (m_panelPixels) {
            free(m_panelPixels);
            m_panelPixels = nullptr;
        }

        m_fonts.Unload();
        rlImGuiShutdown();
        CloseWindow();
    }

    void LoadMedia(const std::string& path) {
        if (path.empty()) return;

        if (m_vp) {
            m_vp->Close();
            m_vp.reset();
        }
        if (m_mediaTexture.id > 0) {
            UnloadTexture(m_mediaTexture);
            m_mediaTexture = {0};
        }

        m_cfg.background_path = path;
        m_cachedVideoPath.clear();
        m_isOptimizingVideo = false;
        m_playingCachedVideo = false;

        // Check if video file
        const char* dot = strrchr(path.c_str(), '.');
        bool is_video = false;
        if (dot) {
            if (_stricmp(dot, ".mp4") == 0 || _stricmp(dot, ".webm") == 0 ||
                _stricmp(dot, ".avi") == 0 || _stricmp(dot, ".mkv") == 0 ||
                _stricmp(dot, ".mov") == 0) {
                is_video = true;
            }
        }

        if (is_video) {
            // Check if optimized video cache is available or needs background transcoding
            if (VideoCache::GetOptimizedPath(path, m_cachedVideoPath)) {
                // Optimized 960x480 @ 30 FPS cache already exists in cache/
            } else if (VideoCache::NeedsOptimization(path)) {
                VideoCache::StartBackground(path);
                m_isOptimizingVideo = true;
            }

            m_vp = std::make_unique<VideoPlayer>();
            if (m_vp->Open(path)) {
                int vw = 0, vh = 0;
                const uint8_t* px = m_vp->GetPixels(&vw, &vh);
                if (px && vw > 0 && vh > 0) {
                    Image img = {
                        .data = (void*)px,
                        .width = vw,
                        .height = vh,
                        .mipmaps = 1,
                        .format = PIXELFORMAT_UNCOMPRESSED_R8G8B8A8
                    };
                    m_mediaTexture = LoadTextureFromImage(img);
                    SetTextureFilter(m_mediaTexture, TEXTURE_FILTER_BILINEAR);
                }
            } else {
                m_vp.reset();
            }
        } else {
            // Still image (PNG, JPG, BMP)
            if (FileExists(path.c_str())) {
                m_mediaTexture = LoadTexture(path.c_str());
                SetTextureFilter(m_mediaTexture, TEXTURE_FILTER_BILINEAR);
            }
        }
    }

    void SetDisplaySleep(bool sleep) {
        m_tray.SetDisplayDisabled(sleep);
        if (m_panel.IsOpen()) {
            if (sleep) {
                // 1. Push a solid black frame to clear the LCD panel memory
                if (m_panelPixels) {
                    memset(m_panelPixels, 0, 960 * 480 * 4);
                    m_panel.PushSurfaceAsync(m_panelPixels, 960, 480);
                    TrayManager::Sleep(40);
                }
                // 2. Set backlight to 0 (sleep / turn off) and send close/standby command
                m_panel.SetBrightness(0);
                // 3. Pause frame transmission in worker thread
                m_panel.Show(false);
            } else {
                // 1. Re-enable panel showing
                m_panel.Show(true);
                // 2. Restore backlight
                int target_b = (m_cfg.brightness > 0) ? m_cfg.brightness : 100;
                m_panel.SetBrightness(target_b);
            }
        }
    }

    void ToggleDisplaySleep() {
        SetDisplaySleep(!m_tray.IsDisplayDisabled());
    }

private:
    void SetEditorActive(bool active) {
        if (m_editorActive == active) return;
        m_editorActive = active;
        if (!m_editorActive) {
            ConfigManager::Save(m_cfg, m_widgets);
            SetWindowTitle("JL LCD AIO - Hardware Monitor & Visualizer");
        } else {
            SetWindowTitle("JL LCD AIO - [EDIT MODE] - Hardware Monitor & Visualizer");
        }
    }

    void PushUndoState(const std::string& action_name) {
        std::string snapshot = ConfigManager::SerializeToString(m_cfg, m_widgets);
        m_undo.Push(snapshot, action_name);
    }

    void ProcessInput(float dt) {
        m_undo.Update(dt);

        ImGuiIO& io = ImGui::GetIO();

        // Shortcuts when typing in ImGui is not active
        if (!io.WantCaptureKeyboard) {
            // Toggle Layout Editor with [E]
            if (IsKeyPressed(KEY_E)) {
                SetEditorActive(!m_editorActive);
            }

            // Undo / Redo
            bool ctrl = IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL);
            bool shift = IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT);

            if (ctrl && !shift && IsKeyPressed(KEY_Z)) {
                std::string prev;
                if (m_undo.Undo(prev)) {
                    ConfigManager::DeserializeFromString(prev, m_cfg, m_widgets);
                }
            } else if (ctrl && (IsKeyPressed(KEY_Y) || (shift && IsKeyPressed(KEY_Z)))) {
                std::string next;
                if (m_undo.Redo(next)) {
                    ConfigManager::DeserializeFromString(next, m_cfg, m_widgets);
                }
            }

            // Deselect with [Escape]
            if (IsKeyPressed(KEY_ESCAPE)) {
                m_widgets.ClearSelection();
            }

            // Toggle Visualizer with [M]
            if (IsKeyPressed(KEY_M)) {
                if (auto* vis = m_widgets.FindWidget("visualizer")) {
                    vis->SetEnabled(!vis->IsEnabled());
                    PushUndoState(vis->IsEnabled() ? "Enable Visualizer" : "Disable Visualizer");
                }
            }

            // Toggle Display Standby with [D]
            if (IsKeyPressed(KEY_D)) {
                ToggleDisplaySleep();
            }

            // File dialog with [B]
            if (IsKeyPressed(KEY_B)) {
                OpenMediaFileDialog();
            }

            // Window stream menu with [S]
            if (IsKeyPressed(KEY_S)) {
                m_streamer.ShowPickerMenu(GetWindowHandle());
            }

            // Nudge selected widget or background with arrow keys
            float nudge = shift ? 5.0f : 1.0f;
            if (m_widgets.GetSelectedWidget()) {
                if (IsKeyPressed(KEY_LEFT) || IsKeyPressedRepeat(KEY_LEFT)) {
                    m_widgets.NudgeSelected(-nudge, 0.0f);
                    PushUndoState("Nudge Left");
                }
                if (IsKeyPressed(KEY_RIGHT) || IsKeyPressedRepeat(KEY_RIGHT)) {
                    m_widgets.NudgeSelected(nudge, 0.0f);
                    PushUndoState("Nudge Right");
                }
                if (IsKeyPressed(KEY_UP) || IsKeyPressedRepeat(KEY_UP)) {
                    m_widgets.NudgeSelected(0.0f, -nudge);
                    PushUndoState("Nudge Up");
                }
                if (IsKeyPressed(KEY_DOWN) || IsKeyPressedRepeat(KEY_DOWN)) {
                    m_widgets.NudgeSelected(0.0f, nudge);
                    PushUndoState("Nudge Down");
                }
            } else if (m_editorActive) {
                // Background is selected: nudge background pan
                if (IsKeyPressed(KEY_LEFT) || IsKeyPressedRepeat(KEY_LEFT)) {
                    m_cfg.bg_pan_x -= nudge;
                    PushUndoState("Pan Background Left");
                }
                if (IsKeyPressed(KEY_RIGHT) || IsKeyPressedRepeat(KEY_RIGHT)) {
                    m_cfg.bg_pan_x += nudge;
                    PushUndoState("Pan Background Right");
                }
                if (IsKeyPressed(KEY_UP) || IsKeyPressedRepeat(KEY_UP)) {
                    m_cfg.bg_pan_y -= nudge;
                    PushUndoState("Pan Background Up");
                }
                if (IsKeyPressed(KEY_DOWN) || IsKeyPressedRepeat(KEY_DOWN)) {
                    m_cfg.bg_pan_y += nudge;
                    PushUndoState("Pan Background Down");
                }
            }

            // Disable / Delete selected widgets
            if (IsKeyPressed(KEY_DELETE) || IsKeyPressed(KEY_BACKSPACE)) {
                if (m_widgets.HasSelection()) {
                    for (Widget* w : m_widgets.GetSelectedWidgets()) {
                        w->SetEnabled(false);
                    }
                    PushUndoState("Disable Selected Widgets");
                }
            }

            // Deselect with Escape
            if (IsKeyPressed(KEY_ESCAPE)) {
                m_widgets.ClearSelection();
            }

            // Select All with Ctrl+A
            if (ctrl && IsKeyPressed(KEY_A)) {
                m_widgets.SelectAll();
            }
        }

        // Direct canvas mouse interaction (when mouse is not captured by ImGui)
        if (!io.WantCaptureMouse) {
            Vector2 mouse = GetMousePosition();
            // Transform window coordinates to 960x480 canvas space
            Vector2 canvasMouse = {
                (mouse.x - m_viewX) / m_viewScale,
                (mouse.y - m_viewY) / m_viewScale
            };

            bool in_canvas = (canvasMouse.x >= 0.0f && canvasMouse.x <= 960.0f &&
                              canvasMouse.y >= 0.0f && canvasMouse.y <= 480.0f);

            // Left-click or right-click canvas interaction
            if (m_editorActive) {
                // Update cursor shape based on hovered resize handles or widgets
                if (!m_widgets.IsResizing() && !m_widgets.IsDragging() && !m_isDraggingBackground) {
                    if (in_canvas) {
                        WidgetResizeHandle hoveredHandle = m_widgets.HitTestHandle(canvasMouse, m_fonts, m_hwMon.GetStats());
                        switch (hoveredHandle) {
                            case WidgetResizeHandle::CornerTL:
                            case WidgetResizeHandle::CornerBR:
                                SetMouseCursor(MOUSE_CURSOR_RESIZE_NWSE);
                                break;
                            case WidgetResizeHandle::CornerTR:
                            case WidgetResizeHandle::CornerBL:
                                SetMouseCursor(MOUSE_CURSOR_RESIZE_NESW);
                                break;
                            case WidgetResizeHandle::EdgeLeft:
                            case WidgetResizeHandle::EdgeRight:
                                SetMouseCursor(MOUSE_CURSOR_RESIZE_EW);
                                break;
                            case WidgetResizeHandle::EdgeTop:
                            case WidgetResizeHandle::EdgeBottom:
                                SetMouseCursor(MOUSE_CURSOR_RESIZE_NS);
                                break;
                            default: {
                                Widget* hoveredWidget = m_widgets.HitTest(canvasMouse, m_fonts, m_hwMon.GetStats());
                                if (hoveredWidget) {
                                    SetMouseCursor(MOUSE_CURSOR_RESIZE_ALL);
                                } else {
                                    SetMouseCursor(MOUSE_CURSOR_DEFAULT);
                                }
                                break;
                            }
                        }
                    } else {
                        SetMouseCursor(MOUSE_CURSOR_DEFAULT);
                    }
                }

                // 1. Mouse Press
                if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
                    bool ctrl_held = IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL);
                    WidgetResizeHandle handle = m_widgets.HitTestHandle(canvasMouse, m_fonts, m_hwMon.GetStats());

                    if (handle != WidgetResizeHandle::None && !ctrl_held) {
                        // Clicked on a resize handle of the currently single-selected widget
                        m_widgets.StartResize(handle, canvasMouse, m_fonts, m_hwMon.GetStats());
                        m_isDraggingBackground = false;
                    } else {
                        Widget* hit = m_widgets.HitTest(canvasMouse, m_fonts, m_hwMon.GetStats());
                        if (ctrl_held) {
                            // Multi-selection: toggle selection with Ctrl+Click
                            if (hit) {
                                m_widgets.SelectWidget(hit, true /* multiSelect */);
                                if (m_widgets.IsSelected(hit)) {
                                    m_widgets.StartDrag(canvasMouse);
                                }
                                m_isDraggingBackground = false;
                            }
                        } else {
                            if (hit) {
                                // If the clicked widget is already in a multi-selection group, preserve selection and drag all!
                                if (!m_widgets.IsSelected(hit)) {
                                    m_widgets.SelectWidget(hit, false);
                                }
                                m_widgets.StartDrag(canvasMouse);
                                m_isDraggingBackground = false;
                            } else if (in_canvas) {
                                // Clicked on empty canvas: clear selection and pan background
                                m_widgets.ClearSelection();
                                m_isDraggingBackground = true;
                                m_bgDragStartMouse = canvasMouse;
                                m_bgDragStartPan = Vector2{ m_cfg.bg_pan_x, m_cfg.bg_pan_y };
                            }
                        }
                    }
                } else if (IsMouseButtonPressed(MOUSE_BUTTON_RIGHT) && in_canvas) {
                    // Right-click drag also conveniently pans background
                    m_isDraggingBackground = true;
                    m_bgDragStartMouse = canvasMouse;
                    m_bgDragStartPan = Vector2{ m_cfg.bg_pan_x, m_cfg.bg_pan_y };
                }

                // 2. Mouse Dragging
                if (IsMouseButtonDown(MOUSE_BUTTON_LEFT) || IsMouseButtonDown(MOUSE_BUTTON_RIGHT)) {
                    if (m_widgets.IsResizing()) {
                        m_widgets.UpdateResize(canvasMouse);
                    } else if (m_widgets.IsDragging()) {
                        m_widgets.UpdateDrag(canvasMouse);
                    } else if (m_isDraggingBackground) {
                        float dx = canvasMouse.x - m_bgDragStartMouse.x;
                        float dy = canvasMouse.y - m_bgDragStartMouse.y;
                        float new_x = m_bgDragStartPan.x + dx;
                        float new_y = m_bgDragStartPan.y + dy;

                        m_snapBgX = false;
                        m_snapBgY = false;
                        if (std::abs(new_x) < 8.0f) {
                            new_x = 0.0f;
                            m_snapBgX = true;
                        }
                        if (std::abs(new_y) < 8.0f) {
                            new_y = 0.0f;
                            m_snapBgY = true;
                        }

                        m_cfg.bg_pan_x = new_x;
                        m_cfg.bg_pan_y = new_y;
                    }
                }

                // 3. Mouse Release
                if (IsMouseButtonReleased(MOUSE_BUTTON_LEFT) || IsMouseButtonReleased(MOUSE_BUTTON_RIGHT)) {
                    if (m_widgets.IsResizing()) {
                        m_widgets.EndResize();
                        PushUndoState("Resize Widget");
                        SetMouseCursor(MOUSE_CURSOR_DEFAULT);
                    } else if (m_widgets.IsDragging()) {
                        m_widgets.EndDrag();
                        PushUndoState("Move Widgets");
                    } else if (m_isDraggingBackground) {
                        m_isDraggingBackground = false;
                        m_snapBgX = false;
                        m_snapBgY = false;
                        if (m_cfg.bg_pan_x != m_bgDragStartPan.x || m_cfg.bg_pan_y != m_bgDragStartPan.y) {
                            PushUndoState("Pan Background");
                        }
                    }
                }

                // 4. Mouse Wheel Zoom (Widgets have priority over background)
                if (in_canvas) {
                    float wheel = GetMouseWheelMove();
                    if (wheel != 0.0f) {
                        Widget* hovered = m_widgets.HitTest(canvasMouse, m_fonts, m_hwMon.GetStats());
                        if (hovered && !m_widgets.HasSelection()) {
                            // Hovering over a widget with no prior selection: select and resize it
                            m_widgets.SelectWidget(hovered, false);
                            m_widgets.ResizeSelectedByWheel(wheel);
                            PushUndoState("Resize Widget");
                        } else if (m_widgets.HasSelection()) {
                            // Resize all selected widgets
                            m_widgets.ResizeSelectedByWheel(wheel);
                            PushUndoState("Resize Widgets");
                        } else {
                            // Empty background: zoom background
                            float prev_zoom = m_cfg.bg_zoom;
                            m_cfg.bg_zoom = std::clamp(m_cfg.bg_zoom + wheel * 0.05f, 0.2f, 5.0f);
                            if (std::abs(m_cfg.bg_zoom - 1.0f) < 0.03f) {
                                m_cfg.bg_zoom = 1.0f;
                            }
                            if (m_cfg.bg_zoom != prev_zoom) {
                                PushUndoState("Zoom Background");
                            }
                        }
                    }
                }
            } else {
                // In normal view mode, right-click drag allows panning background
                if (IsMouseButtonPressed(MOUSE_BUTTON_RIGHT) && in_canvas) {
                    m_isDraggingBackground = true;
                    m_bgDragStartMouse = canvasMouse;
                    m_bgDragStartPan = Vector2{ m_cfg.bg_pan_x, m_cfg.bg_pan_y };
                } else if (IsMouseButtonDown(MOUSE_BUTTON_RIGHT) && m_isDraggingBackground) {
                    float dx = canvasMouse.x - m_bgDragStartMouse.x;
                    float dy = canvasMouse.y - m_bgDragStartMouse.y;
                    m_cfg.bg_pan_x = m_bgDragStartPan.x + dx;
                    m_cfg.bg_pan_y = m_bgDragStartPan.y + dy;
                } else if (IsMouseButtonReleased(MOUSE_BUTTON_RIGHT) && m_isDraggingBackground) {
                    m_isDraggingBackground = false;
                    PushUndoState("Pan Background");
                }
            }
        }
    }

    void UpdateTelemetry(float dt) {
        m_hwMon.Update(dt, m_cfg.refresh_interval);
        // Feed live FPS into telemetry stats
        HardwareStats& stats = const_cast<HardwareStats&>(m_hwMon.GetStats());
        stats.fps = GetFPS();
    }

    void UpdateBackground(float dt) {
        if (m_tray.IsDisplayDisabled()) return;

        // 1. Poll for background video optimization transcoding completion
        if (m_isOptimizingVideo) {
            std::string completed;
            if (VideoCache::PollCompleted(completed)) {
                m_cachedVideoPath = completed;
                m_isOptimizingVideo = false;
                m_undo.Push(ConfigManager::SerializeToString(m_cfg, m_widgets), "Video Cache Ready (960x480 @ 30 FPS)");
            }
        }

        // 2. Auto-switch to lightweight video cache on minimize to tray, trim working set
        bool is_minimized_or_hidden = !m_tray.IsWindowVisible() || IsWindowMinimized();
        if (is_minimized_or_hidden && !m_wasWindowHiddenOrMinimized) {
            // Swapping to lightweight cache when minimized to tray
            if (!m_cachedVideoPath.empty() && !m_playingCachedVideo) {
                if (m_vp && m_vp->Open(m_cachedVideoPath)) {
                    m_playingCachedVideo = true;
                }
            }
            // Trim working set memory to absolute minimum
            TrayManager::TrimMemory();
        } else if (!is_minimized_or_hidden && m_wasWindowHiddenOrMinimized) {
            // Restoring window: swap back to high-res original for full quality preview & editing
            if (m_playingCachedVideo && !m_cfg.background_path.empty()) {
                if (m_vp && m_vp->Open(m_cfg.background_path)) {
                    m_playingCachedVideo = false;
                }
            }
        }
        m_wasWindowHiddenOrMinimized = is_minimized_or_hidden;

        // 3. Update active video or window streamer textures
        if (m_streamer.IsActive()) {
            uint8_t* px = nullptr;
            int sw = 0, sh = 0;
            if (m_streamer.CaptureFrame(&px, &sw, &sh) && px) {
                if (m_streamTexture.id == 0 || m_streamTexture.width != sw || m_streamTexture.height != sh) {
                    if (m_streamTexture.id > 0) UnloadTexture(m_streamTexture);
                    Image img = {
                        .data = px,
                        .width = sw,
                        .height = sh,
                        .mipmaps = 1,
                        .format = PIXELFORMAT_UNCOMPRESSED_R8G8B8A8
                    };
                    m_streamTexture = LoadTextureFromImage(img);
                    SetTextureFilter(m_streamTexture, TEXTURE_FILTER_BILINEAR);
                } else {
                    UpdateTexture(m_streamTexture, px);
                }
            }
        } else if (m_vp && m_vp->Update(dt)) {
            int vw = 0, vh = 0;
            const uint8_t* p = m_vp->GetPixels(&vw, &vh);
            if (p && m_mediaTexture.id > 0) {
                UpdateTexture(m_mediaTexture, p);
            }
        }
    }

    void RenderBackgroundLayer() {
        Texture2D* bg = nullptr;
        if (m_streamer.IsActive() && m_streamTexture.id > 0) {
            bg = &m_streamTexture;
        } else if (m_mediaTexture.id > 0) {
            bg = &m_mediaTexture;
        }

        if (!bg || bg->id == 0) {
            DrawProceduralBackground((float)GetTime());
            return;
        }

        float tw = (float)bg->width;
        float th = (float)bg->height;
        if (tw <= 0 || th <= 0) {
            DrawProceduralBackground((float)GetTime());
            return;
        }

        // Full source texture is always used so panning reveals hidden content
        Rectangle src = { 0.0f, 0.0f, tw, th };

        if (m_cfg.fill_mode == FILL_MODE_CROP) { // Center crop / Cover
            // Scale so texture completely covers 960x480
            float base_scale = std::max(960.0f / tw, 480.0f / th);
            float total_scale = base_scale * m_cfg.bg_zoom;
            float rw = tw * total_scale;
            float rh = th * total_scale;
            float rx = (960.0f - rw) * 0.5f + m_cfg.bg_pan_x;
            float ry = (480.0f - rh) * 0.5f + m_cfg.bg_pan_y;
            DrawTexturePro(*bg, src, Rectangle{ rx, ry, rw, rh }, Vector2{0, 0}, 0.0f, WHITE);
        } else if (m_cfg.fill_mode == FILL_MODE_LETTERBOX) { // Aspect fit with black bars
            float base_scale = std::min(960.0f / tw, 480.0f / th);
            float total_scale = base_scale * m_cfg.bg_zoom;
            float rw = tw * total_scale;
            float rh = th * total_scale;
            float rx = (960.0f - rw) * 0.5f + m_cfg.bg_pan_x;
            float ry = (480.0f - rh) * 0.5f + m_cfg.bg_pan_y;
            DrawRectangle(0, 0, 960, 480, BLACK);
            DrawTexturePro(*bg, src, Rectangle{ rx, ry, rw, rh }, Vector2{0, 0}, 0.0f, WHITE);
        } else { // Adapt / Stretch
            float rw = 960.0f * m_cfg.bg_zoom;
            float rh = 480.0f * m_cfg.bg_zoom;
            float rx = (960.0f - rw) * 0.5f + m_cfg.bg_pan_x;
            float ry = (480.0f - rh) * 0.5f + m_cfg.bg_pan_y;
            DrawTexturePro(*bg, src, Rectangle{ rx, ry, rw, rh }, Vector2{0, 0}, 0.0f, WHITE);
        }
    }

    void RenderOffscreenTarget() {
        BeginTextureMode(m_renderTarget);
        ClearBackground(BLACK);

        if (!m_tray.IsDisplayDisabled()) {
            // 1. Background layer
            RenderBackgroundLayer();

            // 2. Render all active widgets
            RenderContext ctx{
                m_hwMon.GetStats(),
                m_fonts,
                (float)GetTime(),
                m_editorActive
            };
            m_widgets.RenderAll(ctx);
        }

        EndTextureMode();
    }

    void RenderWindow() {
        BeginDrawing();
        ClearBackground(Color{ 10, 14, 20, 255 });

        // Calculate aspect-fit letterbox viewport for 960x480 offscreen surface
        int sw = GetScreenWidth();
        int sh = GetScreenHeight();

        float scale_x = (float)sw / 960.0f;
        float scale_y = (float)sh / 480.0f;
        m_viewScale = std::min(scale_x, scale_y);

        float view_w = 960.0f * m_viewScale;
        float view_h = 480.0f * m_viewScale;
        m_viewX = (sw - view_w) * 0.5f;
        m_viewY = (sh - view_h) * 0.5f;

        // Render scaled offscreen LCD texture (inverted vertically for OpenGL FBO)
        Rectangle src_rec = { 0.0f, 0.0f, 960.0f, -480.0f };
        Rectangle dst_rec = { m_viewX, m_viewY, view_w, view_h };
        DrawTexturePro(m_renderTarget.texture, src_rec, dst_rec, Vector2{0, 0}, 0.0f, WHITE);

        // Editor overlays (selection boxes, snap lines, HUD mode badge) drawn in letterbox space
        if (m_editorActive && !m_tray.IsDisplayDisabled()) {
            Camera2D cam = {0};
            cam.offset = Vector2{ m_viewX, m_viewY };
            cam.zoom = m_viewScale;

            BeginMode2D(cam);

            // Prominent EDIT MODE top-left HUD pill
            DrawRectangleRec(Rectangle{ 8.0f, 8.0f, 216.0f, 22.0f }, Color{ 10, 25, 45, 235 });
            DrawRectangleLinesEx(Rectangle{ 8.0f, 8.0f, 216.0f, 22.0f }, 1.0f, Color{ 30, 230, 215, 220 });
            DrawText("[ EDIT MODE ACTIVE ] - [E] Exit", 14, 13, 11, Color{ 30, 230, 215, 255 });

            // Background snap alignment lines
            if (m_snapBgX) {
                DrawLineEx(Vector2{ 480.0f, 0.0f }, Vector2{ 480.0f, 480.0f }, 1.0f, Color{ 30, 230, 215, 200 });
            }
            if (m_snapBgY) {
                DrawLineEx(Vector2{ 0.0f, 240.0f }, Vector2{ 960.0f, 240.0f }, 1.0f, Color{ 30, 230, 215, 200 });
            }

            // If background is selected (no widget selected), show subtle canvas border & indicator
            if (m_widgets.GetSelectedWidget() == nullptr) {
                DrawRectangleLinesEx(Rectangle{ 0, 0, 960, 480 }, 1.5f, Color{ 30, 230, 215, 120 });
                DrawText("[Background Selected - Drag to Pan, Scroll to Zoom]", 12, 34, 10, Color{ 30, 230, 215, 180 });
            }

            RenderContext ctx{
                m_hwMon.GetStats(),
                m_fonts,
                (float)GetTime(),
                m_editorActive
            };
            m_widgets.RenderEditorOverlay(ctx);
            EndMode2D();
        }

        // Draw Undo toast notification
        DrawToastNotification(m_undo);

        // Dear ImGui User Interface
        rlImGuiBegin();

        DrawImGuiMenuBar();

        if (m_editorActive) {
            if (m_showWidgetManager) {
                if (m_widgets.DrawManagerWindow(&m_showWidgetManager)) {
                    PushUndoState("Widget Manager Edit");
                }
            }
            if (m_showInspector) {
                if (m_widgets.DrawInspectorWindow(&m_showInspector, &m_cfg.bg_pan_x, &m_cfg.bg_pan_y, &m_cfg.bg_zoom, &m_cfg.fill_mode)) {
                    PushUndoState("Property Edit");
                }
            }
        }

        if (m_showDisplayControls) {
            DrawDisplayControlsWindow(&m_showDisplayControls);
        }

        if (m_showBackgroundSettings) {
            DrawBackgroundSettingsWindow(&m_showBackgroundSettings);
        }

        if (m_showHelp) {
            DrawHelpWindow(&m_showHelp);
        }

        rlImGuiEnd();

        EndDrawing();
    }

    void DrawImGuiMenuBar() {
        if (ImGui::BeginMainMenuBar()) {
            if (ImGui::BeginMenu("File")) {
                if (ImGui::MenuItem("Open Background Media...", "B")) {
                    OpenMediaFileDialog();
                }
                if (ImGui::MenuItem("Stream Desktop Window...", "S")) {
                    m_streamer.ShowPickerMenu(GetWindowHandle());
                }
                ImGui::Separator();
                if (ImGui::MenuItem("Save Configuration", "Ctrl+S")) {
                    ConfigManager::Save(m_cfg, m_widgets);
                }
                ImGui::Separator();
                if (ImGui::MenuItem("Exit", "Alt+F4")) {
                    CloseWindow();
                }
                ImGui::EndMenu();
            }

            if (ImGui::BeginMenu("Edit")) {
                if (ImGui::MenuItem("Undo", "Ctrl+Z", false, m_undo.CanUndo())) {
                    std::string prev;
                    if (m_undo.Undo(prev)) {
                        ConfigManager::DeserializeFromString(prev, m_cfg, m_widgets);
                    }
                }
                if (ImGui::MenuItem("Redo", "Ctrl+Y", false, m_undo.CanRedo())) {
                    std::string next;
                    if (m_undo.Redo(next)) {
                        ConfigManager::DeserializeFromString(next, m_cfg, m_widgets);
                    }
                }
                ImGui::Separator();
                if (ImGui::MenuItem("Reset Layout to Defaults")) {
                    m_widgets.ResetAll();
                    PushUndoState("Reset Layout");
                }
                ImGui::EndMenu();
            }

            if (ImGui::BeginMenu("View")) {
                if (ImGui::MenuItem("Layout Editor Mode", "E", m_editorActive)) {
                    SetEditorActive(!m_editorActive);
                }
                if (ImGui::MenuItem("Display Sleep / Standby", "D", m_tray.IsDisplayDisabled())) {
                    ToggleDisplaySleep();
                }
                if (auto* vis = m_widgets.FindWidget("visualizer")) {
                    bool vis_en = vis->IsEnabled();
                    if (ImGui::MenuItem("Audio Visualizer", "M", vis_en)) {
                        vis->SetEnabled(!vis_en);
                        PushUndoState("Toggle Visualizer");
                    }
                }
                ImGui::Separator();
                const char* fill_names[] = { "Fill: Crop (Cover)", "Fill: Letterbox (Fit)", "Fill: Adapt (Stretch)" };
                for (int i = 0; i < 3; i++) {
                    if (ImGui::MenuItem(fill_names[i], nullptr, m_cfg.fill_mode == i)) {
                        m_cfg.fill_mode = i;
                        PushUndoState("Fill Mode Change");
                    }
                }
                ImGui::EndMenu();
            }

            if (ImGui::BeginMenu("Windows")) {
                ImGui::MenuItem("Widget Manager", nullptr, &m_showWidgetManager);
                ImGui::MenuItem("Properties Inspector", nullptr, &m_showInspector);
                ImGui::MenuItem("Display & Output Settings", nullptr, &m_showDisplayControls);
                ImGui::MenuItem("Background & Media Settings", nullptr, &m_showBackgroundSettings);
                ImGui::Separator();
                ImGui::MenuItem("Keyboard Shortcuts & Help", nullptr, &m_showHelp);
                ImGui::EndMenu();
            }

            // Prominent Mode Indicator in Menu Bar
            ImGui::SameLine();
            ImGui::Separator();
            ImGui::SameLine();
            if (m_editorActive) {
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.10f, 0.40f, 0.45f, 0.9f));
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.15f, 0.55f, 0.60f, 1.0f));
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.20f, 1.0f, 0.95f, 1.0f));
                if (ImGui::SmallButton("[ EDIT MODE ON ]")) {
                    SetEditorActive(false);
                }
                ImGui::PopStyleColor(3);
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("Layout Editor is ACTIVE.\nClick or press 'E' to exit.");
                }
            } else {
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.12f, 0.16f, 0.22f, 0.7f));
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.18f, 0.24f, 0.32f, 0.9f));
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.60f, 0.70f, 0.80f, 1.0f));
                if (ImGui::SmallButton("[ VIEW MODE ]")) {
                    SetEditorActive(true);
                }
                ImGui::PopStyleColor(3);
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("Normal View Mode.\nClick or press 'E' to edit layout.");
                }
            }

            // Quick Status indicator on the right side
            float avail = ImGui::GetContentRegionAvail().x;
            if (avail > 320.0f) {
                ImGui::SameLine(avail - 220.0f);
                if (m_panel.IsOpen()) {
                    ImGui::TextColored(ImVec4(0.2f, 0.9f, 0.4f, 1.0f), "[Panel Connected]");
                } else {
                    ImGui::TextColored(ImVec4(0.9f, 0.6f, 0.2f, 1.0f), "[Desktop Preview]");
                }
                ImGui::SameLine();
                ImGui::TextDisabled("| %d FPS", GetFPS());
            }

            ImGui::EndMainMenuBar();
        }
    }

    void DrawDisplayControlsWindow(bool* p_open) {
        ImGui::SetNextWindowPos(ImVec2(310, 50), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize(ImVec2(340, 380), ImGuiCond_FirstUseEver);
        if (ImGui::Begin("Display & Output Settings", p_open)) {
            bool changed = false;

            // Brightness slider
            int b = m_cfg.brightness;
            if (ImGui::SliderInt("Brightness", &b, 0, 100, "%d%%")) {
                m_cfg.brightness = b;
                if (b > 0 && m_tray.IsDisplayDisabled()) {
                    SetDisplaySleep(false);
                } else if (b == 0 && !m_tray.IsDisplayDisabled()) {
                    SetDisplaySleep(true);
                } else if (m_panel.IsOpen() && !m_tray.IsDisplayDisabled()) {
                    m_panel.SetBrightness(b);
                }
                changed = true;
            }

            // JPEG Quality slider
            float q = std::clamp(m_cfg.jpeg_quality * 100.0f, 10.0f, 100.0f);
            if (ImGui::SliderFloat("JPEG Quality", &q, 10.0f, 100.0f, "%.0f%%")) {
                m_cfg.jpeg_quality = q / 100.0f;
                if (m_panel.IsOpen()) m_panel.SetJpegQuality(m_cfg.jpeg_quality);
                changed = true;
            }

            // Refresh interval
            if (ImGui::SliderFloat("Refresh Interval", &m_cfg.refresh_interval, 0.2f, 5.0f, "%.1f sec")) {
                changed = true;
            }

            ImGui::Separator();
            ImGui::Text("Panel Transformation");

            const char* rot_labels[] = { "0 deg", "90 deg", "180 deg", "270 deg" };
            int rot_idx = m_cfg.rotation / 90;
            if (ImGui::Combo("Rotation", &rot_idx, rot_labels, 4)) {
                m_cfg.rotation = rot_idx * 90;
                changed = true;
            }

            if (ImGui::Checkbox("Flip Horizontal", &m_cfg.flip_x)) changed = true;
            ImGui::SameLine();
            if (ImGui::Checkbox("Flip Vertical", &m_cfg.flip_y)) changed = true;

            ImGui::Separator();
            if (m_tray.IsDisplayDisabled()) {
                ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "LCD Status: OFF (Standby)");
            } else {
                ImGui::TextColored(ImVec4(0.2f, 0.9f, 0.4f, 1.0f), "LCD Status: ON (Active)");
            }
            if (ImGui::Button("Turn Off Display (Sleep)")) {
                SetDisplaySleep(true);
            }
            ImGui::SameLine();
            if (ImGui::Button("Turn On Display")) {
                SetDisplaySleep(false);
            }

            if (changed) {
                PushUndoState("Display Setting Change");
            }
        }
        ImGui::End();
    }

    void DrawBackgroundSettingsWindow(bool* p_open) {
        ImGui::SetNextWindowPos(ImVec2(300, 70), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize(ImVec2(380, 420), ImGuiCond_FirstUseEver);
        if (ImGui::Begin("Background & Media Settings", p_open)) {
            bool changed = false;

            ImGui::TextWrapped("Current file: %s", m_cfg.background_path.c_str());
            if (ImGui::Button("Browse Media File...")) {
                OpenMediaFileDialog();
            }

            ImGui::Separator();
            const char* fill_names[] = { "Crop (Cover)", "Letterbox (Fit)", "Adapt (Stretch)" };
            if (ImGui::Combo("Fill Mode", &m_cfg.fill_mode, fill_names, 3)) {
                changed = true;
            }

            if (ImGui::DragFloat2("Pan (X, Y)", &m_cfg.bg_pan_x, 1.0f, -480.0f, 480.0f, "%.1f")) changed = true;
            if (ImGui::SliderFloat("Zoom", &m_cfg.bg_zoom, 0.2f, 5.0f, "%.2fx")) changed = true;

            ImGui::Spacing();
            if (ImGui::Button("Reset Pan & Zoom")) {
                m_cfg.bg_pan_x = 0.0f;
                m_cfg.bg_pan_y = 0.0f;
                m_cfg.bg_zoom = 1.0f;
                changed = true;
            }

            ImGui::Separator();
            ImGui::TextColored(ImVec4(0.12f, 0.90f, 0.85f, 1.0f), "Lightweight Video Cache (960x480 @ 30 FPS)");
            ImGui::TextDisabled("Auto-swaps to cache on minimize/tray to minimize CPU/RAM usage");

            if (m_isOptimizingVideo) {
                ImGui::TextColored(ImVec4(0.9f, 0.7f, 0.2f, 1.0f), "Transcoding in background: downscaling to 960x480...");
            } else if (!m_cachedVideoPath.empty()) {
                ImGui::TextColored(ImVec4(0.2f, 0.9f, 0.4f, 1.0f), "Cache Status: Ready");
                if (m_playingCachedVideo) {
                    ImGui::TextDisabled("Currently playing: Optimized Cache (960x480 @ 30 FPS)");
                    if (ImGui::Button("Switch to Original High-Res")) {
                        if (m_vp && m_vp->Open(m_cfg.background_path)) {
                            m_playingCachedVideo = false;
                        }
                    }
                } else {
                    ImGui::TextDisabled("Currently playing: Original High-Res");
                    if (ImGui::Button("Switch to Optimized Cache")) {
                        if (m_vp && m_vp->Open(m_cachedVideoPath)) {
                            m_playingCachedVideo = true;
                        }
                    }
                }
            } else {
                ImGui::TextDisabled("Cache Status: No cache active");
                if (m_vp && VideoCache::NeedsOptimization(m_cfg.background_path)) {
                    if (ImGui::Button("Generate 960x480 Cache Now")) {
                        VideoCache::StartBackground(m_cfg.background_path);
                        m_isOptimizingVideo = true;
                    }
                }
            }

            if (changed) {
                PushUndoState("Background Pan/Zoom Change");
            }
        }
        ImGui::End();
    }

    void DrawHelpWindow(bool* p_open) {
        ImGui::SetNextWindowPos(ImVec2(290, 70), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize(ImVec2(400, 380), ImGuiCond_FirstUseEver);
        if (ImGui::Begin("Keyboard Shortcuts & Help", p_open)) {
            ImGui::TextColored(ImVec4(0.12f, 0.90f, 0.85f, 1.0f), "JL LCD AIO - Controls");
            ImGui::Separator();
            ImGui::BulletText("[E] : Toggle Layout Editor Mode");
            ImGui::BulletText("[B] : Browse & Load Background Media");
            ImGui::BulletText("[S] : Stream Desktop Window");
            ImGui::BulletText("[D] : Toggle Display Sleep / Standby");
            ImGui::BulletText("[M] : Toggle CAVA Audio Visualizer");
            ImGui::BulletText("[Ctrl+Z] : Undo Layout Edit");
            ImGui::BulletText("[Ctrl+Y] : Redo Layout Edit");
            ImGui::BulletText("[Ctrl+Click] : Multi-select widgets on canvas");
            ImGui::BulletText("[Ctrl+A] : Select all widgets");
            ImGui::BulletText("[Arrows] : Nudge selected widget(s) (Shift: 5px)");
            ImGui::BulletText("[Del / Backspace] : Disable selected widget(s)");
            ImGui::BulletText("[Esc] : Clear widget selection");
            ImGui::BulletText("[Drag Handles] : Resize widget by edges or corners");
            ImGui::BulletText("[Scroll Wheel] : Resize selected widget / zoom BG");
            ImGui::Separator();
            ImGui::TextWrapped("The UI canvas is dynamically rendered to 960x480 for the physical USB display, and scales seamlessly with the desktop window.");
        }
        ImGui::End();
    }

    void OpenMediaFileDialog() {
        std::string path = TrayManager::OpenNativeFileDialog(GetWindowHandle());
        if (!path.empty()) {
            LoadMedia(path);
            PushUndoState("Load Media");
        }
    }

    void PushFrameToPanel(float dt) {
        if (!m_panel.IsOpen() || m_tray.IsDisplayDisabled()) return;

        m_panelTimer += dt;
        // ~30 FPS frame push rate to USB hardware
        if (m_panelTimer >= (1.0f / 30.0f)) {
            m_panelTimer = 0.0f;

            void* raw_pixels = rlReadTexturePixels(m_renderTarget.texture.id, 960, 480, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8);
            if (raw_pixels) {
                JLPanel::TransformFrame(
                    (const uint8_t*)raw_pixels, 960, 480,
                    m_panelPixels,
                    m_cfg.rotation, m_cfg.flip_x, m_cfg.flip_y,
                    true // Fuses vertical OpenGL inversion into transform
                );

                m_panel.PushSurfaceAsync(m_panelPixels, 960, 480);
                MemFree(raw_pixels);
            }
        }
    }

    AppConfig m_cfg;
    WidgetManager m_widgets;
    UndoManager m_undo;
    HardwareMonitor m_hwMon;
    JLPanel m_panel;
    TrayManager m_tray;
    FontCollection m_fonts;
    WindowStreamer m_streamer;

    std::unique_ptr<VideoPlayer> m_vp;
    Texture2D m_mediaTexture = {0};
    Texture2D m_streamTexture = {0};
    RenderTexture2D m_renderTarget = {0};

    uint8_t* m_panelPixels = nullptr;
    float m_panelTimer = 0.0f;

    bool m_editorActive = false;
    bool m_showWidgetManager = true;
    bool m_showInspector = true;
    bool m_showDisplayControls = false;
    bool m_showBackgroundSettings = false;
    bool m_showHelp = false;

    float m_viewScale = 1.0f;
    float m_viewX = 0.0f;
    float m_viewY = 0.0f;

    bool m_noPanel = false;
    bool m_startMinimized = false;
    std::string m_screenshotPath;

    // Background interactive drag state
    bool m_isDraggingBackground = false;
    Vector2 m_bgDragStartMouse = {0, 0};
    Vector2 m_bgDragStartPan = {0, 0};
    bool m_snapBgX = false;
    bool m_snapBgY = false;

    // Video Cache state
    std::string m_cachedVideoPath;
    bool m_isOptimizingVideo = false;
    bool m_playingCachedVideo = false;
    bool m_wasWindowHiddenOrMinimized = false;
};

int main(int argc, char** argv) {
    App app;
    if (!app.Init(argc, argv)) {
        return 1;
    }
    app.Run();
    return 0;
}
