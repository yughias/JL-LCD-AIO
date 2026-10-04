#pragma once

#include <string>
#include <functional>
#include <cstdint>

class TrayManager {
public:
    using ChangeBgCallback = std::function<void(const std::string&)>;

    TrayManager();
    ~TrayManager();

    void Init(void* hwnd, ChangeBgCallback change_bg_cb);
    void Update();
    void Shutdown();

    bool IsDisplayDisabled() const;
    void SetDisplayDisabled(bool disabled);

    void OpenFileDialog();
    bool PickCustomColor(uint32_t* in_out_color);

    bool ConsumeToggleEdit();
    bool ConsumePickStream();

    int ConsumeFillModeRequest();
    void SetActiveFillMode(int mode);

    int ConsumeQualityRequest();
    void SetActiveQuality(int percent);

    bool IsWindowVisible() const;
    void MinimizeWindow();
    void RestoreWindow();
    bool ShouldExit() const;

    static void TrimMemory();
    static void Sleep(unsigned long ms);

    static std::string OpenNativeFileDialog(void* hwnd);
};
