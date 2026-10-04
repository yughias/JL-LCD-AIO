#include "config_manager.hpp"
#include "widgets.hpp"
#include "tortellini.hh"
#include <fstream>
#include <sstream>
#include <cmath>
#include <algorithm>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOGDI
#define NOUSER
#include <windows.h>
#undef CloseWindow
#undef ShowCursor
#undef Rectangle
#undef DrawText
#endif

void AppConfig::SetDefaults() {
    rotation = 0;
    flip_x = false;
    flip_y = false;
    fill_mode = FILL_MODE_CROP;
    jpeg_quality = 0.75f;
    brightness = 100;
    refresh_interval = 1.0f;

    background_path = "video.mp4";
    bg_pan_x = 0.0f;
    bg_pan_y = 0.0f;
    bg_zoom = 1.0f;

    window_width = 960;
    window_height = 480;

    cpu_name.clear();
    gpu_name.clear();
}

std::string ConfigManager::GetConfigPath() {
#ifdef _WIN32
    char exe_path[MAX_PATH] = {0};
    DWORD len = GetModuleFileNameA(NULL, exe_path, MAX_PATH);
    if (len > 0 && len < MAX_PATH) {
        char* last_slash = strrchr(exe_path, '\\');
        char* last_fwd = strrchr(exe_path, '/');
        if (last_fwd && (!last_slash || last_fwd > last_slash)) {
            last_slash = last_fwd;
        }
        if (last_slash) {
            *last_slash = '\0';
            std::string path = std::string(exe_path) + "\\config.ini";
            std::ifstream test(path);
            if (test.is_open()) return path;
            std::ifstream test_cwd("config.ini");
            if (test_cwd.is_open()) return "config.ini";
            return path;
        }
    }
#endif
    return "config.ini";
}

std::string ConfigManager::SerializeToString(const AppConfig& cfg, const WidgetManager& widgets) {
    tortellini::ini ini;

    // General
    ini["General"]["refresh_interval"] = cfg.refresh_interval;
    ini["General"]["window_width"] = cfg.window_width;
    ini["General"]["window_height"] = cfg.window_height;

    // Display
    ini["Display"]["rotation"] = cfg.rotation;
    ini["Display"]["flip_x"] = cfg.flip_x;
    ini["Display"]["flip_y"] = cfg.flip_y;
    ini["Display"]["fill_mode"] = cfg.fill_mode;
    ini["Display"]["jpeg_quality"] = (int)std::round(cfg.jpeg_quality * 100.0f);
    ini["Display"]["brightness"] = cfg.brightness;

    // Media
    ini["Media"]["background"] = cfg.background_path;
    ini["Media"]["pan_x"] = cfg.bg_pan_x;
    ini["Media"]["pan_y"] = cfg.bg_pan_y;
    ini["Media"]["zoom"] = cfg.bg_zoom;

    // Hardware
    if (auto* w = dynamic_cast<const TextWidget*>(widgets.FindWidget("cpu_header"))) {
        ini["Hardware"]["cpu"] = (w->GetUseCustomText() && !w->GetCustomText().empty()) ? w->GetCustomText() : "";
    } else {
        ini["Hardware"]["cpu"] = cfg.cpu_name;
    }
    if (auto* w = dynamic_cast<const TextWidget*>(widgets.FindWidget("gpu_header"))) {
        ini["Hardware"]["gpu"] = (w->GetUseCustomText() && !w->GetCustomText().empty()) ? w->GetCustomText() : "";
    } else {
        ini["Hardware"]["gpu"] = cfg.gpu_name;
    }

    // Widgets
    widgets.SaveToIni(ini);

    std::ostringstream ss;
    ss << ini;
    return ss.str();
}

bool ConfigManager::DeserializeFromString(const std::string& ini_str, AppConfig& cfg, WidgetManager& widgets) {
    if (ini_str.empty()) return false;

    std::istringstream ss(ini_str);
    tortellini::ini ini;
    ss >> ini;

    // General
    cfg.refresh_interval = (float)(ini["General"]["refresh_interval"] | (double)cfg.refresh_interval);
    cfg.window_width = (int)(ini["General"]["window_width"] | (long long)cfg.window_width);
    cfg.window_height = (int)(ini["General"]["window_height"] | (long long)cfg.window_height);

    // Display
    cfg.rotation = (int)(ini["Display"]["rotation"] | (long long)cfg.rotation);
    cfg.flip_x = ini["Display"]["flip_x"] | cfg.flip_x;
    cfg.flip_y = ini["Display"]["flip_y"] | cfg.flip_y;
    cfg.fill_mode = (int)(ini["Display"]["fill_mode"] | (long long)cfg.fill_mode);
    double raw_quality = (double)(ini["Display"]["jpeg_quality"] | (double)(cfg.jpeg_quality * 100.0));
    if (raw_quality > 1.0) {
        while (raw_quality > 100.0) {
            raw_quality /= 10.0;
        }
        cfg.jpeg_quality = (float)(raw_quality / 100.0);
    } else if (raw_quality > 0.0) {
        cfg.jpeg_quality = (float)raw_quality;
    } else {
        cfg.jpeg_quality = 0.75f;
    }
    cfg.jpeg_quality = std::clamp(cfg.jpeg_quality, 0.10f, 1.0f);
    cfg.brightness = (int)(ini["Display"]["brightness"] | (long long)cfg.brightness);

    // Media
    cfg.background_path = ini["Media"]["background"] | cfg.background_path;
    cfg.bg_pan_x = (float)(ini["Media"]["pan_x"] | (double)cfg.bg_pan_x);
    cfg.bg_pan_y = (float)(ini["Media"]["pan_y"] | (double)cfg.bg_pan_y);
    cfg.bg_zoom = (float)(ini["Media"]["zoom"] | (double)cfg.bg_zoom);

    // Hardware
    cfg.cpu_name = ini["Hardware"]["cpu"] | cfg.cpu_name;
    cfg.gpu_name = ini["Hardware"]["gpu"] | cfg.gpu_name;

    // Widgets
    widgets.LoadFromIni(ini);

    // Update custom header labels if present in Hardware
    if (!cfg.cpu_name.empty()) {
        if (auto* w = dynamic_cast<TextWidget*>(widgets.FindWidget("cpu_header"))) {
            w->SetCustomText(cfg.cpu_name);
            w->SetUseCustomText(true);
        }
    }
    if (!cfg.gpu_name.empty()) {
        if (auto* w = dynamic_cast<TextWidget*>(widgets.FindWidget("gpu_header"))) {
            w->SetCustomText(cfg.gpu_name);
            w->SetUseCustomText(true);
        }
    }

    return true;
}

bool ConfigManager::Load(AppConfig& cfg, WidgetManager& widgets, const std::string& path) {
    std::string config_file = path.empty() ? GetConfigPath() : path;
    std::ifstream file(config_file);
    if (!file.is_open()) {
        cfg.SetDefaults();
        widgets.ResetAll();
        return false;
    }

    std::ostringstream ss;
    ss << file.rdbuf();
    return DeserializeFromString(ss.str(), cfg, widgets);
}

bool ConfigManager::Save(const AppConfig& cfg, const WidgetManager& widgets, const std::string& path) {
    std::string config_file = path.empty() ? GetConfigPath() : path;
    std::ofstream file(config_file);
    if (!file.is_open()) {
        return false;
    }

    file << SerializeToString(cfg, widgets);
    return true;
}
