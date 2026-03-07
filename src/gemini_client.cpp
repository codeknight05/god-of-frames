#include "gemini_client.h"

#ifdef _WIN32

#include <windows.h>
#include <winhttp.h>

#include <algorithm>
#include <utility>
#include <cctype>
#include <sstream>

#pragma comment(lib, "winhttp.lib")

namespace {

std::wstring ToWide(const std::string& s) {
    if (s.empty()) return L"";
    int size = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
    if (size <= 0) return L"";
    std::wstring ws(size, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, &ws[0], size);
    if (!ws.empty() && ws.back() == L'\0') ws.pop_back();
    return ws;
}

std::string ToUtf8(const std::wstring& ws) {
    if (ws.empty()) return "";
    int size = WideCharToMultiByte(CP_UTF8, 0, ws.c_str(), -1, nullptr, 0, nullptr, nullptr);
    if (size <= 0) return "";
    std::string s(size, '\0');
    WideCharToMultiByte(CP_UTF8, 0, ws.c_str(), -1, &s[0], size, nullptr, nullptr);
    if (!s.empty() && s.back() == '\0') s.pop_back();
    return s;
}

std::string JsonEscape(const std::string& s) {
    std::string out;
    out.reserve(s.size() + 16);
    for (char c : s) {
        switch (c) {
            case '\\': out += "\\\\"; break;
            case '"': out += "\\\""; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default: out += c; break;
        }
    }
    return out;
}

std::string HttpPostJson(const std::wstring& host, const std::wstring& path, const std::string& body) {
    std::string response;

    HINTERNET hSession = WinHttpOpen(L"GodOfFrames/1.0", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
                                     WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!hSession) return "";

    HINTERNET hConnect = WinHttpConnect(hSession, host.c_str(), INTERNET_DEFAULT_HTTPS_PORT, 0);
    if (!hConnect) {
        WinHttpCloseHandle(hSession);
        return "";
    }

    HINTERNET hRequest = WinHttpOpenRequest(hConnect, L"POST", path.c_str(), nullptr,
                                            WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
                                            WINHTTP_FLAG_SECURE);
    if (!hRequest) {
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return "";
    }

    const wchar_t* headers = L"Content-Type: application/json\r\n";
    BOOL ok = WinHttpSendRequest(hRequest, headers, -1L,
                                 (LPVOID)body.data(), static_cast<DWORD>(body.size()),
                                 static_cast<DWORD>(body.size()), 0);

    if (ok) ok = WinHttpReceiveResponse(hRequest, nullptr);

    if (ok) {
        DWORD size = 0;
        do {
            size = 0;
            if (!WinHttpQueryDataAvailable(hRequest, &size)) break;
            if (size == 0) break;
            std::string chunk(size, '\0');
            DWORD downloaded = 0;
            if (!WinHttpReadData(hRequest, &chunk[0], size, &downloaded)) break;
            chunk.resize(downloaded);
            response += chunk;
        } while (size > 0);
    }

    WinHttpCloseHandle(hRequest);
    WinHttpCloseHandle(hConnect);
    WinHttpCloseHandle(hSession);
    return response;
}

} // namespace

GeminiClient::GeminiClient(std::string apiKey, std::string model)
    : apiKey_(std::move(apiKey)), model_(std::move(model)) {}

bool GeminiClient::IsConfigured() const {
    return !apiKey_.empty();
}

std::string GeminiClient::BuildPrompt(const TelemetrySnapshot& t) const {
    std::ostringstream oss;
    oss << "You are an FPS optimization agent for PC games.\n"
        << "Given telemetry, emit ONLY lines in this exact grammar:\n"
        << "AUTO_FIX:<TOKEN>|<short reason>\n"
        << "NOTIFY:<TOKEN>|<short reason>\n"
        << "No extra text. Max 4 lines.\n"
        << "Allowed AUTO_FIX tokens: SET_HIGH_PERF_POWER_PLAN, SET_GAME_PRIORITY_HIGH, TRIM_BACKGROUND_APPS\n"
        << "Use NOTIFY for actions needing user intervention (drivers, BIOS, game settings, reinstall, hardware).\n"
        << "Telemetry:\n"
        << "gameExe=" << t.gameExe << "\n"
        << "pid=" << t.pid << "\n"
        << "processCpuPercent=" << t.processCpuPercent << "\n"
        << "systemMemoryUsedPercent=" << t.systemMemoryUsedPercent << "\n"
        << "processWorkingSetMB=" << t.processWorkingSetMB << "\n"
        << "observedFps=" << t.observedFps << "\n"
        << "severity=" << t.inferredSeverity << "\n";
    return oss.str();
}

std::string GeminiClient::Generate(const std::string& prompt) const {
    if (!IsConfigured()) return "";

    std::string body =
        std::string("{\"contents\":[{\"parts\":[{\"text\":\"") + JsonEscape(prompt) +
        "\"}]}],\"generationConfig\":{\"temperature\":0.2}}";

    std::wstring host = L"generativelanguage.googleapis.com";
    std::wstring path = L"/v1beta/models/" + ToWide(model_) + L":generateContent?key=" + ToWide(apiKey_);
    return HttpPostJson(host, path, body);
}

std::string GeminiClient::ExtractTextFromResponseJson(const std::string& json) const {
    // Lightweight extraction for: candidates[0].content.parts[0].text
    auto keyPos = json.find("\"text\":");
    if (keyPos == std::string::npos) return "";

    auto firstQuote = json.find('"', keyPos + 7);
    if (firstQuote == std::string::npos) return "";

    std::string out;
    bool escape = false;
    for (size_t i = firstQuote + 1; i < json.size(); ++i) {
        char c = json[i];
        if (escape) {
            switch (c) {
                case 'n': out.push_back('\n'); break;
                case 'r': out.push_back('\r'); break;
                case 't': out.push_back('\t'); break;
                case '"': out.push_back('"'); break;
                case '\\': out.push_back('\\'); break;
                default: out.push_back(c); break;
            }
            escape = false;
            continue;
        }
        if (c == '\\') {
            escape = true;
            continue;
        }
        if (c == '"') {
            break;
        }
        out.push_back(c);
    }
    return out;
}

std::vector<AiAction> GeminiClient::ParseActions(const std::string& text) const {
    std::vector<AiAction> actions;
    std::istringstream iss(text);
    std::string line;

    while (std::getline(iss, line)) {
        if (line.empty()) continue;
        AiAction a;

        if (line.rfind("AUTO_FIX:", 0) == 0) {
            a.kind = ActionKind::AutoFix;
            line = line.substr(9);
        } else if (line.rfind("NOTIFY:", 0) == 0) {
            a.kind = ActionKind::Notify;
            line = line.substr(7);
        } else {
            continue;
        }

        auto sep = line.find('|');
        if (sep == std::string::npos) {
            a.token = line;
        } else {
            a.token = line.substr(0, sep);
            a.details = line.substr(sep + 1);
        }

        a.token.erase(std::remove_if(a.token.begin(), a.token.end(), [](unsigned char c) {
            return std::isspace(c) != 0;
        }), a.token.end());

        if (!a.token.empty()) actions.push_back(a);
    }

    return actions;
}

std::vector<AiAction> GeminiClient::Analyze(const TelemetrySnapshot& t) const {
    if (!IsConfigured()) return {};

    const std::string prompt = BuildPrompt(t);
    const std::string raw = Generate(prompt);
    if (raw.empty()) return {};

    const std::string text = ExtractTextFromResponseJson(raw);
    if (text.empty()) return {};

    return ParseActions(text);
}

#else

GeminiClient::GeminiClient(std::string apiKey, std::string model)
    : apiKey_(std::move(apiKey)), model_(std::move(model)) {}

bool GeminiClient::IsConfigured() const { return false; }
std::vector<AiAction> GeminiClient::Analyze(const TelemetrySnapshot&) const { return {}; }

#endif



