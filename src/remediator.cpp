#include "remediator.h"

#ifdef _WIN32

#include <windows.h>
#include <tlhelp32.h>

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <sstream>
#include <utility>

namespace {

bool EqualsIgnoreCase(const std::string& a, const std::string& b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i) {
        if (std::tolower(static_cast<unsigned char>(a[i])) != std::tolower(static_cast<unsigned char>(b[i]))) {
            return false;
        }
    }
    return true;
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

ActionResult MakeNotify(const std::string& token, const std::string& msg) {
    ActionResult r;
    r.token = token;
    r.kind = ActionKind::Notify;
    r.attempted = false;
    r.success = false;
    r.message = msg;
    return r;
}

} // namespace

Remediator::Remediator(std::vector<std::string> trimTargets, std::vector<std::string> protectedApps)
    : trimTargets_(std::move(trimTargets)), protectedApps_(std::move(protectedApps)) {}

void Remediator::SetTrimPolicy(std::vector<std::string> trimTargets, std::vector<std::string> protectedApps) {
    trimTargets_ = std::move(trimTargets);
    protectedApps_ = std::move(protectedApps);
}

bool Remediator::IsAllowedAutoFixToken(const std::string& token) const {
    return std::find(kAllowedAutoFixTokens.begin(), kAllowedAutoFixTokens.end(), token) != kAllowedAutoFixTokens.end();
}

bool Remediator::SetHighPerformancePlan() const {
    int code = std::system("powercfg /setactive SCHEME_MIN >nul 2>nul");
    return code == 0;
}

bool Remediator::SetGamePriorityHigh(int pid) const {
    HANDLE h = OpenProcess(PROCESS_SET_INFORMATION, FALSE, static_cast<DWORD>(pid));
    if (!h) return false;
    BOOL ok = SetPriorityClass(h, HIGH_PRIORITY_CLASS);
    CloseHandle(h);
    return ok == TRUE;
}

int Remediator::TrimBackgroundApps() const {
    std::vector<std::string> killList = trimTargets_;
    if (killList.empty()) {
        killList = {
            "Overwolf.exe",
            "RadeonSoftware.exe",
            "XboxPcApp.exe"
        };
    }

    int killed = 0;
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE) return 0;

    PROCESSENTRY32 pe{};
    pe.dwSize = sizeof(pe);
    if (!Process32First(snapshot, &pe)) {
        CloseHandle(snapshot);
        return 0;
    }

    do {
        std::wstring ws(pe.szExeFile);
        std::string exe = WideToUtf8(ws);
        if (IsProtectedApp(exe)) {
            continue;
        }
        for (const auto& target : killList) {
            if (!EqualsIgnoreCase(exe, target)) continue;
            HANDLE h = OpenProcess(PROCESS_TERMINATE, FALSE, pe.th32ProcessID);
            if (!h) continue;
            if (TerminateProcess(h, 0)) {
                ++killed;
            }
            CloseHandle(h);
        }
    } while (Process32Next(snapshot, &pe));

    CloseHandle(snapshot);
    return killed;
}

std::vector<ActionResult> Remediator::Apply(const TelemetrySnapshot& t, const std::vector<AiAction>& actions) const {
    std::vector<ActionResult> out;

    for (const auto& action : actions) {
        if (action.kind == ActionKind::Notify) {
            out.push_back(MakeNotify(action.token, action.details));
            continue;
        }
        if (action.kind != ActionKind::AutoFix) {
            continue;
        }

        if (!IsAllowedAutoFixToken(action.token)) {
            out.push_back(MakeNotify("UNSAFE_OR_UNKNOWN_ACTION", action.token + " blocked by policy."));
            continue;
        }

        ActionResult r;
        r.token = action.token;
        r.kind = ActionKind::AutoFix;
        r.attempted = true;

        if (action.token == "SET_HIGH_PERF_POWER_PLAN") {
            r.success = SetHighPerformancePlan();
            r.message = r.success ? "Applied High Performance power plan." : "Failed to switch power plan (try admin).";
        } else if (action.token == "SET_GAME_PRIORITY_HIGH") {
            r.success = SetGamePriorityHigh(t.pid);
            r.message = r.success ? "Set game process to HIGH priority." : "Could not set process priority.";
        } else if (action.token == "TRIM_BACKGROUND_APPS") {
            int n = TrimBackgroundApps();
            r.success = n > 0;
            std::ostringstream oss;
            oss << "Closed " << n << " background app process(es).";
            r.message = oss.str();
        }

        out.push_back(r);
    }

    return out;
}

bool Remediator::IsProtectedApp(const std::string& exeName) const {
    for (const auto& p : protectedApps_) {
        if (EqualsIgnoreCase(p, exeName)) return true;
    }
    return false;
}

#else

std::vector<ActionResult> Remediator::Apply(const TelemetrySnapshot&, const std::vector<AiAction>& actions) const {
    std::vector<ActionResult> out;
    for (const auto& a : actions) {
        ActionResult r;
        r.token = a.token;
        r.kind = ActionKind::Notify;
        r.message = "Windows-only remediation in current build.";
        out.push_back(r);
    }
    return out;
}

#endif
