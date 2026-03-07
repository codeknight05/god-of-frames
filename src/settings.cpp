#include "settings.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <sstream>

SettingsStore::SettingsStore(std::string path) : path_(std::move(path)) {}

std::string SettingsStore::Trim(const std::string& s) {
    const auto begin = s.find_first_not_of(" \t\r\n");
    if (begin == std::string::npos) return "";
    const auto end = s.find_last_not_of(" \t\r\n");
    return s.substr(begin, end - begin + 1);
}

std::string SettingsStore::ToLower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return s;
}

std::vector<std::string> SettingsStore::ParseList(const std::string& value) {
    std::vector<std::string> out;
    std::stringstream ss(value);
    std::string item;
    while (std::getline(ss, item, ';')) {
        item = Trim(item);
        if (!item.empty()) out.push_back(item);
    }
    return out;
}

std::string SettingsStore::JoinList(const std::vector<std::string>& values) {
    std::string out;
    for (size_t i = 0; i < values.size(); ++i) {
        if (i) out += ';';
        out += values[i];
    }
    return out;
}

bool SettingsStore::Save(const AppSettings& settings) const {
    std::filesystem::path p(path_);
    std::filesystem::create_directories(p.parent_path());

    std::ofstream out(path_, std::ios::trunc);
    if (!out.is_open()) return false;

    out << "# God of Frames settings\n";
    out << "interval_seconds=" << settings.intervalSeconds << "\n";
    out << "fps_threshold=" << settings.fpsAlertThreshold << "\n";
    out << "auto_elevate=" << (settings.autoElevate ? "true" : "false") << "\n";
    out << "open_ui_on_start=" << (settings.openUiOnStart ? "true" : "false") << "\n";
    out << "feedback_endpoint=" << settings.feedbackEndpoint << "\n";
    out << "game_processes=" << JoinList(settings.gameProcesses) << "\n";
    out << "trim_targets=" << JoinList(settings.trimTargets) << "\n";
    out << "protected_apps=" << JoinList(settings.protectedApps) << "\n";

    return true;
}

AppSettings SettingsStore::LoadOrCreateDefault() const {
    AppSettings settings;

    std::ifstream in(path_);
    if (!in.is_open()) {
        Save(settings);
        return settings;
    }

    std::string line;
    while (std::getline(in, line)) {
        line = Trim(line);
        if (line.empty() || line[0] == '#') continue;

        const auto eq = line.find('=');
        if (eq == std::string::npos) continue;

        std::string key = ToLower(Trim(line.substr(0, eq)));
        std::string value = Trim(line.substr(eq + 1));

        if (key == "interval_seconds") {
            try { settings.intervalSeconds = std::max(1, std::stoi(value)); } catch (...) {}
        } else if (key == "fps_threshold") {
            try { settings.fpsAlertThreshold = std::stod(value); } catch (...) {}
        } else if (key == "auto_elevate") {
            const std::string v = ToLower(value);
            settings.autoElevate = (v == "true" || v == "1" || v == "yes");
        } else if (key == "open_ui_on_start") {
            const std::string v = ToLower(value);
            settings.openUiOnStart = (v == "true" || v == "1" || v == "yes");
        } else if (key == "feedback_endpoint") {
            settings.feedbackEndpoint = value;
        } else if (key == "game_processes") {
            auto parsed = ParseList(value);
            if (!parsed.empty()) settings.gameProcesses = std::move(parsed);
        } else if (key == "trim_targets") {
            auto parsed = ParseList(value);
            if (!parsed.empty()) settings.trimTargets = std::move(parsed);
        } else if (key == "protected_apps") {
            settings.protectedApps = ParseList(value);
        }
    }

    Save(settings);
    return settings;
}

