#pragma once

#include <string>
#include <vector>

struct AppSettings {
    int intervalSeconds = 1;
    double fpsAlertThreshold = 55.0;
    bool autoElevate = true;
    bool openUiOnStart = true;
    std::string feedbackEndpoint;
    std::vector<std::string> gameProcesses = {
        "helldivers2.exe",
        "forhonor.exe"
    };
    std::vector<std::string> trimTargets = {
        "Overwolf.exe",
        "RadeonSoftware.exe",
        "XboxPcApp.exe"
    };
    std::vector<std::string> protectedApps = {
        "Discord.exe",
        "Steam.exe"
    };
};

class SettingsStore {
public:
    explicit SettingsStore(std::string path);

    AppSettings LoadOrCreateDefault() const;
    bool Save(const AppSettings& settings) const;

private:
    std::string path_;

    static std::vector<std::string> ParseList(const std::string& value);
    static std::string JoinList(const std::vector<std::string>& values);
    static std::string Trim(const std::string& s);
    static std::string ToLower(std::string s);
};
