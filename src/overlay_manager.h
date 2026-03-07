#pragma once

#include "types.h"

#include <mutex>
#include <string>
#include <thread>

class OverlayManager {
public:
    enum class OverlayMode {
        Full,
        Mini,
        Hidden
    };

    OverlayManager();
    ~OverlayManager();

    bool Start();
    void Stop();
    void UpdateSnapshot(const TelemetrySnapshot& snapshot);
    void ShowAlert(const std::string& reason, int durationMs = 5000);
    bool GetActiveAlert(std::string* out);
    bool GetLatestSnapshot(TelemetrySnapshot* out) const;
    OverlayMode GetMode() const;
    OverlayMode CycleMode();

private:
#ifdef _WIN32
    void ThreadMain();

    mutable std::mutex mutex_;
    TelemetrySnapshot latest_{};
    bool hasSnapshot_ = false;
    std::string alertText_;
    unsigned long long alertExpireTickMs_ = 0;

    void* threadHandle_ = nullptr;
    unsigned int threadId_ = 0;
    void* hwnd_ = nullptr;
    bool running_ = false;
    OverlayMode mode_ = OverlayMode::Full;
#endif
};
