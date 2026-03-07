#pragma once

#include "types.h"

#include <map>
#include <string>
#include <vector>

class HistoryStore {
public:
    HistoryStore(const std::string& dataDir, const std::string& gameExe);

    void AppendSnapshot(const TelemetrySnapshot& t);
    void AppendResults(const std::vector<ActionResult>& results);

    void UpdateLearning(const std::vector<ActionResult>& results, double severityBefore, double severityAfter);
    double GetTokenScore(const std::string& token) const;
    double GetRecentAverageSeverity(size_t window = 24) const;
    std::vector<LearningStat> GetLearningStats() const;

private:
    std::string dataDir_;
    std::string gameExe_;
    std::string historyCsvPath_;
    std::string eventsLogPath_;
    std::string learningPath_;

    std::vector<TelemetrySnapshot> recent_;
    std::map<std::string, LearningStat> learning_;

    void LoadLearning();
    void SaveLearning() const;
    std::string Sanitize(const std::string& s) const;
};
