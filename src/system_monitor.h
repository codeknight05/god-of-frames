#pragma once

#include "types.h"

#include <optional>

class SystemMonitor {
public:
    std::optional<TelemetrySnapshot> Capture(const LoopConfig& cfg);

private:
    int FindPidByExeName(const std::string& exeName) const;
    double GetSystemMemoryUsedPercent() const;
    double GetProcessWorkingSetMB(void* processHandle) const;
    double GetProcessCpuPercent(int pid);
    double GetObservedFpsViaPresentMon(int pid, const std::string& exeName, bool* presentMonFound) const;
    double InferSeverity(const TelemetrySnapshot& t, const LoopConfig& cfg) const;
};
