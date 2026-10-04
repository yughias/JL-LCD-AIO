# JL LCD AIO Monitor (960×480)

A real-time Hardware Monitor & AIO Display application designed specifically for JL USB LCD secondary screens (960×480).

It renders CPU & GPU metrics with circular/dome arcs, supports seamless background video playback (`video.mp4`), in-app layout editor (`[E]`), live window streaming (`[S]`), WASAPI visualizer (`[M]`), and simultaneously outputs to the physical USB LCD display and a local desktop preview window.

---

## Visual Design

Based on the AIO theme:
- **Left Side (CPU)**:
  - Header: CPU Model (e.g. `Intel 265K`, loaded from `config.ini` or auto-detected).
  - Upper Gauge: Circular ring displaying **Temperature** (e.g. `40°C`) with vibrant neon cyan active arc.
  - Lower Gauge: Semicircular dome displaying **Utilization** (e.g. `11%`).
- **Right Side (GPU)**:
  - Header: GPU Model (e.g. `5070 Ti`, loaded from `config.ini` or auto-detected).
  - Upper Gauge: Circular ring displaying **Temperature** (e.g. `42°C`).
  - Lower Gauge: Semicircular dome displaying **Utilization** (e.g. `2%`).
- **Background**:
  - Automatically loads and plays **`video.mp4`** in a continuous seamless loop if found in the folder.
  - If `video.mp4` is not present, falls back to a procedural animated anime starry sky with twinkling stars and nebulae.

---

## Features

- **Modern C++ & Dear ImGui Architecture**:
  - Modular polymorphic widget architecture (`Widget`, `TextWidget`, `ArcGaugeWidget`, `VisualizerWidget`).
  - Dark futuristic styling with custom Dear ImGui docking windows (Widget Manager, Properties Inspector, Display Controls, Background Settings).
- **Interactive In-App Layout Editor (`[E]`)**:
  - **Multi-Selection (`Ctrl + Click`)**: Select and manipulate multiple widgets together. Drag them across the canvas in synchronized lockstep!
  - **Edge & Corner Resize Handles**: Pull any of the 4 corners or 4 edges of the selected widget to dynamically scale its size/radius/dimensions, with real-time responsive cursor feedback (`NWSE`, `NESW`, `EW`, `NS`).
  - **Smart Mouse Wheel Zoom**: Hovering or selecting any widget resizes it smoothly. When hovering over empty canvas, scrolling the wheel zooms the background media.
  - **360° Uncropped Background Pan & Centered Zoom**: Panning downward, upward, or sideways smoothly reveals the full uncropped source content.
  - **PowerPoint-Style Snapping**: Guides for center alignment and axis snapping against other widgets.
  - **Multi-Level Undo / Redo (`Ctrl+Z`, `Ctrl+Y`)**: Complete state history snapshots with transient HUD toast alerts.
- **Hardware Info Extraction**:
  - **`config.ini` Configuration**: Reads custom CPU and GPU labels from `config.ini`.
  - **Dynamic System Detection**: If not specified in `config.ini`, auto-detects CPU model via Windows Registry (`ProcessorNameString`) and GPU via NVIDIA NVML (`nvml.dll`).
  - **Command-line override**: Accepts `--cpu` and `--gpu` flags.
- **Automatic Video Cache & Memory Trimming (960×480)**:
  - Generates an optimized, lightweight 960×480 @ 30 FPS MP4 cache (`cache/` relative to `.exe`), leaving original video untouched for preview editing.
  - **Auto-Switch on Minimize**: When minimized or docked to the system tray, automatically switches playback to the lightweight cache and calls `EmptyWorkingSet`, dropping memory and CPU usage to the absolute minimum.
- **Interactive Background Media Loading (Videos & Images)**:
  - Click **`Browse Media File...`** in Background Settings (or press **`B`**, or right-click the menu tray icon) to open a native Windows file dialog.
  - Supports videos (`.mp4`, `.avi`, `.mkv`, `.mov`, `.webm`) and images (`.png`, `.jpg`, `.jpeg`, `.bmp`).
  - **Persistence**: Your chosen background and layout are saved in `config.ini` and automatically reloaded next time you start the app.
- **Default Quality 75%**:
  - Pre-calibrated 75% JPEG compression quality for optimal USB transfer speed and razor-sharp clarity.
- **Live Window Streaming (`[S]`)**:
  - Mirror any running application window (Spotify, Discord, browser, OBS, media player) directly to the USB LCD with real-time scaling!
- **WASAPI Audio Spectrum Visualizer (`[M]`)**:
  - Built-in WASAPI loopback audio capture with real-time FFT spectrum bars (CAVA-style).
- **Minimize to Menu Tray (Not Taskbar)**:
  - Minimizing or clicking the `[X]` button hides the window from the taskbar and docks it into the system tray.
  - Left-click or double-click the tray icon to restore the preview window.
  - Minimal context menu: **Show Window** / **Hide Window**, and **Exit LCD AIO**.
- **Display Standby / Sleep Mode (`[D]`)**:
  - Right-click the menu tray icon or press **`D`** to turn off display output with 0.0% CPU usage.
- **Pure GUI Mode (No CMD Window)**:
  - Built as a native Windows GUI application (`-mwindows` / Subsystem 2), with zero external DLL requirements.
- **Simultaneous USB Output & Desktop Preview**:
  - Transmits frames directly to the physical JL USB LCD panel via `jl_push_surface_async()`.
  - Also displays an interactive desktop window so you can monitor or test on your PC.

---

## Interactive Controls & Hotkeys Reference

| Key / Control | Action |
|:---:|:---|
| **`[E]`** | Toggle Dear ImGui layout editor (drag elements, handles, customize properties) |
| **`[Ctrl + Click]`** | **Multi-selection**: Add / remove widgets to group selection |
| **`[Ctrl + A]`** | Select all active widgets |
| **`[Esc]`** | Deselect all widgets |
| **`[Del] / [Backspace]`** | Disable selected widget(s) |
| **`[Drag Borders/Corners]`** | Resize selected widget (interactive zoom via edge/corner handles) |
| **Mouse Wheel (on Widget)** | Incrementally resize / zoom selected or hovered widget |
| **Mouse Wheel (on Canvas)** | Zoom background image or video smoothly around center |
| **Left / Right Drag (Canvas)** | Pan background image or video (fully reveals top/bottom/sides) |
| **`Arrow Keys`** | Nudge selected widget(s) (hold `Shift` for 5px steps) |
| **`Ctrl + Z`** | Undo last layout edit / element position change |
| **`Ctrl + Y`** | Redo previously undone action |
| **`[S]`** | Open live window streaming picker menu |
| **`[B]`** | Open native file dialog to choose background video or image |
| **`[D]`** | Mute display / Toggle physical LCD sleep (0% CPU standby) |
| **`[M]`** | Toggle CAVA audio visualizer |
| **Window `[X]`** | Minimizes application directly to system tray |
| **Tray Icon** | Right-click for quick actions; double-click to restore/minimize window |

### Command-line Arguments

- `--minimized` (or `--minimize`, `-m`): Start application minimized to system tray.
- `--no-panel`: Run desktop preview without opening the physical USB LCD display.
- `--editor`: Launch directly into edit mode (`[E]`).
- `--screenshot <path>`: Capture a screenshot and exit (useful for automated testing/scripts).

---

## Configuration (`config.ini`)

You can edit [`config.ini`](config.ini) (located in the same directory as the executable) to customize colors, font sizes, positions, and display settings. If missing, default values are automatically generated and saved.

---

## Acknowledgments & Credits

Adapted and evolved from the original USB serial protocol reverse-engineering by **senesees**:
- **Repository**: [senesees / Jungle-Leopard-Display-TZMRIT-PF360-LCD](https://github.com/senesees/Jungle-Leopard-Display-TZMRIT-PF360-LCD)

---

## Use of AI

Developed and refactored with the assistance of **Google DeepMind's Antigravity** agentic coding pair programmer for architectural design, ImGui layout editor features, telemetry binding, video caching optimizations, and automated test verification.

The build produces `lcd_aio.exe` inside `build/`.
