#include "system_monitor.h"

#ifdef _WIN32

#include <windows.h>
#include <tlhelp32.h>
#include <psapi.h>

#include <algorithm>
#include <cctype>
#include <array>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <optional>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

namespace {

std::string ToLower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return s;
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

ULONGLONG FileTimeToUInt64(const FILETIME& ft) {
    ULARGE_INTEGER v;
    v.LowPart = ft.dwLowDateTime;
    v.HighPart = ft.dwHighDateTime;
    return v.QuadPart;
}

std::string Trim(const std::string& s) {
    const auto first = s.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return "";
    const auto last = s.find_last_not_of(" \t\r\n");
    return s.substr(first, last - first + 1);
}

std::vector<std::string> SplitCsvLine(const std::string& line) {
    std::vector<std::string> out;
    std::string cur;
    bool inQuotes = false;
    for (char c : line) {
        if (c == '"') {
            inQuotes = !inQuotes;
            continue;
        }
        if (c == ',' && !inQuotes) {
            out.push_back(Trim(cur));
            cur.clear();
            continue;
        }
        cur.push_back(c);
    }
    out.push_back(Trim(cur));
    return out;
}

std::string FindPresentMonPath() {
    std::array<char, 512> buffer{};
    std::string output;
    FILE* pipe = _popen("where presentmon.exe 2>nul", "r");
    if (pipe) {
        while (fgets(buffer.data(), static_cast<int>(buffer.size()), pipe) != nullptr) {
            output += buffer.data();
        }
        _pclose(pipe);
    }

    output = Trim(output);
    if (!output.empty()) {
        auto newline = output.find('\n');
        if (newline != std::string::npos) {
            output = output.substr(0, newline);
        }
        output = Trim(output);
        if (!output.empty() && std::filesystem::exists(output)) {
            return output;
        }
    }

    const char* localAppData = std::getenv("LOCALAPPDATA");
    if (localAppData) {
        std::filesystem::path fallback =
            std::filesystem::path(localAppData) /
            "Microsoft/WinGet/Packages/Intel.PresentMon.Console_Microsoft.Winget.Source_8wekyb3d8bbwe/presentmon.exe";
        if (std::filesystem::exists(fallback)) {
            return fallback.string();
        }
    }

    return "";
}

} // namespace

int SystemMonitor::FindPidByExeName(const std::string& exeName) const {
    const std::string wanted = ToLower(exeName);

    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE) {
        return 0;
    }

    PROCESSENTRY32 pe;
    pe.dwSize = sizeof(PROCESSENTRY32);

    if (!Process32First(snapshot, &pe)) {
        CloseHandle(snapshot);
        return 0;
    }

    do {
        std::wstring ws(pe.szExeFile);
        std::string name = WideToUtf8(ws);
        if (ToLower(name) == wanted) {
            CloseHandle(snapshot);
            return static_cast<int>(pe.th32ProcessID);
        }
    } while (Process32Next(snapshot, &pe));

    CloseHandle(snapshot);
    return 0;
}

double SystemMonitor::GetSystemMemoryUsedPercent() const {
    MEMORYSTATUSEX mem{};
    mem.dwLength = sizeof(mem);
    if (!GlobalMemoryStatusEx(&mem)) {
        return 0.0;
    }
    return static_cast<double>(mem.dwMemoryLoad);
}

double SystemMonitor::GetProcessWorkingSetMB(void* processHandle) const {
    PROCESS_MEMORY_COUNTERS pmc{};
    if (!GetProcessMemoryInfo(static_cast<HANDLE>(processHandle), &pmc, sizeof(pmc))) {
        return 0.0;
    }
    return static_cast<double>(pmc.WorkingSetSize) / (1024.0 * 1024.0);
}

double SystemMonitor::GetProcessCpuPercent(int pid) {
    static std::unordered_map<int, std::pair<ULONGLONG, ULONGLONG>> previous;

    HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, static_cast<DWORD>(pid));
    if (!process) {
        return 0.0;
    }

    FILETIME c{}, e{}, k{}, u{};
    if (!GetProcessTimes(process, &c, &e, &k, &u)) {
        CloseHandle(process);
        return 0.0;
    }

    FILETIME now{};
    GetSystemTimeAsFileTime(&now);

    ULONGLONG procTime = FileTimeToUInt64(k) + FileTimeToUInt64(u);
    ULONGLONG wallTime = FileTimeToUInt64(now);

    SYSTEM_INFO si{};
    GetSystemInfo(&si);
    const ULONGLONG cores = std::max<ULONGLONG>(1ULL, static_cast<ULONGLONG>(si.dwNumberOfProcessors));

    double cpu = 0.0;
    const auto it = previous.find(pid);
    if (it != previous.end()) {
        ULONGLONG deltaProc = procTime - it->second.first;
        ULONGLONG deltaWall = wallTime - it->second.second;
        if (deltaWall > 0) {
            cpu = (100.0 * static_cast<double>(deltaProc)) / (static_cast<double>(deltaWall) * static_cast<double>(cores));
        }
    }

    previous[pid] = {procTime, wallTime};
    CloseHandle(process);
    return std::clamp(cpu, 0.0, 100.0);
}

double SystemMonitor::GetObservedFpsViaPresentMon(int pid, const std::string& exeName, bool* presentMonFound) const {
    if (presentMonFound) {
        *presentMonFound = false;
    }

    const std::string presentMonPath = FindPresentMonPath();
    if (presentMonPath.empty()) {
        return -1.0;
    }
    if (presentMonFound) {
        *presentMonFound = true;
    }

    // v2.x emits CSV rows; compute FPS from average frame-time (msBetweenPresents).
    const auto nowMs = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
    const std::string sessionName = "GodOfFrames_" + std::to_string(pid) + "_" + std::to_string(nowMs);
    std::string command =
        "\"" + presentMonPath +
        "\" --session_name " + sessionName + " --timed 1 --output_stdout --no_console_stats --v2_metrics --terminate_after_timed 2>&1";

    std::array<char, 512> buffer{};
    std::string output;

    FILE* pipe = _popen(command.c_str(), "r");
    if (!pipe) {
        return -1.0;
    }

    while (fgets(buffer.data(), static_cast<int>(buffer.size()), pipe) != nullptr) {
        output += buffer.data();
    }

    _pclose(pipe);
    if (output.empty()) {
        return -1.0;
    }

    const std::string lowerOut = ToLower(output);
    if (lowerOut.find("access denied") != std::string::npos ||
        lowerOut.find("failed to start trace session") != std::string::npos) {
        return -1.0;
    }

    std::istringstream iss(output);
    std::string header;
    bool headerFound = false;
    while (std::getline(iss, header)) {
        const std::string h = ToLower(header);
        if (h.find("application,") != std::string::npos && h.find("processid") != std::string::npos) {
            headerFound = true;
            break;
        }
    }
    if (!headerFound) {
        return -1.0;
    }
    auto cols = SplitCsvLine(header);
    int appIdx = -1;
    int pidIdx = -1;
    int frameTimeIdx = -1;
    int msIdx = -1;
    int fpsIdx = -1;
    for (int i = 0; i < static_cast<int>(cols.size()); ++i) {
        const std::string c = ToLower(cols[i]);
        if (c == "application") {
            appIdx = i;
        } else if (c == "processid") {
            pidIdx = i;
        } else if (c == "frametime") {
            frameTimeIdx = i;
        } else
        if (c == "msbetweenpresents" || c == "msbetweendisplaychange") {
            msIdx = i;
        } else if (c == "displayedfps" || c == "avgfps" || c == "fps") {
            fpsIdx = i;
        }
    }

    double sumMsTarget = 0.0;
    int countMsTarget = 0;
    double sumFpsTarget = 0.0;
    int countFpsTarget = 0;
    double sumMsAny = 0.0;
    int countMsAny = 0;
    double sumFpsAny = 0.0;
    int countFpsAny = 0;

    std::string line;
    const std::string wantedExe = ToLower(exeName);
    while (std::getline(iss, line)) {
        if (Trim(line).empty()) continue;
        auto fields = SplitCsvLine(line);

        bool isTarget = false;
        if (pidIdx >= 0 && pidIdx < static_cast<int>(fields.size())) {
            try {
                isTarget = (std::stoi(fields[pidIdx]) == pid);
            } catch (...) {
            }
        }
        if (!isTarget && appIdx >= 0 && appIdx < static_cast<int>(fields.size())) {
            const std::string app = ToLower(fields[appIdx]);
            isTarget = (app == wantedExe);
        }

        if (msIdx >= 0 && msIdx < static_cast<int>(fields.size())) {
            try {
                double ms = std::stod(fields[msIdx]);
                if (ms > 0.1 && ms < 1000.0) {
                    if (isTarget) {
                        sumMsTarget += ms;
                        ++countMsTarget;
                    }
                    sumMsAny += ms;
                    ++countMsAny;
                }
            } catch (...) {
            }
        }
        if (frameTimeIdx >= 0 && frameTimeIdx < static_cast<int>(fields.size())) {
            try {
                double ms = std::stod(fields[frameTimeIdx]);
                if (ms > 0.1 && ms < 1000.0) {
                    const double fps = 1000.0 / ms;
                    if (isTarget) {
                        sumFpsTarget += fps;
                        ++countFpsTarget;
                    }
                    sumFpsAny += fps;
                    ++countFpsAny;
                }
            } catch (...) {
            }
        }
        if (fpsIdx >= 0 && fpsIdx < static_cast<int>(fields.size())) {
            try {
                double fps = std::stod(fields[fpsIdx]);
                if (fps > 1.0 && fps < 1000.0) {
                    if (isTarget) {
                        sumFpsTarget += fps;
                        ++countFpsTarget;
                    }
                    sumFpsAny += fps;
                    ++countFpsAny;
                }
            } catch (...) {
            }
        }
    }

    if (countFpsTarget > 0) {
        return sumFpsTarget / static_cast<double>(countFpsTarget);
    }
    if (countMsTarget > 0) {
        const double avgMs = sumMsTarget / static_cast<double>(countMsTarget);
        if (avgMs > 0.0) {
            return 1000.0 / avgMs;
        }
    }
    if (countFpsAny > 0) {
        return sumFpsAny / static_cast<double>(countFpsAny);
    }
    if (countMsAny > 0) {
        const double avgMs = sumMsAny / static_cast<double>(countMsAny);
        if (avgMs > 0.0) {
            return 1000.0 / avgMs;
        }
    }

    return -1.0;
}

double SystemMonitor::InferSeverity(const TelemetrySnapshot& t, const LoopConfig& cfg) const {
    double s = 0.0;
    if (t.processCpuPercent > 90.0) s += 0.45;
    if (t.systemMemoryUsedPercent > 88.0) s += 0.35;
    if (t.processWorkingSetMB > 6000.0) s += 0.20;
    if (t.observedFps > 0 && t.observedFps < cfg.fpsAlertThreshold) s += 0.55;
    return std::clamp(s, 0.0, 1.0);
}

std::optional<TelemetrySnapshot> SystemMonitor::Capture(const LoopConfig& cfg) {
    TelemetrySnapshot t;
    t.gameExe = cfg.gameExe;
    t.pid = FindPidByExeName(cfg.gameExe);
    if (t.pid == 0) {
        return std::nullopt;
    }

    HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, static_cast<DWORD>(t.pid));
    if (!process) {
        return std::nullopt;
    }

    t.processWorkingSetMB = GetProcessWorkingSetMB(process);
    t.systemMemoryUsedPercent = GetSystemMemoryUsedPercent();
    t.processCpuPercent = GetProcessCpuPercent(t.pid);
    t.observedFps = GetObservedFpsViaPresentMon(t.pid, cfg.gameExe, &t.presentMonAvailable);
    t.inferredSeverity = InferSeverity(t, cfg);

    CloseHandle(process);
    return t;
}

#else

std::optional<TelemetrySnapshot> SystemMonitor::Capture(const LoopConfig&) {
    return std::nullopt;
}

#endif

