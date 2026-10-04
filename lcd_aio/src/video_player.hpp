#pragma once

#include <string>
#include <cstdint>
#include <memory>

struct video_player;

class VideoPlayer {
public:
    VideoPlayer();
    ~VideoPlayer();

    bool Open(const std::string& path);
    void Close();
    bool Update(float dt);
    const uint8_t* GetPixels(int* out_w, int* out_h) const;
    int GetNativeWidth() const;
    int GetNativeHeight() const;
    float GetFps() const;
    bool IsActive() const;

private:
    video_player* m_handle = nullptr;
};

class VideoCache {
public:
    static bool GetOptimizedPath(const std::string& input_path, std::string& out_cached_path);
    static bool NeedsOptimization(const std::string& input_path);
    static void StartBackground(const std::string& input_path);
    static bool PollCompleted(std::string& out_cached_path);
    static void Cancel();
};
