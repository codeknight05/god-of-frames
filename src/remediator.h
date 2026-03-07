#pragma once

#include "types.h"

#include <vector>

class Remediator {
public:
    Remediator() = default;
    Remediator(std::vector<std::string> trimTargets, std::vector<std::string> protectedApps);

    void SetTrimPolicy(std::vector<std::string> trimTargets, std::vector<std::string> protectedApps);
    std::vector<ActionResult> Apply(const TelemetrySnapshot& t, const std::vector<AiAction>& actions) const;

private:
    bool IsAllowedAutoFixToken(const std::string& token) const;
    bool SetHighPerformancePlan() const;
    bool SetGamePriorityHigh(int pid) const;
    int TrimBackgroundApps() const;
    bool IsProtectedApp(const std::string& exeName) const;

    std::vector<std::string> trimTargets_;
    std::vector<std::string> protectedApps_;
};
