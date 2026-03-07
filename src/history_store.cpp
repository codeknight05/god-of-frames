#include "history_store.h"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <numeric>
#include <sstream>

namespace fs = std::filesystem;

HistoryStore::HistoryStore(const std::string& dataDir, const std::string& gameExe)
    : dataDir_(dataDir), gameExe_(gameExe) {
    fs::create_directories(dataDir_);
    const std::string base = Sanitize(gameExe_);
    historyCsvPath_ = (fs::path(dataDir_) / (base + ".history.csv")).string();
    eventsLogPath_ = (fs::path(dataDir_) / (base + ".events.log")).string();
    learningPath_ = (fs::path(dataDir_) / (base + ".learning.csv")).string();

    if (!fs::exists(historyCsvPath_)) {
        std::ofstream out(historyCsvPath_, std::ios::app);
        out << "ts,pid,cpu,sys_mem,proc_mem,fps,severity,presentmon\n";
    }

    LoadLearning();
}

std::string HistoryStore::Sanitize(const std::string& s) const {
    std::string out;
    out.reserve(s.size());
    for (char c : s) {
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '-') {
            out.push_back(c);
        } else {
            out.push_back('_');
        }
    }
    return out;
}

void HistoryStore::AppendSnapshot(const TelemetrySnapshot& t) {
    recent_.push_back(t);
    if (recent_.size() > 512) {
        recent_.erase(recent_.begin(), recent_.begin() + 128);
    }

    std::ofstream out(historyCsvPath_, std::ios::app);
    out << t.unixTimeSec << ',' << t.pid << ',' << t.processCpuPercent << ',' << t.systemMemoryUsedPercent << ','
        << t.processWorkingSetMB << ',' << t.observedFps << ',' << t.inferredSeverity << ',' << (t.presentMonAvailable ? 1 : 0) << '\n';
}

void HistoryStore::AppendResults(const std::vector<ActionResult>& results) {
    if (results.empty()) return;
    std::ofstream out(eventsLogPath_, std::ios::app);
    for (const auto& r : results) {
        out << r.token << "|" << (r.kind == ActionKind::AutoFix ? "AUTO_FIX" : "NOTIFY")
            << "|" << (r.attempted ? "attempted" : "skipped")
            << "|" << (r.success ? "success" : "no_success")
            << "|" << r.message << '\n';
    }
}

double HistoryStore::GetRecentAverageSeverity(size_t window) const {
    if (recent_.empty()) return 0.0;
    const size_t start = recent_.size() > window ? recent_.size() - window : 0;
    double sum = 0.0;
    size_t count = 0;
    for (size_t i = start; i < recent_.size(); ++i) {
        sum += recent_[i].inferredSeverity;
        ++count;
    }
    return count ? (sum / static_cast<double>(count)) : 0.0;
}

void HistoryStore::UpdateLearning(const std::vector<ActionResult>& results, double severityBefore, double severityAfter) {
    if (results.empty()) return;

    const double improvement = severityBefore - severityAfter;

    for (const auto& r : results) {
        if (r.kind != ActionKind::AutoFix || !r.attempted) continue;

        auto& stat = learning_[r.token];
        stat.token = r.token;
        stat.attempts++;

        bool improved = (improvement > 0.03 && r.success);
        if (improved) {
            stat.successes++;
        }

        const double alpha = 0.18;
        const double target = improved ? 1.0 : 0.0;
        stat.score = (1.0 - alpha) * stat.score + alpha * target;

        stat.score = std::clamp(stat.score, 0.01, 0.99);
    }

    SaveLearning();
}

double HistoryStore::GetTokenScore(const std::string& token) const {
    const auto it = learning_.find(token);
    if (it == learning_.end()) return 0.5;
    return it->second.score;
}

std::vector<LearningStat> HistoryStore::GetLearningStats() const {
    std::vector<LearningStat> out;
    for (const auto& kv : learning_) {
        out.push_back(kv.second);
    }
    std::sort(out.begin(), out.end(), [](const LearningStat& a, const LearningStat& b) {
        return a.score > b.score;
    });
    return out;
}

void HistoryStore::LoadLearning() {
    std::ifstream in(learningPath_);
    if (!in.is_open()) return;

    std::string line;
    while (std::getline(in, line)) {
        if (line.empty()) continue;
        std::istringstream iss(line);
        std::string token, attempts, successes, score;
        if (!std::getline(iss, token, ',')) continue;
        if (!std::getline(iss, attempts, ',')) continue;
        if (!std::getline(iss, successes, ',')) continue;
        if (!std::getline(iss, score, ',')) continue;

        LearningStat s;
        s.token = token;
        s.attempts = std::stoi(attempts);
        s.successes = std::stoi(successes);
        s.score = std::stod(score);
        learning_[token] = s;
    }
}

void HistoryStore::SaveLearning() const {
    std::ofstream out(learningPath_, std::ios::trunc);
    for (const auto& kv : learning_) {
        const auto& s = kv.second;
        out << s.token << ',' << s.attempts << ',' << s.successes << ',' << std::fixed << std::setprecision(4) << s.score << '\n';
    }
}
