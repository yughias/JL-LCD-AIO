# JL USB LCD Display Driver & AIO System Monitor

[![CI](https://github.com/yughias/JL-LCD-AIO/actions/workflows/ci.yml/badge.svg)](https://github.com/yughias/JL-LCD-AIO/actions/workflows/ci.yml)
[![Release](https://img.shields.io/github/v/release/yughias/JL-LCD-AIO?include_prereleases&style=flat-square)](https://github.com/yughias/JL-LCD-AIO/releases)
[![Platform](https://img.shields.io/badge/platform-Windows%2010%20%7C%2011-blue.svg?style=flat-square)](https://microsoft.com/windows)
[![Resolution](https://img.shields.io/badge/resolution-960%C3%97480-orange.svg?style=flat-square)]()
[![License](https://img.shields.io/badge/license-MIT-green.svg?style=flat-square)](LICENSE)

A high-performance C/C++ driver and all-in-one desktop monitor application designed for secondary **USB LCD displays based on the Jieli (JL) chipset** (commonly 960×480 panels such as Jungle Leopard, Turing, AX206, and similar PC case sub-screens).

---

## Repository Structure

```
├── CMakeLists.txt         # Root unified CMake build configuration
├── jl_lcd.h               # Core header-only stb-style C/C++ driver library
├── lcd_aio/               # Full-featured Raylib AIO Monitor & GUI desktop application
│   ├── CMakeLists.txt     # AIO subproject build configuration (fetches Raylib automatically)
│   ├── src/               # Application sources (telemetry, video player, UI, audio, tray)
│   └── README.md          # Dedicated AIO application documentation
├── examples/              # Standalone C driver demo programs
│   ├── aspect_ratio.c     # Arbitrary resolution scaling & aspect ratio fitting
│   ├── benchmark.c        # USB throughput and FPS transfer benchmarking
│   ├── color_wave.c       # Procedural color wave animation
│   ├── dvd_logo.c         # Bouncing DVD logo with wall collision color cycling
│   ├── gradient.c         # Smooth linear color gradient rendering
│   └── sine_wave.c        # Dynamic mathematical sine wave graph
└── .github/workflows/     # GitHub Actions CI & automated release workflows
    ├── ci.yml             # Automated build verification on push / PR
    └── release.yml        # Automatic GitHub Release publishing on git tag push (`v*`)
```

---

## Features

### 🖥️ Core Driver Library (`jl_lcd.h`)
- **Header-Only (`stb`-style)**: Integrate anywhere by simply writing `#define JL_PANEL_IMPLEMENTATION` before `#include "jl_lcd.h"`.
- **C & C++ Compatible**: Native C11 code, seamlessly usable in pure C or modern C++.
- **Zero Configuration Discovery**: Automatically enumerates Windows device registries to find and bind to connected JL USB serial devices without hardcoded COM ports.
- **Asynchronous Worker Thread**: Non-blocking `jl_push_surface_async()` transmits frames in the background with automatic frame-dropping to prevent latency backpressure.
- **Synchronous API**: `jl_push_surface_sync()` for deterministic throughput benchmarks and diagnostic tests.
- **Native JPEG Compression**: Direct hardware-accelerated Windows Imaging Component (WIC) encoding with runtime quality adjustment (`jl_set_jpeg_quality()`).
- **Aspect Ratio & Auto-Fit**: Automatically centers and letterboxes surfaces of any arbitrary dimension to the native 960×480 canvas.

---

### 🚀 AIO Desktop Monitor Application (`lcd_aio`)
- **Real-Time System Telemetry**:
  - **CPU**: Real-time utilization calculated from Windows kernel times (`GetSystemTimes`), live CPU temperature, and automatic CPU model name detection.
  - **GPU**: Direct utilization & temperature monitoring via NVIDIA NVML dynamic linking (`nvml.dll`).
  - **Memory & Storage**: RAM / VRAM usage gauges and live free disk space readout.
  - **Time & Performance**: High-precision digital clock, date, live FPS counter, and system uptime.
- **Interactive Dear ImGui Layout Editor (`[E]`)**:
  - Custom dark futuristic visual styling powered by **Dear ImGui** and **Raylib 5.5**.
  - **Multi-Selection (`Ctrl + Click`)**: Select multiple telemetry gauges, text labels, or visualizers simultaneously. Drag, nudge, or delete entire groups in lockstep!
  - **Edge & Corner Resize Handles**: Pull any of the 4 corners or 4 edges of a widget to resize or zoom it interactively, with real-time responsive cursor feedback (`NWSE`, `NESW`, `EW`, `NS`).
  - **Smart Mouse Wheel Zoom**: Hovering or selecting any widget resizes it immediately; scrolling over empty canvas zooms the background image or video.
  - **360° Uncropped Background Pan & Centered Zoom**: Panning downward, upward, or sideways smoothly reveals the full uncropped source content of any resolution/aspect ratio.
  - **PowerPoint-Style Snapping**: Intelligent dynamic alignment guide lines for center and axis snapping.
  - **Multi-Level Undo / Redo (`Ctrl+Z`, `Ctrl+Y`)**: Complete state history snapshots with transient HUD toast alerts.
  - **Properties Inspector & Widget Manager**: Filter by category (CPU, GPU, Memory & Storage, Time & Status, Audio), bulk align widgets, customize labels, and tune colors.
- **Hardware-Accelerated Background Video & Cache**:
  - Plays animated loops (`video.mp4` or user-selected media via `[B]`).
  - **Full Quality in Preview**: When the preview window is open, original media plays at native resolution for 100% editing and framing freedom (Crop, Letterbox, Adapt).
  - **Automatic Lightweight Cache**: A background thread encodes a downscaled 960×480 @ 30 FPS MP4 cache (`cache/` located in the executable directory).
  - **Auto-Switching on Minimize**: When minimized to system tray, the player automatically swaps to the optimized cache and calls `EmptyWorkingSet`, dropping memory and CPU usage to the absolute minimum.
  - **Cache Controls**: Background & Media Settings window provides real-time cache status and manual toggle between high-res and optimized cache.
- **Live Window Streaming (`[S]`)**:
  - Mirror any running application window (Spotify, Discord, browser, OBS, media player) directly to the USB LCD with real-time scaling!
- **WASAPI Audio Spectrum Visualizer (`[M]`)**:
  - Built-in WASAPI loopback audio capture with real-time FFT spectrum bars (CAVA-style).
- **Deep Power-Saving & Sleep Mode (`[D]`)**:
  - Temporarily turn off the physical LCD display backlight and rendering loop with 0.0% CPU usage.
- **System Tray Integration**:
  - Window `[X]` button minimizes to the Windows notification tray instead of terminating.
  - Minimal context menu: **Show Window** / **Hide Window**, and **Exit LCD AIO**.
- **Default Quality 75%**:
  - Pre-calibrated 75% JPEG compression quality for crystal-clear visuals and smooth USB transfer throughput.
- **Fully Portable**: Statically linked binary with zero external DLL requirements.

---

## Quick Start: Building with CMake

### Prerequisites
- **Operating System**: Windows 10 or Windows 11 (64-bit)
- **Build Tools**: CMake (>= 3.16) and MinGW-w64 (`gcc`/`g++`) or MSVC
- **Git** (for automatic Raylib dependency fetching)

### Unified Build (All Targets)

Clone the repository and run:

```bash
# 1. Configure the build
cmake -B build -G "Ninja" -DCMAKE_BUILD_TYPE=Release

# 2. Build all targets (application + examples)
cmake --build build -j 4
```

*(If using Visual Studio, omit `-G "Ninja"` to generate standard MSVC solution files).*

All built executables will be neatly organized inside `build/bin/`:
- `build/bin/lcd_aio.exe` — The complete AIO application
- `build/bin/aspect_ratio.exe` — Driver aspect ratio scaling demo
- `build/bin/benchmark.exe` — USB FPS throughput benchmark
- `build/bin/color_wave.exe` — Color wave animation
- `build/bin/dvd_logo.exe` — Bouncing DVD logo demo
- `build/bin/gradient.exe` — Linear gradient test
- `build/bin/sine_wave.exe` — Dynamic sine wave graph

### Building Only the Application
```bash
cmake --build build --target lcd_aio
```

### Building Only the Examples
```bash
cmake -B build -DBUILD_APP=OFF -DBUILD_EXAMPLES=ON
cmake --build build
```

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
| **`[B]`** | Open native file dialog to choose background video (`.mp4`, `.mkv`) or image |
| **`[D]`** | Mute display / Toggle physical LCD sleep (0% CPU standby) |
| **`[M]`** | Toggle CAVA audio visualizer |
| **Window `[X]`** | Minimizes application directly to system tray |
| **Tray Icon** | Right-click for quick actions; double-click to restore/minimize window |

---

## Minimal Driver Code Example (`jl_lcd.h`)

To use the standalone driver in your own C or C++ project:

```c
#include <stdio.h>
#include <stdint.h>
#include <windows.h>

#define JL_PANEL_IMPLEMENTATION
#include "jl_lcd.h"

int main(void) {
    // 1. Initialize COM subsystem
    if (!jl_init()) {
        fprintf(stderr, "Failed to initialize COM runtime.\n");
        return 1;
    }

    // 2. Auto-detect and open the USB LCD panel
    jl_panel_t* panel = jl_open(NULL);
    if (!panel) {
        fprintf(stderr, "No JL LCD panel found on USB.\n");
        return 1;
    }

    // 3. Allocate 960x480 32-bit BGRA surface
    uint32_t pixels[960 * 480];
    for (int i = 0; i < 960 * 480; i++) {
        pixels[i] = 0x001E90FF; // Deep sky blue
    }

    // 4. Send surface asynchronously (worker thread encodes & pushes frame)
    jl_push_surface_async(panel, pixels, 960, 480);

    Sleep(1000);

    // 5. Cleanup
    jl_close(panel);
    return 0;
}
```

---

## Automated GitHub Releases (CI/CD)

The repository includes a GitHub Actions release pipeline (`.github/workflows/release.yml`).

### How to Trigger an Automated Release:
1. Create and push a semantic version tag:
   ```bash
   git tag v1.0.0
   git push origin v1.0.0
   ```
2. GitHub Actions will automatically:
   - Check out the repository.
   - Configure and compile all targets using MinGW-w64 and Ninja.
   - Package `lcd_aio.exe`, driver examples, headers, and documentation into `jl_lcd_aio-v1.0.0-windows-x64.zip`.
   - Publish a new GitHub Release with release notes and the `.zip` binary archive attached!

---

## Acknowledgments & Credits

This project was inspired by and adapted from the pioneering work of **senesees** on GitHub:
- **Original Repository**: [senesees / Jungle-Leopard-Display-TZMRIT-PF360-LCD](https://github.com/senesees/Jungle-Leopard-Display-TZMRIT-PF360-LCD)

Special thanks to **senesees** for reverse-engineering the USB serial protocol and communication packets of the Jungle Leopard / TZMRIT PF360 960×480 AIO liquid cooler display, providing the initial foundation for driving these displays natively without proprietary vendor bloatware.

This repository took those core insights and evolved them into:
- A standalone, portable, header-only C/C++ driver library (`jl_lcd.h`) with zero third-party DLL dependencies.
- Hardware-accelerated Windows Imaging Component (WIC) JPEG compression with dynamic runtime quality control.
- An asynchronous frame queue running on an independent background worker thread with automatic latency prevention.
- A fully-featured modern desktop telemetry and visualizer suite (`lcd_aio`) built with Raylib and Dear ImGui.

---

## Use of AI

This project was developed, architected, and refactored in pair-programming collaboration with AI assistance (**Google DeepMind's Antigravity** advanced agentic coding assistant). 

AI capabilities were utilized throughout development for:
- **Architecture & Refactoring**: Porting and re-architecting the application into clean, modern modular C++ and creating the header-only driver format.
- **UI & Layout Engine**: Designing and implementing the interactive Dear ImGui layout editor with real-time edge/corner resize handles, multi-widget selection (`Ctrl + Click`), alignment guide snapping, and multi-level undo/redo snapshot history.
- **Hardware Telemetry**: Integrating Windows registry CPU discovery, kernel CPU utilization calculations, and dynamic NVIDIA NVML runtime binding.
- **Performance & Media Pipelines**: Implementing background video transcoding pipelines, lightweight downscaled cache generation (960×480 @ 30 FPS), and automatic memory trimming (`EmptyWorkingSet`) when minimized to the system tray.
- **Testing & Verification**: Authoring automated headless test suites, ensuring robust error handling, percentage parsing normalizations, and multi-platform build integrity.

---

## License

This project is licensed under the [MIT License](LICENSE).
