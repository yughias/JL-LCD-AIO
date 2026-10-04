#include "hw_monitor.hpp"
#include <windows.h>
#include <iostream>
#include <algorithm>
#include <cstring>

typedef int (*nvmlInit_t)(void);
typedef int (*nvmlShutdown_t)(void);
typedef int (*nvmlDeviceGetHandleByIndex_t)(unsigned int, void**);
typedef int (*nvmlDeviceGetName_t)(void*, char*, unsigned int);
typedef int (*nvmlDeviceGetTemperature_t)(void*, int, unsigned int*);
typedef struct { unsigned int gpu; unsigned int memory; } nvmlUtilization_t;
typedef int (*nvmlDeviceGetUtilizationRates_t)(void*, nvmlUtilization_t*);
typedef struct { unsigned long long total; unsigned long long free; unsigned long long used; } nvmlMemory_t;
typedef int (*nvmlDeviceGetMemoryInfo_t)(void*, nvmlMemory_t*);

static uint64_t FiletimeToUll(const FILETIME& ft) {
    return (((uint64_t)ft.dwHighDateTime) << 32) | ft.dwLowDateTime;
}

HardwareMonitor::HardwareMonitor() = default;

HardwareMonitor::~HardwareMonitor() {
    Shutdown();
}

void HardwareMonitor::Init(const std::string& cpu_override, const std::string& gpu_override) {
    FILETIME idle{}, kernel{}, user{};
    if (GetSystemTimes(&idle, &kernel, &user)) {
        m_prev_idle = FiletimeToUll(idle);
        m_prev_kernel = FiletimeToUll(kernel);
        m_prev_user = FiletimeToUll(user);
    }

    HMODULE nvml = LoadLibraryA("nvml.dll");
    if (nvml) {
        m_nvml_module = nvml;
        auto nvmlInit = (nvmlInit_t)GetProcAddress(nvml, "nvmlInit_v2");
        if (!nvmlInit) nvmlInit = (nvmlInit_t)GetProcAddress(nvml, "nvmlInit");

        m_nvmlShutdown = (void*)GetProcAddress(nvml, "nvmlShutdown");
        auto nvmlGetHandle = (nvmlDeviceGetHandleByIndex_t)GetProcAddress(nvml, "nvmlDeviceGetHandleByIndex_v2");
        if (!nvmlGetHandle) nvmlGetHandle = (nvmlDeviceGetHandleByIndex_t)GetProcAddress(nvml, "nvmlDeviceGetHandleByIndex");

        m_nvmlGetName = (void*)GetProcAddress(nvml, "nvmlDeviceGetName");
        m_nvmlGetTemp = (void*)GetProcAddress(nvml, "nvmlDeviceGetTemperature");
        m_nvmlGetUtil = (void*)GetProcAddress(nvml, "nvmlDeviceGetUtilizationRates");
        m_nvmlGetMemory = (void*)GetProcAddress(nvml, "nvmlDeviceGetMemoryInfo");

        if (nvmlInit && nvmlInit() == 0) {
            if (nvmlGetHandle && nvmlGetHandle(0, &m_nvml_device) == 0) {
                m_nvml_available = true;
            }
        }
    }

    if (!cpu_override.empty()) {
        m_cached_cpu_name = cpu_override;
    } else {
        DetectCpuName();
    }

    if (!gpu_override.empty()) {
        m_cached_gpu_name = gpu_override;
    } else {
        DetectGpuName();
    }

    SampleHardware();
}

void HardwareMonitor::Shutdown() {
    if (m_nvml_available && m_nvmlShutdown) {
        ((nvmlShutdown_t)m_nvmlShutdown)();
        m_nvml_available = false;
        m_nvml_device = nullptr;
    }
    if (m_nvml_module) {
        FreeLibrary((HMODULE)m_nvml_module);
        m_nvml_module = nullptr;
    }
}

void HardwareMonitor::DetectCpuName() {
    HKEY hKey;
    if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, "HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0", 0, KEY_READ, &hKey) == ERROR_SUCCESS) {
        char raw[128] = {0};
        DWORD size = sizeof(raw);
        if (RegQueryValueExA(hKey, "ProcessorNameString", nullptr, nullptr, (LPBYTE)raw, &size) == ERROR_SUCCESS) {
            std::string s(raw);
            if (s.find("265K") != std::string::npos) {
                m_cached_cpu_name = "Intel 265K";
            } else if (s.find("Intel") != std::string::npos) {
                size_t last_space = s.rfind(' ');
                if (last_space != std::string::npos && last_space + 1 < s.size()) {
                    m_cached_cpu_name = "Intel " + s.substr(last_space + 1);
                } else {
                    m_cached_cpu_name = "Intel CPU";
                }
            } else if (s.find("AMD") != std::string::npos || s.find("Ryzen") != std::string::npos) {
                size_t last_space = s.rfind(' ');
                if (last_space != std::string::npos && last_space + 1 < s.size()) {
                    m_cached_cpu_name = "Ryzen " + s.substr(last_space + 1);
                } else {
                    m_cached_cpu_name = "AMD Ryzen";
                }
            } else {
                m_cached_cpu_name = s;
            }
        }
        RegCloseKey(hKey);
    }
}

void HardwareMonitor::DetectGpuName() {
    if (m_nvml_available && m_nvml_device && m_nvmlGetName) {
        char raw[128] = {0};
        if (((nvmlDeviceGetName_t)m_nvmlGetName)(m_nvml_device, raw, sizeof(raw)) == 0) {
            std::string s(raw);
            if (s.rfind("NVIDIA ", 0) == 0) s = s.substr(7);
            if (s.rfind("GeForce ", 0) == 0) s = s.substr(8);
            size_t first = s.find_first_not_of(' ');
            if (first != std::string::npos) s = s.substr(first);
            m_cached_gpu_name = s;
            return;
        }
    }

    for (int idx = 1; idx >= 0; idx--) {
        char subkey[128];
        snprintf(subkey, sizeof(subkey), "SYSTEM\\CurrentControlSet\\Control\\Class\\{4d36e968-e325-11ce-bfc1-08002be10318}\\%04d", idx);
        HKEY hKey;
        if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, subkey, 0, KEY_READ, &hKey) == ERROR_SUCCESS) {
            char desc[128] = {0};
            DWORD size = sizeof(desc);
            if (RegQueryValueExA(hKey, "DriverDesc", nullptr, nullptr, (LPBYTE)desc, &size) == ERROR_SUCCESS) {
                RegCloseKey(hKey);
                std::string s(desc);
                if (s.find("Basic Render") == std::string::npos && s.find("Intel(R) Graphics") == std::string::npos) {
                    if (s.rfind("NVIDIA ", 0) == 0) s = s.substr(7);
                    if (s.rfind("GeForce ", 0) == 0) s = s.substr(8);
                    if (s.rfind("RTX ", 0) == 0) s = s.substr(4);
                    size_t first = s.find_first_not_of(' ');
                    if (first != std::string::npos) s = s.substr(first);
                    m_cached_gpu_name = s;
                    return;
                }
            } else {
                RegCloseKey(hKey);
            }
        }
    }
}

void HardwareMonitor::SampleHardware() {
    FILETIME idle{}, kernel{}, user{};
    if (GetSystemTimes(&idle, &kernel, &user)) {
        uint64_t cur_idle = FiletimeToUll(idle);
        uint64_t cur_kernel = FiletimeToUll(kernel);
        uint64_t cur_user = FiletimeToUll(user);

        uint64_t diff_idle = cur_idle - m_prev_idle;
        uint64_t diff_kernel = cur_kernel - m_prev_kernel;
        uint64_t diff_user = cur_user - m_prev_user;
        uint64_t diff_total = diff_kernel + diff_user;

        m_prev_idle = cur_idle;
        m_prev_kernel = cur_kernel;
        m_prev_user = cur_user;

        if (diff_total > 0 && diff_total >= diff_idle) {
            m_raw_cpu_util = (float)(diff_total - diff_idle) * 100.0f / (float)diff_total;
        }
    }

    m_raw_cpu_temp = 38.0f + (m_raw_cpu_util / 100.0f) * 38.0f;

    if (m_nvml_available && m_nvml_device) {
        if (m_nvmlGetTemp) {
            unsigned int temp = 0;
            if (((nvmlDeviceGetTemperature_t)m_nvmlGetTemp)(m_nvml_device, 0, &temp) == 0) {
                m_raw_gpu_temp = (float)temp;
            }
        }
        if (m_nvmlGetUtil) {
            nvmlUtilization_t util{};
            if (((nvmlDeviceGetUtilizationRates_t)m_nvmlGetUtil)(m_nvml_device, &util) == 0) {
                m_raw_gpu_util = (float)util.gpu;
            }
        }
        if (m_nvmlGetMemory) {
            nvmlMemory_t mem{};
            if (((nvmlDeviceGetMemoryInfo_t)m_nvmlGetMemory)(m_nvml_device, &mem) == 0 && mem.total > 0) {
                m_raw_vram_total_gb = (float)mem.total / (1024.0f * 1024.0f * 1024.0f);
                m_raw_vram_used_gb = (float)mem.used / (1024.0f * 1024.0f * 1024.0f);
                m_raw_vram_util = (float)mem.used * 100.0f / (float)mem.total;
            }
        }
    } else {
        m_raw_gpu_temp = 41.0f + (m_raw_gpu_util / 100.0f) * 35.0f;
    }

    MEMORYSTATUSEX mem_status{};
    mem_status.dwLength = sizeof(mem_status);
    if (GlobalMemoryStatusEx(&mem_status)) {
        m_raw_ram_util = (float)mem_status.dwMemoryLoad;
        m_raw_ram_total_gb = (float)mem_status.ullTotalPhys / (1024.0f * 1024.0f * 1024.0f);
        m_raw_ram_used_gb = (float)(mem_status.ullTotalPhys - mem_status.ullAvailPhys) / (1024.0f * 1024.0f * 1024.0f);
    }

    ULARGE_INTEGER free_bytes{}, total_bytes{}, total_free{};
    if (GetDiskFreeSpaceExA("C:\\", &free_bytes, &total_bytes, &total_free) && total_bytes.QuadPart > 0) {
        m_raw_disk_total_gb = (float)total_bytes.QuadPart / (1024.0f * 1024.0f * 1024.0f);
        m_raw_disk_free_gb = (float)free_bytes.QuadPart / (1024.0f * 1024.0f * 1024.0f);
        uint64_t used = total_bytes.QuadPart - free_bytes.QuadPart;
        m_raw_disk_util = (float)used * 100.0f / (float)total_bytes.QuadPart;
    }

    m_stats.uptime_seconds = (uint64_t)(GetTickCount64() / 1000ULL);
}

void HardwareMonitor::Update(float delta_time, float refresh_interval) {
    if (refresh_interval < 0.2f) refresh_interval = 0.2f;
    m_sample_timer += delta_time;
    if (m_sample_timer >= refresh_interval) {
        m_sample_timer = 0.0f;
        SampleHardware();
    }

    float smooth = std::min(1.0f, delta_time * 3.5f);
    m_stats.cpu_util += (m_raw_cpu_util - m_stats.cpu_util) * smooth;
    m_stats.cpu_temp += (m_raw_cpu_temp - m_stats.cpu_temp) * (smooth * 0.5f);
    m_stats.gpu_util += (m_raw_gpu_util - m_stats.gpu_util) * smooth;
    m_stats.gpu_temp += (m_raw_gpu_temp - m_stats.gpu_temp) * (smooth * 0.5f);
    m_stats.ram_util += (m_raw_ram_util - m_stats.ram_util) * smooth;
    m_stats.vram_util += (m_raw_vram_util - m_stats.vram_util) * smooth;
    m_stats.disk_util += (m_raw_disk_util - m_stats.disk_util) * smooth;

    m_stats.cpu_name = m_cached_cpu_name;
    m_stats.gpu_name = m_cached_gpu_name;
    m_stats.ram_used_gb = m_raw_ram_used_gb;
    m_stats.ram_total_gb = m_raw_ram_total_gb;
    m_stats.vram_used_gb = m_raw_vram_used_gb;
    m_stats.vram_total_gb = m_raw_vram_total_gb;
    m_stats.disk_free_gb = m_raw_disk_free_gb;
    m_stats.disk_total_gb = m_raw_disk_total_gb;
}
