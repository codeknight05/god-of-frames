#pragma once

#include "types.h"

#include <string>
#include <vector>

class DashboardWriter {
public:
    explicit DashboardWriter(std::string outputPath);

    void Write(const TelemetrySnapshot& t,
               const std::vector<AiAction>& proposed,
               const std::vector<ActionResult>& executed,
               const std::vector<LearningStat>& learning,
               const std::vector<TelemetrySnapshot>& recent) const;

    const std::string& OutputPath() const { return outputPath_; }

private:
    std::string outputPath_;
    std::string Escape(const std::string& s) const;
};
