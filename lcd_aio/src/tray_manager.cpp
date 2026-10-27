#include "tray_manager.hpp"
#include <windows.h>
#include <shellapi.h>
#include <commdlg.h>
#include <psapi.h>
#include <cstdio>
#include <cstring>

#define WM_TRAYICON (WM_USER + 105)
#define ID_TRAY_DISABLE_DISPLAY 1001
#define ID_TRAY_CHANGE_BG       1002
#define ID_TRAY_STREAM_WINDOW   1003
#define ID_TRAY_TOGGLE_EDIT     1004
#define ID_TRAY_RESTORE_WINDOW  1005
#define ID_TRAY_EXIT            1006
#define ID_TRAY_FILL_CROP       1007
#define ID_TRAY_FILL_LETTERBOX  1008
#define ID_TRAY_FILL_ADAPT      1009
#define ID_TRAY_QUAL_100        1020
#define ID_TRAY_QUAL_95         1021
#define ID_TRAY_QUAL_90         1022
#define ID_TRAY_QUAL_80         1023
#define ID_TRAY_QUAL_60         1024

namespace {

static HWND s_hwnd = nullptr;
static WNDPROC s_orig_wndproc = nullptr;
static NOTIFYICONDATAA s_nid{};
static TrayManager::ChangeBgCallback s_on_change_bg;
static bool s_display_disabled = false;
static bool s_should_exit = false;
static bool s_pending_open_file = false;
static bool s_pending_toggle_display = false;
static bool s_pending_toggle_edit = false;
static bool s_pending_pick_stream = false;
static int s_active_fill_mode = 0;
static int s_pending_fill_mode = -1;
static int s_active_quality = 90;
static int s_pending_quality = -1;
static COLORREF s_cust_colors[16] = {0};

void update_tray_tooltip() {
    s_nid.uFlags = NIF_TIP;
    if (s_display_disabled) {
        snprintf(s_nid.szTip, sizeof(s_nid.szTip), "JL LCD AIO - Display Turned OFF (Sleep)");
    } else {
        snprintf(s_nid.szTip, sizeof(s_nid.szTip), "JL LCD AIO Monitor (960x480)");
    }
    Shell_NotifyIconA(NIM_MODIFY, &s_nid);
}

void show_tray_context_menu() {
    POINT pt;
    GetCursorPos(&pt);

    HMENU hMenu = CreatePopupMenu();

    bool is_visible = IsWindowVisible(s_hwnd) && !IsIconic(s_hwnd);
    AppendMenuA(hMenu, MF_STRING, ID_TRAY_RESTORE_WINDOW, is_visible ? "Hide Window" : "Show Window");
    AppendMenuA(hMenu, MF_STRING, ID_TRAY_DISABLE_DISPLAY, s_display_disabled ? "Turn ON LCD Display" : "Turn OFF LCD Display");
    AppendMenuA(hMenu, MF_SEPARATOR, 0, nullptr);
    AppendMenuA(hMenu, MF_STRING, ID_TRAY_EXIT, "Exit LCD AIO");

    SetForegroundWindow(s_hwnd);
    TrackPopupMenu(hMenu, TPM_RIGHTBUTTON, pt.x, pt.y, 0, s_hwnd, nullptr);
    DestroyMenu(hMenu);
}

LRESULT CALLBACK tray_subclass_proc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    switch (uMsg) {
        case WM_SYSCOMMAND:
            if ((wParam & 0xFFF0) == SC_MINIMIZE) {
                ShowWindow(hwnd, SW_HIDE);
                return 0;
            }
            if ((wParam & 0xFFF0) == SC_CLOSE) {
                if (!s_should_exit) {
                    ShowWindow(hwnd, SW_HIDE);
                    return 0;
                }
            }
            break;

        case WM_CLOSE:
            if (!s_should_exit) {
                ShowWindow(hwnd, SW_HIDE);
                return 0;
            }
            break;

        case WM_SIZE:
            if (wParam == SIZE_MINIMIZED) {
                ShowWindow(hwnd, SW_HIDE);
                return 0;
            }
            break;

        case WM_COMMAND: {
            WORD id = LOWORD(wParam);
            if (id == ID_TRAY_DISABLE_DISPLAY) {
                s_pending_toggle_display = true;
                return 0;
            } else if (id == ID_TRAY_RESTORE_WINDOW) {
                if (IsWindowVisible(hwnd) && !IsIconic(hwnd)) {
                    ShowWindow(hwnd, SW_HIDE);
                } else {
                    ShowWindow(hwnd, SW_SHOW);
                    ShowWindow(hwnd, SW_RESTORE);
                    SetForegroundWindow(hwnd);
                }
                return 0;
            } else if (id == ID_TRAY_EXIT) {
                s_should_exit = true;
                PostMessage(hwnd, WM_CLOSE, 0, 0);
                return 0;
            } else if (id == ID_TRAY_CHANGE_BG) {
                s_pending_open_file = true;
                return 0;
            } else if (id == ID_TRAY_STREAM_WINDOW) {
                s_pending_pick_stream = true;
                return 0;
            } else if (id == ID_TRAY_TOGGLE_EDIT) {
                s_pending_toggle_edit = true;
                return 0;
            } else if (id == ID_TRAY_FILL_CROP) {
                s_pending_fill_mode = 0;
                return 0;
            } else if (id == ID_TRAY_FILL_LETTERBOX) {
                s_pending_fill_mode = 1;
                return 0;
            } else if (id == ID_TRAY_FILL_ADAPT) {
                s_pending_fill_mode = 2;
                return 0;
            } else if (id == ID_TRAY_QUAL_100) {
                s_pending_quality = 100;
                return 0;
            } else if (id == ID_TRAY_QUAL_95) {
                s_pending_quality = 95;
                return 0;
            } else if (id == ID_TRAY_QUAL_90) {
                s_pending_quality = 90;
                return 0;
            } else if (id == ID_TRAY_QUAL_80) {
                s_pending_quality = 80;
                return 0;
            } else if (id == ID_TRAY_QUAL_60) {
                s_pending_quality = 60;
                return 0;
            }
            break;
        }

        case WM_TRAYICON:
            if (lParam == WM_LBUTTONUP || lParam == WM_LBUTTONDBLCLK) {
                if (IsWindowVisible(hwnd) && !IsIconic(hwnd)) {
                    ShowWindow(hwnd, SW_HIDE);
                } else {
                    ShowWindow(hwnd, SW_SHOW);
                    ShowWindow(hwnd, SW_RESTORE);
                    SetForegroundWindow(hwnd);
                }
                return 0;
            } else if (lParam == WM_RBUTTONUP || lParam == WM_CONTEXTMENU) {
                show_tray_context_menu();
                return 0;
            }
            break;

        default:
            break;
    }

    if (s_orig_wndproc) {
        return CallWindowProc(s_orig_wndproc, hwnd, uMsg, wParam, lParam);
    }
    return DefWindowProc(hwnd, uMsg, wParam, lParam);
}

} // namespace

TrayManager::TrayManager() = default;

TrayManager::~TrayManager() {
    Shutdown();
}

void TrayManager::Init(void* hwnd, ChangeBgCallback change_bg_cb) {
    if (!hwnd) return;
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    s_hwnd = (HWND)hwnd;
    s_on_change_bg = change_bg_cb;

    s_orig_wndproc = (WNDPROC)SetWindowLongPtr(s_hwnd, GWLP_WNDPROC, (LONG_PTR)tray_subclass_proc);

    ZeroMemory(&s_nid, sizeof(s_nid));
    s_nid.cbSize = sizeof(s_nid);
    s_nid.hWnd = s_hwnd;
    s_nid.uID = 1;
    s_nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    s_nid.uCallbackMessage = WM_TRAYICON;
    HICON hAppIcon = (HICON)LoadImageA(GetModuleHandle(nullptr), MAKEINTRESOURCEA(1), IMAGE_ICON, 0, 0, LR_DEFAULTSIZE | LR_SHARED);
    if (hAppIcon) {
        SendMessageA(s_hwnd, WM_SETICON, 1, (LPARAM)hAppIcon);
        SendMessageA(s_hwnd, WM_SETICON, 0, (LPARAM)hAppIcon);
        s_nid.hIcon = hAppIcon;
    } else {
        s_nid.hIcon = LoadIcon(nullptr, IDI_APPLICATION);
    }
    snprintf(s_nid.szTip, sizeof(s_nid.szTip), "JL LCD AIO Monitor (960x480)");

    Shell_NotifyIconA(NIM_ADD, &s_nid);
}

void TrayManager::Update() {
    MSG msg;
    while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    if (s_pending_open_file) {
        s_pending_open_file = false;
        OpenFileDialog();
    }
}

void TrayManager::Shutdown() {
    Shell_NotifyIconA(NIM_DELETE, &s_nid);
    if (s_orig_wndproc && s_hwnd) {
        SetWindowLongPtr(s_hwnd, GWLP_WNDPROC, (LONG_PTR)s_orig_wndproc);
        s_orig_wndproc = nullptr;
    }
    s_hwnd = nullptr;
}

bool TrayManager::IsDisplayDisabled() const {
    return s_display_disabled;
}

void TrayManager::SetDisplayDisabled(bool disabled) {
    s_display_disabled = disabled;
    update_tray_tooltip();
}

std::string TrayManager::OpenNativeFileDialog(void* hwnd) {
    char szFile[MAX_PATH] = {0};
    OPENFILENAMEA ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = (HWND)hwnd;
    ofn.lpstrFile = szFile;
    ofn.nMaxFile = sizeof(szFile);
    ofn.lpstrFilter = "Supported Media (*.mp4;*.png;*.jpg;*.jpeg;*.bmp)\0*.mp4;*.png;*.jpg;*.jpeg;*.bmp\0Videos (*.mp4)\0*.mp4\0Images (*.png;*.jpg;*.jpeg;*.bmp)\0*.png;*.jpg;*.jpeg;*.bmp\0All Files (*.*)\0*.*\0";
    ofn.nFilterIndex = 1;
    ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST | OFN_NOCHANGEDIR;

    if (GetOpenFileNameA(&ofn)) {
        return std::string(szFile);
    }
    return {};
}

void TrayManager::OpenFileDialog() {
    std::string path = OpenNativeFileDialog(s_hwnd);
    if (!path.empty() && s_on_change_bg) {
        s_on_change_bg(path);
    }
}

bool TrayManager::PickCustomColor(uint32_t* in_out_color) {
    if (!in_out_color) return false;
    CHOOSECOLORA cc{};
    cc.lStructSize = sizeof(cc);
    cc.hwndOwner = s_hwnd;
    cc.lpCustColors = s_cust_colors;

    uint32_t c = *in_out_color;
    BYTE r = (c >> 24) & 0xFF;
    BYTE g = (c >> 16) & 0xFF;
    BYTE b = (c >> 8) & 0xFF;
    cc.rgbResult = RGB(r, g, b);
    cc.Flags = CC_FULLOPEN | CC_RGBINIT;

    if (ChooseColorA(&cc)) {
        BYTE out_r = GetRValue(cc.rgbResult);
        BYTE out_g = GetGValue(cc.rgbResult);
        BYTE out_b = GetBValue(cc.rgbResult);
        *in_out_color = ((uint32_t)out_r << 24) | ((uint32_t)out_g << 16) | ((uint32_t)out_b << 8) | 0xFF;
        return true;
    }
    return false;
}

bool TrayManager::ConsumeToggleDisplay() {
    bool p = s_pending_toggle_display;
    s_pending_toggle_display = false;
    return p;
}

bool TrayManager::ConsumeToggleEdit() {
    bool p = s_pending_toggle_edit;
    s_pending_toggle_edit = false;
    return p;
}

bool TrayManager::ConsumePickStream() {
    bool p = s_pending_pick_stream;
    s_pending_pick_stream = false;
    return p;
}

int TrayManager::ConsumeFillModeRequest() {
    int m = s_pending_fill_mode;
    s_pending_fill_mode = -1;
    return m;
}

void TrayManager::SetActiveFillMode(int mode) {
    s_active_fill_mode = mode;
}

int TrayManager::ConsumeQualityRequest() {
    int q = s_pending_quality;
    s_pending_quality = -1;
    return q;
}

void TrayManager::SetActiveQuality(int percent) {
    s_active_quality = percent;
}

bool TrayManager::IsWindowVisible() const {
    return (s_hwnd != nullptr && ::IsWindowVisible(s_hwnd) && !IsIconic(s_hwnd));
}

void TrayManager::MinimizeWindow() {
    if (s_hwnd) {
        ShowWindow(s_hwnd, SW_HIDE);
    }
}

void TrayManager::RestoreWindow() {
    if (s_hwnd) {
        ShowWindow(s_hwnd, SW_SHOW);
        ShowWindow(s_hwnd, SW_RESTORE);
        SetForegroundWindow(s_hwnd);
    }
}

bool TrayManager::ShouldExit() const {
    return s_should_exit;
}

void TrayManager::TrimMemory() {
    SetProcessWorkingSetSize(GetCurrentProcess(), (SIZE_T)-1, (SIZE_T)-1);
    EmptyWorkingSet(GetCurrentProcess());
}

void TrayManager::Sleep(unsigned long ms) {
    MsgWaitForMultipleObjects(0, nullptr, FALSE, ms, QS_ALLINPUT);
}
