#pragma once

#include <string>

class WidgetManager;

enum FillMode {
    FILL_MODE_CROP = 0,      // Cover (center-crop to 2:1)
    FILL_MODE_LETTERBOX = 1, // Fit with black bars
    FILL_MODE_ADAPT = 2,     // Stretch to fill 960x480
    FILL_MODE_COUNT
};

struct AppConfig {
    // Hardware & display output
    int rotation = 0;
    bool flip_x = false;
    bool flip_y = false;
    int fill_mode = FILL_MODE_CROP;
    float jpeg_quality = 0.75f;
    int brightness = 100;
    float refresh_interval = 1.0f;

    // Media background
    std::string background_path = "video.mp4";
    float bg_pan_x = 0.0f;
    float bg_pan_y = 0.0f;
    float bg_zoom = 1.0f;

    // Window dimensions
    int window_width = 960;
    int window_height = 480;

    // Custom labels
    std::string cpu_name;
    std::string gpu_name;

    void SetDefaults();
};

class ConfigManager {
public:
    static std::string GetConfigPath();

    static bool Load(AppConfig& cfg, WidgetManager& widgets, const std::string& path = "");
    static bool Save(const AppConfig& cfg, const WidgetManager& widgets, const std::string& path = "");

    static std::string SerializeToString(const AppConfig& cfg, const WidgetManager& widgets);
    static bool DeserializeFromString(const std::string& ini_str, AppConfig& cfg, WidgetManager& widgets);
};
