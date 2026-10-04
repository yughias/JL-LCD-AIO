#pragma once

#include <string>
#include <cstdint>

struct HardwareStats {
    std::string cpu_name = "Intel CPU";
    float cpu_temp = 40.0f;       // degrees Celsius
    float cpu_util = 10.0f;       // 0.0f - 100.0f percent

    std::string gpu_name = "GPU";
    float gpu_temp = 42.0f;       // degrees Celsius
    float gpu_util = 5.0f;        // 0.0f - 100.0f percent

    float ram_util = 35.0f;       // 0.0f - 100.0f percent
    float ram_used_gb = 11.2f;    // GB used
    float ram_total_gb = 32.0f;   // GB total

    float vram_util = 20.0f;      // 0.0f - 100.0f percent
    float vram_used_gb = 3.2f;    // VRAM GB used
    float vram_total_gb = 16.0f;  // VRAM GB total

    float disk_util = 45.0f;      // 0.0f - 100.0f percent
    float disk_free_gb = 240.0f;  // Disk GB free
    float disk_total_gb = 512.0f; // Disk GB total

    uint64_t uptime_seconds = 0;  // System uptime
    int fps = 60;                 // Live frame rate
};

class HardwareMonitor {
public:
    HardwareMonitor();
    ~HardwareMonitor();

    void Init(const std::string& cpu_override = "", const std::string& gpu_override = "");
    void Shutdown();
    void Update(float delta_time, float refresh_interval);

    const HardwareStats& GetStats() const { return m_stats; }
    HardwareStats& GetStatsMutable() { return m_stats; }
    const std::string& GetCpuName() const { return m_cached_cpu_name; }
    const std::string& GetGpuName() const { return m_cached_gpu_name; }

private:
    void SampleHardware();
    void DetectCpuName();
    void DetectGpuName();

    HardwareStats m_stats;
    std::string m_cached_cpu_name = "Intel CPU";
    std::string m_cached_gpu_name = "GPU";

    uint64_t m_prev_idle = 0;
    uint64_t m_prev_kernel = 0;
    uint64_t m_prev_user = 0;

    float m_sample_timer = 0.0f;
    float m_raw_cpu_util = 10.0f;
    float m_raw_cpu_temp = 40.0f;
    float m_raw_gpu_util = 5.0f;
    float m_raw_gpu_temp = 42.0f;
    float m_raw_ram_util = 35.0f;
    float m_raw_ram_used_gb = 11.2f;
    float m_raw_ram_total_gb = 32.0f;
    float m_raw_vram_util = 20.0f;
    float m_raw_vram_used_gb = 3.2f;
    float m_raw_vram_total_gb = 16.0f;
    float m_raw_disk_util = 45.0f;
    float m_raw_disk_free_gb = 240.0f;
    float m_raw_disk_total_gb = 512.0f;

    void* m_nvml_module = nullptr;
    void* m_nvml_device = nullptr;
    bool m_nvml_available = false;

    // Function pointers for NVML
    void* m_nvmlShutdown = nullptr;
    void* m_nvmlGetName = nullptr;
    void* m_nvmlGetTemp = nullptr;
    void* m_nvmlGetUtil = nullptr;
    void* m_nvmlGetMemory = nullptr;
};
