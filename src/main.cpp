#include "bottleneck_analyzer.h"
#include "control_server.h"
#include "dashboard_writer.h"
#include "gemini_client.h"
#include "history_store.h"
#include "overlay_manager.h"
#include "remediator.h"
#include "settings.h"
#include "system_monitor.h"
#include "types.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdlib>
#include <ctime>
#include <iostream>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_set>
#include <utility>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#include <shellapi.h>
#include <tlhelp32.h>
#endif

namespace {

struct RuntimeOptions {
    bool openUi = false;
    bool watchGames = false;
    bool autoElevate = true;
    bool installStartup = false;
    bool removeStartup = false;
    std::string settingsPath = "data/settings.conf";
    std::vector<std::string> protectedApps;
    std::vector<std::string> gamesOverride;
};

std::string ToLower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return s;
}

std::string Trim(const std::string& s) {
    const auto b = s.find_first_not_of(" \t\r\n");
    if (b == std::string::npos) return "";
    const auto e = s.find_last_not_of(" \t\r\n");
    return s.substr(b, e - b + 1);
}

std::vector<std::string> ParseList(const std::string& text) {
    std::vector<std::string> out;
    std::string cur;
    for (char c : text) {
        if (c == ';' || c == ',') {
            cur = Trim(cur);
            if (!cur.empty()) out.push_back(cur);
            cur.clear();
            continue;
        }
        cur.push_back(c);
    }
    cur = Trim(cur);
    if (!cur.empty()) out.push_back(cur);
    return out;
}

std::string ExtractSettingsPath(int argc, char** argv) {
    for (int i = 1; i + 1 < argc; ++i) {
        if (std::string(argv[i]) == "--settings") {
            return argv[i + 1];
        }
    }
    return "data/settings.conf";
}

void PrintUsage() {
    std::cout << "God of Frames - adaptive optimizer\n"
              << "Usage:\n"
              << "  god_of_frames --game <Game.exe> [--interval 1] [--fps-threshold 55] [--model gemini-1.5-flash] [--open-ui] [--no-elevate]\n"
              << "  god_of_frames --watch-games [--games helldivers2.exe,forhonor.exe] [--open-ui]\n"
              << "  god_of_frames --install-startup | --remove-startup\n\n"
              << "Options:\n"
              << "  --settings <path>       Settings file path (default data/settings.conf)\n"
              << "  --protect-app <exe>     Add app to never-close list (repeatable)\n"
              << "  --games <list>          Comma/semicolon list for watch mode\n"
              << "  --watch-games           Auto-attach when any configured game starts\n"
              << "  --install-startup       Start watcher automatically at Windows login\n"
              << "  --remove-startup        Remove startup entry\n"
              << "  --no-elevate            Skip UAC relaunch\n\n"
              << "Overlay:\n"
              << "  F10 cycles overlay modes: full -> mini FPS -> off\n";
}

bool ParseArgs(int argc, char** argv, LoopConfig& cfg, std::string& model, RuntimeOptions& opt) {
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "--game" && i + 1 < argc) {
            cfg.gameExe = argv[++i];
        } else if (a == "--interval" && i + 1 < argc) {
            cfg.intervalSeconds = std::max(1, std::atoi(argv[++i]));
        } else if (a == "--fps-threshold" && i + 1 < argc) {
            cfg.fpsAlertThreshold = std::atof(argv[++i]);
        } else if (a == "--model" && i + 1 < argc) {
            model = argv[++i];
        } else if (a == "--open-ui") {
            opt.openUi = true;
        } else if (a == "--watch-games") {
            opt.watchGames = true;
        } else if (a == "--games" && i + 1 < argc) {
            opt.gamesOverride = ParseList(argv[++i]);
        } else if (a == "--protect-app" && i + 1 < argc) {
            opt.protectedApps.push_back(argv[++i]);
        } else if (a == "--settings" && i + 1 < argc) {
            opt.settingsPath = argv[++i];
        } else if (a == "--no-elevate") {
            opt.autoElevate = false;
        } else if (a == "--install-startup") {
            opt.installStartup = true;
        } else if (a == "--remove-startup") {
            opt.removeStartup = true;
        } else if (a == "--help" || a == "-h") {
            return false;
        }
    }

    if (opt.installStartup || opt.removeStartup) return true;
    if (opt.watchGames) return true;
    return !cfg.gameExe.empty();
}

#ifdef _WIN32
bool IsRunningElevated() {
    HANDLE token = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) return false;
    TOKEN_ELEVATION elevation{};
    DWORD bytes = 0;
    const BOOL ok = GetTokenInformation(token, TokenElevation, &elevation, sizeof(elevation), &bytes);
    CloseHandle(token);
    return ok == TRUE && elevation.TokenIsElevated != 0;
}

std::wstring Utf8ToWide(const std::string& s) {
    if (s.empty()) return L"";
    const int size = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
    if (size <= 0) return L"";
    std::wstring ws(size, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, &ws[0], size);
    if (!ws.empty() && ws.back() == L'\0') ws.pop_back();
    return ws;
}

std::string WideToUtf8(const std::wstring& ws) {
    if (ws.empty()) return "";
    const int size = WideCharToMultiByte(CP_UTF8, 0, ws.c_str(), -1, nullptr, 0, nullptr, nullptr);
    if (size <= 0) return "";
    std::string out(size, '\0');
    WideCharToMultiByte(CP_UTF8, 0, ws.c_str(), -1, &out[0], size, nullptr, nullptr);
    if (!out.empty() && out.back() == '\0') out.pop_back();
    return out;
}

std::wstring QuoteArg(const std::string& arg) {
    std::wstring w = Utf8ToWide(arg);
    std::wstring out = L"\"";
    for (wchar_t c : w) {
        if (c == L'\"') out += L"\\\"";
        else out.push_back(c);
    }
    out += L"\"";
    return out;
}

bool RelaunchAsAdmin(int argc, char** argv) {
    wchar_t exePath[MAX_PATH] = {};
    if (GetModuleFileNameW(nullptr, exePath, MAX_PATH) == 0) return false;

    std::wstring params;
    for (int i = 1; i < argc; ++i) {
        if (!params.empty()) params += L" ";
        params += QuoteArg(argv[i]);
    }

    wchar_t cwd[MAX_PATH] = {};
    GetCurrentDirectoryW(MAX_PATH, cwd);

    SHELLEXECUTEINFOW sei{};
    sei.cbSize = sizeof(sei);
    sei.fMask = SEE_MASK_NOCLOSEPROCESS;
    sei.lpVerb = L"runas";
    sei.lpFile = exePath;
    sei.lpParameters = params.c_str();
    sei.lpDirectory = cwd;
    sei.nShow = SW_SHOWNORMAL;

    if (!ShellExecuteExW(&sei)) return false;
    if (sei.hProcess) CloseHandle(sei.hProcess);
    return true;
}

std::string FindFirstRunningGame(const std::vector<std::string>& gameExes) {
    if (gameExes.empty()) return "";

    std::unordered_set<std::string> running;

    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE) return "";

    PROCESSENTRY32 pe{};
    pe.dwSize = sizeof(pe);
    if (Process32First(snapshot, &pe)) {
        do {
            std::wstring ws(pe.szExeFile);
            std::string exe = WideToUtf8(ws);
            running.insert(ToLower(exe));
        } while (Process32Next(snapshot, &pe));
    }
    CloseHandle(snapshot);

    for (const auto& g : gameExes) {
        if (running.find(ToLower(g)) != running.end()) return g;
    }
    return "";
}

bool InstallStartupEntry(const std::string& settingsPath, bool openUi) {
    wchar_t exePath[MAX_PATH] = {};
    if (GetModuleFileNameW(nullptr, exePath, MAX_PATH) == 0) return false;

    std::wstring cmd = L"\"" + std::wstring(exePath) + L"\" --watch-games";
    if (openUi) cmd += L" --open-ui";
    cmd += L" --settings ";
    cmd += QuoteArg(settingsPath);

    HKEY key;
    if (RegCreateKeyExW(HKEY_CURRENT_USER,
                        L"Software\\Microsoft\\Windows\\CurrentVersion\\Run",
                        0, nullptr, 0, KEY_SET_VALUE, nullptr, &key, nullptr) != ERROR_SUCCESS) {
        return false;
    }

    const LONG rc = RegSetValueExW(key,
                                   L"GodOfFramesAutoStart",
                                   0,
                                   REG_SZ,
                                   reinterpret_cast<const BYTE*>(cmd.c_str()),
                                   static_cast<DWORD>((cmd.size() + 1) * sizeof(wchar_t)));
    RegCloseKey(key);
    return rc == ERROR_SUCCESS;
}

bool RemoveStartupEntry() {
    HKEY key;
    if (RegOpenKeyExW(HKEY_CURRENT_USER,
                      L"Software\\Microsoft\\Windows\\CurrentVersion\\Run",
                      0,
                      KEY_SET_VALUE,
                      &key) != ERROR_SUCCESS) {
        return false;
    }

    const LONG rc = RegDeleteValueW(key, L"GodOfFramesAutoStart");
    RegCloseKey(key);
    return rc == ERROR_SUCCESS || rc == ERROR_FILE_NOT_FOUND;
}

void OpenUrl(const std::string& url) {
    std::string cmd = "start \"\" \"" + url + "\"";
    std::system(cmd.c_str());
}
#endif

std::vector<AiAction> MergeAndRankActions(std::vector<AiAction> localActions,
                                          std::vector<AiAction> aiActions,
                                          const HistoryStore& history) {
    std::vector<AiAction> merged;
    merged.reserve(localActions.size() + aiActions.size());
    merged.insert(merged.end(), localActions.begin(), localActions.end());
    merged.insert(merged.end(), aiActions.begin(), aiActions.end());

    std::map<std::string, AiAction> dedup;
    for (auto a : merged) {
        const std::string key = std::to_string(static_cast<int>(a.kind)) + "|" + a.token;
        if (a.kind == ActionKind::AutoFix) {
            a.confidence += 0.25 * (history.GetTokenScore(a.token) - 0.5);
        }
        auto it = dedup.find(key);
        if (it == dedup.end() || a.confidence > it->second.confidence) {
            dedup[key] = a;
        }
    }

    std::vector<AiAction> out;
    for (const auto& kv : dedup) out.push_back(kv.second);
    std::sort(out.begin(), out.end(), [](const AiAction& a, const AiAction& b) {
        return a.confidence > b.confidence;
    });
    if (out.size() > 6) out.resize(6);
    return out;
}

void PrintSnapshot(const TelemetrySnapshot& t) {
    const std::string fpsText = (t.observedFps >= 0.0) ? std::to_string(t.observedFps) : "N/A";
    std::cout << "[telemetry] pid=" << t.pid
              << " cpu=" << t.processCpuPercent << "%"
              << " mem(sys)=" << t.systemMemoryUsedPercent << "%"
              << " mem(game)=" << t.processWorkingSetMB << "MB"
              << " fps=" << fpsText
              << " severity=" << t.inferredSeverity
              << (t.presentMonAvailable ? " [presentmon]" : " [no-presentmon]")
              << "\n";
}

void PrintResults(const std::vector<ActionResult>& results) {
    for (const auto& r : results) {
        if (r.kind == ActionKind::Notify) {
            std::cout << "[notify] " << r.token << " - " << r.message << "\n";
        } else {
            std::cout << "[auto-fix] " << r.token << " - " << r.message << (r.success ? " [ok]" : " [failed]") << "\n";
        }
    }
}

bool IsSevereFpsDrop(const TelemetrySnapshot& t, const LoopConfig& cfg) {
    if (t.observedFps <= 0.0) return false;
    const double severeThreshold = std::max(20.0, cfg.fpsAlertThreshold * 0.60);
    return t.observedFps < severeThreshold;
}

std::string BuildSevereDropReason(const TelemetrySnapshot& t) {
    if (t.processCpuPercent >= 90.0) {
        return "CPU bottleneck";
    }
    if (t.systemMemoryUsedPercent >= 90.0 || t.processWorkingSetMB >= 7000.0) {
        return "Memory pressure";
    }
    if (t.presentMonAvailable && t.observedFps > 0.0) {
        return "Likely GPU bound";
    }
    return "Background contention";
}

} // namespace

int main(int argc, char** argv) {
    LoopConfig cfg;
    std::string model = "gemini-1.5-flash";

    RuntimeOptions opt;
    opt.settingsPath = ExtractSettingsPath(argc, argv);

    SettingsStore settingsStore(opt.settingsPath);
    AppSettings settings = settingsStore.LoadOrCreateDefault();

    cfg.intervalSeconds = settings.intervalSeconds;
    cfg.fpsAlertThreshold = settings.fpsAlertThreshold;
    opt.autoElevate = settings.autoElevate;
    opt.openUi = settings.openUiOnStart;

    if (!ParseArgs(argc, argv, cfg, model, opt)) {
        PrintUsage();
        return 1;
    }

#ifdef _WIN32
    if (opt.installStartup) {
        const bool ok = InstallStartupEntry(opt.settingsPath, opt.openUi);
        std::cout << (ok ? "Startup entry installed.\n" : "Failed to install startup entry.\n");
        return ok ? 0 : 1;
    }
    if (opt.removeStartup) {
        const bool ok = RemoveStartupEntry();
        std::cout << (ok ? "Startup entry removed.\n" : "Failed to remove startup entry.\n");
        return ok ? 0 : 1;
    }
#else
    if (opt.installStartup || opt.removeStartup) {
        std::cout << "Startup install/remove is currently implemented for Windows only.\n";
        return 1;
    }
#endif

    if (!opt.protectedApps.empty()) {
        for (const auto& p : opt.protectedApps) {
            bool exists = false;
            for (const auto& e : settings.protectedApps) {
                if (ToLower(e) == ToLower(p)) {
                    exists = true;
                    break;
                }
            }
            if (!exists) settings.protectedApps.push_back(p);
        }
        settingsStore.Save(settings);
    }

    std::vector<std::string> watchList = !opt.gamesOverride.empty() ? opt.gamesOverride : settings.gameProcesses;

    if (!opt.watchGames && cfg.gameExe.empty()) {
        std::cout << "Missing --game (or use --watch-games).\n";
        PrintUsage();
        return 1;
    }
    if (opt.watchGames && watchList.empty()) {
        std::cout << "Watch mode requires at least one game process in settings or --games.\n";
        return 1;
    }

#ifdef _WIN32
    if (opt.autoElevate && !IsRunningElevated()) {
        std::cout << "Requesting Administrator permission for reliable FPS capture...\n";
        if (RelaunchAsAdmin(argc, argv)) return 0;
        std::cout << "Elevation was not granted. FPS may show as N/A.\n";
    }
#endif

    const char* key = std::getenv("GEMINI_API_KEY");
    GeminiClient gemini(key ? key : "", model);
    SystemMonitor monitor;
    Remediator remediator(settings.trimTargets, settings.protectedApps);
    BottleneckAnalyzer analyzer;
    OverlayManager overlay;

    std::unique_ptr<HistoryStore> history;
    std::unique_ptr<DashboardWriter> dashboard;

    std::vector<TelemetrySnapshot> recent;
    std::vector<ActionResult> previousResults;
    double previousSeverity = -1.0;
    bool wasSevereDrop = false;

    auto webState = std::make_shared<WebUiState>();
    {
        std::lock_guard<std::mutex> lock(webState->mutex);
        webState->settings = settings;
        webState->activeGameExe = cfg.gameExe;
    }

    std::mutex patchMutex;
    bool hasPendingPatch = false;
    SettingsPatch pendingPatch;

    ControlServer controlServer;
    bool controlStarted = controlServer.Start(5055, webState, [&](const SettingsPatch& patch) {
        std::lock_guard<std::mutex> lock(patchMutex);
        pendingPatch = patch;
        hasPendingPatch = true;
    });

    std::cout << "Starting God of Frames\n";
    if (controlStarted) {
        std::cout << "Control UI: " << controlServer.Url() << "\n";
    }
    if (overlay.Start()) {
        std::cout << "Overlay: enabled (F10 cycles full -> mini -> off)\n";
    }

    if (opt.watchGames) {
        std::cout << "Watch mode: waiting for configured games (" << watchList.size() << " entries).\n";
    } else {
        std::cout << "Target game: " << cfg.gameExe << "\n";
        history = std::make_unique<HistoryStore>(cfg.dataDir, cfg.gameExe);
        dashboard = std::make_unique<DashboardWriter>(cfg.dataDir + "/dashboard.html");
    }

#ifdef _WIN32
    if (opt.openUi) {
        if (controlStarted) OpenUrl(controlServer.Url());
        else OpenUrl(cfg.dataDir + "/dashboard.html");
    }
#endif

    if (!gemini.IsConfigured()) {
        std::cout << "GEMINI_API_KEY not set. Local analyzer + learning mode only.\n";
    }

    for (;;) {
        {
            std::lock_guard<std::mutex> lock(patchMutex);
            if (hasPendingPatch) {
                settings.protectedApps = pendingPatch.protectedApps;
                settings.trimTargets = pendingPatch.trimTargets;
                settings.gameProcesses = pendingPatch.gameProcesses;
                settings.feedbackEndpoint = pendingPatch.feedbackEndpoint;

                settingsStore.Save(settings);
                remediator.SetTrimPolicy(settings.trimTargets, settings.protectedApps);
                watchList = settings.gameProcesses;

                {
                    std::lock_guard<std::mutex> ws(webState->mutex);
                    webState->settings = settings;
                }

                hasPendingPatch = false;
                std::cout << "[ui] Settings updated from control website.\n";
            }
        }

        if (opt.watchGames && cfg.gameExe.empty()) {
#ifdef _WIN32
            const std::string detected = FindFirstRunningGame(watchList);
            if (detected.empty()) {
                std::this_thread::sleep_for(std::chrono::seconds(cfg.intervalSeconds));
                continue;
            }

            cfg.gameExe = detected;
            history = std::make_unique<HistoryStore>(cfg.dataDir, cfg.gameExe);
            dashboard = std::make_unique<DashboardWriter>(cfg.dataDir + "/dashboard.html");
            previousResults.clear();
            previousSeverity = -1.0;
            recent.clear();
            std::cout << "[watch] Detected game: " << cfg.gameExe << "\n";

            std::lock_guard<std::mutex> ws(webState->mutex);
            webState->activeGameExe = cfg.gameExe;
#endif
        }

        auto snap = monitor.Capture(cfg);
        if (!snap.has_value()) {
            if (opt.watchGames && !cfg.gameExe.empty()) {
                std::cout << "[watch] Game ended: " << cfg.gameExe << "\n";
                cfg.gameExe.clear();
                history.reset();
                dashboard.reset();
                previousResults.clear();
                previousSeverity = -1.0;
                recent.clear();
                wasSevereDrop = false;

                std::lock_guard<std::mutex> ws(webState->mutex);
                webState->hasSnapshot = false;
                webState->activeGameExe.clear();
            }

            std::this_thread::sleep_for(std::chrono::seconds(cfg.intervalSeconds));
            continue;
        }

        if (!history) history = std::make_unique<HistoryStore>(cfg.dataDir, cfg.gameExe);
        if (!dashboard) dashboard = std::make_unique<DashboardWriter>(cfg.dataDir + "/dashboard.html");

        snap->unixTimeSec = static_cast<long long>(std::time(nullptr));
        overlay.UpdateSnapshot(*snap);
        const bool severeDropNow = IsSevereFpsDrop(*snap, cfg);
        if (severeDropNow && !wasSevereDrop) {
            overlay.ShowAlert(BuildSevereDropReason(*snap), 5000);
        }
        wasSevereDrop = severeDropNow;

        {
            std::lock_guard<std::mutex> ws(webState->mutex);
            webState->latestSnapshot = *snap;
            webState->hasSnapshot = true;
            webState->activeGameExe = cfg.gameExe;
            webState->settings = settings;
        }

        if (previousSeverity >= 0.0 && !previousResults.empty()) {
            history->UpdateLearning(previousResults, previousSeverity, snap->inferredSeverity);
        }

        history->AppendSnapshot(*snap);
        recent.push_back(*snap);
        if (recent.size() > 120) recent.erase(recent.begin(), recent.begin() + 20);

        PrintSnapshot(*snap);

        auto localActions = analyzer.BuildActions(*snap, history->GetRecentAverageSeverity());
        std::vector<AiAction> aiActions;

        if (gemini.IsConfigured() && snap->inferredSeverity >= 0.30) {
            aiActions = gemini.Analyze(*snap);
            for (auto& a : aiActions) {
                a.source = "gemini";
                if (a.confidence <= 0.0) a.confidence = 0.63;
            }
        }

        auto plannedActions = MergeAndRankActions(localActions, aiActions, *history);
        auto results = remediator.Apply(*snap, plannedActions);

        PrintResults(results);
        history->AppendResults(results);
        dashboard->Write(*snap, plannedActions, results, history->GetLearningStats(), recent);

        previousSeverity = snap->inferredSeverity;
        previousResults = results;

        std::this_thread::sleep_for(std::chrono::seconds(cfg.intervalSeconds));
    }
}

