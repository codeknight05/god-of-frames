#pragma once

#include <string>
#include <vector>

struct TelemetrySnapshot {
    std::string gameExe;
    int pid = 0;
    double processCpuPercent = 0.0;
    double systemMemoryUsedPercent = 0.0;
    double processWorkingSetMB = 0.0;
    double inferredSeverity = 0.0;
    bool presentMonAvailable = false;
    double observedFps = -1.0;
    long long unixTimeSec = 0;
};

enum class ActionKind {
    AutoFix,
    Notify,
    Unknown
};

struct AiAction {
    ActionKind kind = ActionKind::Unknown;
    std::string token;
    std::string details;
    double confidence = 0.5;
    std::string source = "local";
};

struct ActionResult {
    std::string token;
    ActionKind kind = ActionKind::Unknown;
    bool attempted = false;
    bool success = false;
    std::string message;
};

struct LearningStat {
    std::string token;
    int attempts = 0;
    int successes = 0;
    double score = 0.5;
};

struct LoopConfig {
    std::string gameExe;
    int intervalSeconds = 1;
    double fpsAlertThreshold = 55.0;
    std::string dataDir = "data";
};

inline std::vector<std::string> kAllowedAutoFixTokens = {
    "SET_HIGH_PERF_POWER_PLAN",
    "SET_GAME_PRIORITY_HIGH",
    "TRIM_BACKGROUND_APPS"
};
