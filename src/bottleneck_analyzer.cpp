#include "bottleneck_analyzer.h"

#include <algorithm>

std::vector<AiAction> BottleneckAnalyzer::BuildActions(const TelemetrySnapshot& t, double recentAvgSeverity) const {
    std::vector<AiAction> out;

    const bool fpsLow = (t.observedFps > 0.0 && t.observedFps < 55.0);
    const bool cpuBound = t.processCpuPercent >= 88.0;
    const bool memBound = t.systemMemoryUsedPercent >= 88.0 || t.processWorkingSetMB >= 7000.0;
    const bool severitySpike = t.inferredSeverity > (recentAvgSeverity + 0.2);

    if (cpuBound) {
        out.push_back({ActionKind::AutoFix, "SET_GAME_PRIORITY_HIGH", "CPU bottleneck detected.", 0.84, "local"});
        out.push_back({ActionKind::AutoFix, "TRIM_BACKGROUND_APPS", "CPU pressure high, trimming known background apps.", 0.70, "local"});
    }

    if (memBound) {
        out.push_back({ActionKind::AutoFix, "TRIM_BACKGROUND_APPS", "Memory pressure detected.", 0.77, "local"});
        out.push_back({ActionKind::Notify, "REDUCE_TEXTURE_QUALITY", "Likely VRAM/system-memory pressure. Lower texture quality and close browser tabs.", 0.71, "local"});
    }

    if (fpsLow && !cpuBound && !memBound) {
        out.push_back({ActionKind::Notify, "GPU_BOUND", "FPS is low without CPU/RAM saturation. Lower GPU-heavy settings or update GPU driver.", 0.69, "local"});
    }

    if (severitySpike) {
        out.push_back({ActionKind::AutoFix, "SET_HIGH_PERF_POWER_PLAN", "Sudden degradation detected versus baseline.", 0.74, "local"});
    }

    if (out.empty() && t.inferredSeverity >= 0.70) {
        out.push_back({ActionKind::AutoFix, "SET_HIGH_PERF_POWER_PLAN", "High severity fallback rule.", 0.60, "local"});
        out.push_back({ActionKind::AutoFix, "SET_GAME_PRIORITY_HIGH", "High severity fallback rule.", 0.60, "local"});
    }

    // De-duplicate by token preferring higher confidence.
    std::vector<AiAction> dedup;
    for (const auto& a : out) {
        auto it = std::find_if(dedup.begin(), dedup.end(), [&](const AiAction& x) {
            return x.kind == a.kind && x.token == a.token;
        });
        if (it == dedup.end()) {
            dedup.push_back(a);
        } else if (a.confidence > it->confidence) {
            *it = a;
        }
    }

    return dedup;
}
