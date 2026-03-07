#pragma once

#include "types.h"

#include <vector>

class BottleneckAnalyzer {
public:
    std::vector<AiAction> BuildActions(const TelemetrySnapshot& t, double recentAvgSeverity) const;
};
