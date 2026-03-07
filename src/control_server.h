#pragma once

#include "settings.h"
#include "types.h"

#include <functional>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

struct SettingsPatch {
    std::vector<std::string> protectedApps;
    std::vector<std::string> trimTargets;
    std::vector<std::string> gameProcesses;
    std::string feedbackEndpoint;
};

struct WebUiState {
    mutable std::mutex mutex;
    TelemetrySnapshot latestSnapshot;
    bool hasSnapshot = false;
    std::string activeGameExe;
    AppSettings settings;
};

class ControlServer {
public:
    using UpdateCallback = std::function<void(const SettingsPatch&)>;

    ControlServer();
    ~ControlServer();

    bool Start(int port, std::shared_ptr<WebUiState> state, UpdateCallback onUpdate);
    void Stop();

    std::string Url() const;

private:
#ifdef _WIN32
    void ThreadMain();
    bool HandleClient(uintptr_t clientSocket);

    std::string BuildHtml() const;
    std::string BuildStateJson() const;
    std::string BuildFeedbackJson() const;

    static std::string ParsePath(const std::string& requestLine);
    static std::string UrlDecode(const std::string& s);
    static std::vector<std::string> ParseList(const std::string& value);
    static std::string EscapeJson(const std::string& s);
    static std::string SanitizeFeedbackField(const std::string& s);
    static std::string CurrentTimestamp();
    static std::vector<std::string> Split(const std::string& s, char delimiter);

    bool AppendFeedback(const std::string& category,
                        const std::string& message,
                        const std::string& contact,
                        const std::string& gameExe,
                        bool* forwarded) const;

    bool SendHttp(uintptr_t clientSocket,
                  int status,
                  const std::string& contentType,
                  const std::string& body) const;

    std::shared_ptr<WebUiState> state_;
    UpdateCallback onUpdate_;
    int port_ = 5055;
    bool running_ = false;
    void* threadHandle_ = nullptr;
    unsigned int threadId_ = 0;
    std::string feedbackPath_ = "data/feedback.log";
#endif
};
