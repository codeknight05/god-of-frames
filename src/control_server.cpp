#include "control_server.h"

#ifdef _WIN32

#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <winhttp.h>
#include <process.h>

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>

#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "winhttp.lib")

namespace {

std::string Trim(const std::string& s) {
    const auto b = s.find_first_not_of(" \t\r\n");
    if (b == std::string::npos) return "";
    const auto e = s.find_last_not_of(" \t\r\n");
    return s.substr(b, e - b + 1);
}

std::string JoinList(const std::vector<std::string>& values) {
    std::string out;
    for (size_t i = 0; i < values.size(); ++i) {
        if (i) out += ';';
        out += values[i];
    }
    return out;
}

std::wstring ToWide(const std::string& s) {
    if (s.empty()) return L"";
    const int size = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
    if (size <= 0) return L"";
    std::wstring out(size, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, &out[0], size);
    if (!out.empty() && out.back() == L'\0') out.pop_back();
    return out;
}

bool HttpPostJson(const std::string& url, const std::string& body) {
    const std::wstring wurl = ToWide(url);
    if (wurl.empty()) return false;

    URL_COMPONENTSW uc{};
    uc.dwStructSize = sizeof(uc);
    uc.dwSchemeLength = static_cast<DWORD>(-1);
    uc.dwHostNameLength = static_cast<DWORD>(-1);
    uc.dwUrlPathLength = static_cast<DWORD>(-1);
    uc.dwExtraInfoLength = static_cast<DWORD>(-1);
    if (!WinHttpCrackUrl(wurl.c_str(), 0, 0, &uc)) {
        return false;
    }

    const std::wstring host(uc.lpszHostName, uc.dwHostNameLength);
    std::wstring path = uc.dwUrlPathLength > 0
        ? std::wstring(uc.lpszUrlPath, uc.dwUrlPathLength)
        : L"/";
    if (uc.dwExtraInfoLength > 0) {
        path.append(uc.lpszExtraInfo, uc.dwExtraInfoLength);
    }

    HINTERNET hSession = WinHttpOpen(L"GodOfFrames/1.0",
                                     WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
                                     WINHTTP_NO_PROXY_NAME,
                                     WINHTTP_NO_PROXY_BYPASS,
                                     0);
    if (!hSession) return false;

    HINTERNET hConnect = WinHttpConnect(hSession, host.c_str(), uc.nPort, 0);
    if (!hConnect) {
        WinHttpCloseHandle(hSession);
        return false;
    }

    const DWORD flags = (uc.nScheme == INTERNET_SCHEME_HTTPS) ? WINHTTP_FLAG_SECURE : 0;
    HINTERNET hRequest = WinHttpOpenRequest(hConnect,
                                            L"POST",
                                            path.c_str(),
                                            nullptr,
                                            WINHTTP_NO_REFERER,
                                            WINHTTP_DEFAULT_ACCEPT_TYPES,
                                            flags);
    if (!hRequest) {
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return false;
    }

    const wchar_t* headers = L"Content-Type: application/json\r\n";
    BOOL ok = WinHttpSendRequest(hRequest,
                                 headers,
                                 static_cast<DWORD>(-1),
                                 reinterpret_cast<LPVOID>(const_cast<char*>(body.data())),
                                 static_cast<DWORD>(body.size()),
                                 static_cast<DWORD>(body.size()),
                                 0);
    if (ok) {
        ok = WinHttpReceiveResponse(hRequest, nullptr);
    }

    DWORD statusCode = 0;
    DWORD codeSize = sizeof(statusCode);
    if (ok) {
        WinHttpQueryHeaders(hRequest,
                            WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                            WINHTTP_HEADER_NAME_BY_INDEX,
                            &statusCode,
                            &codeSize,
                            WINHTTP_NO_HEADER_INDEX);
    }

    WinHttpCloseHandle(hRequest);
    WinHttpCloseHandle(hConnect);
    WinHttpCloseHandle(hSession);

    return ok == TRUE && statusCode >= 200 && statusCode < 300;
}

} // namespace

ControlServer::ControlServer() {}
ControlServer::~ControlServer() { Stop(); }

bool ControlServer::Start(int port, std::shared_ptr<WebUiState> state, UpdateCallback onUpdate) {
    if (running_) return true;
    port_ = port;
    state_ = std::move(state);
    onUpdate_ = std::move(onUpdate);
    running_ = true;

    uintptr_t h = _beginthreadex(nullptr, 0,
        [](void* p) -> unsigned int {
            reinterpret_cast<ControlServer*>(p)->ThreadMain();
            return 0;
        }, this, 0, &threadId_);

    if (h == 0) {
        running_ = false;
        return false;
    }

    threadHandle_ = reinterpret_cast<void*>(h);
    return true;
}

void ControlServer::Stop() {
    if (!running_) return;
    running_ = false;

    HANDLE hThread = reinterpret_cast<HANDLE>(threadHandle_);
    if (hThread) {
        WaitForSingleObject(hThread, 2000);
        CloseHandle(hThread);
        threadHandle_ = nullptr;
    }
}

std::string ControlServer::Url() const {
    return "http://127.0.0.1:" + std::to_string(port_);
}

std::string ControlServer::ParsePath(const std::string& requestLine) {
    auto p1 = requestLine.find(' ');
    if (p1 == std::string::npos) return "/";
    auto p2 = requestLine.find(' ', p1 + 1);
    if (p2 == std::string::npos) return "/";
    std::string path = requestLine.substr(p1 + 1, p2 - p1 - 1);
    const auto q = path.find('?');
    if (q != std::string::npos) {
        path = path.substr(0, q);
    }
    return path;
}

std::string ControlServer::UrlDecode(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '+') {
            out.push_back(' ');
        } else if (s[i] == '%' && i + 2 < s.size()) {
            const char h1 = s[i + 1];
            const char h2 = s[i + 2];
            auto hex = [](char c) -> int {
                if (c >= '0' && c <= '9') return c - '0';
                if (c >= 'a' && c <= 'f') return 10 + c - 'a';
                if (c >= 'A' && c <= 'F') return 10 + c - 'A';
                return -1;
            };
            const int v1 = hex(h1), v2 = hex(h2);
            if (v1 >= 0 && v2 >= 0) {
                out.push_back(static_cast<char>((v1 << 4) | v2));
                i += 2;
            } else {
                out.push_back(s[i]);
            }
        } else {
            out.push_back(s[i]);
        }
    }
    return out;
}

std::vector<std::string> ControlServer::ParseList(const std::string& value) {
    std::vector<std::string> out;
    std::string cur;
    for (char c : value) {
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

std::string ControlServer::EscapeJson(const std::string& s) {
    std::string out;
    out.reserve(s.size() + 8);
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

std::string ControlServer::SanitizeFeedbackField(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (char c : s) {
        if (c == '\r' || c == '\n' || c == '\t') {
            out.push_back(' ');
        } else if (c == '|') {
            out.push_back('/');
        } else {
            out.push_back(c);
        }
        if (out.size() >= 1500) break;
    }
    return Trim(out);
}

std::string ControlServer::CurrentTimestamp() {
    std::time_t now = std::time(nullptr);
    std::tm localTm{};
#ifdef _MSC_VER
    localtime_s(&localTm, &now);
#else
    localTm = *std::localtime(&now);
#endif
    std::ostringstream oss;
    oss << std::put_time(&localTm, "%Y-%m-%d %H:%M:%S");
    return oss.str();
}

std::vector<std::string> ControlServer::Split(const std::string& s, char delimiter) {
    std::vector<std::string> out;
    std::string cur;
    for (char c : s) {
        if (c == delimiter) {
            out.push_back(cur);
            cur.clear();
        } else {
            cur.push_back(c);
        }
    }
    out.push_back(cur);
    return out;
}

bool ControlServer::AppendFeedback(const std::string& category,
                                   const std::string& message,
                                   const std::string& contact,
                                   const std::string& gameExe,
                                   bool* forwarded) const {
    if (forwarded) *forwarded = false;

    const std::string ts = CurrentTimestamp();
    const std::string cleanCategory = SanitizeFeedbackField(category.empty() ? "general" : category);
    const std::string cleanMessage = SanitizeFeedbackField(message);
    const std::string cleanContact = SanitizeFeedbackField(contact);
    const std::string cleanGame = SanitizeFeedbackField(gameExe);

    if (cleanMessage.empty()) return false;

    std::filesystem::path p(feedbackPath_);
    if (p.has_parent_path()) {
        std::filesystem::create_directories(p.parent_path());
    }

    {
        std::ofstream out(feedbackPath_, std::ios::app);
        if (!out.is_open()) return false;
        out << ts << '|'
            << cleanCategory << '|'
            << cleanGame << '|'
            << cleanContact << '|'
            << cleanMessage << '\n';
    }

    std::string endpoint;
    if (state_) {
        std::lock_guard<std::mutex> lock(state_->mutex);
        endpoint = Trim(state_->settings.feedbackEndpoint);
    }

    if (!endpoint.empty()) {
        std::ostringstream payload;
        payload << "{"
                << "\"timestamp\":\"" << EscapeJson(ts) << "\"," 
                << "\"category\":\"" << EscapeJson(cleanCategory) << "\"," 
                << "\"game\":\"" << EscapeJson(cleanGame) << "\"," 
                << "\"contact\":\"" << EscapeJson(cleanContact) << "\"," 
                << "\"message\":\"" << EscapeJson(cleanMessage) << "\""
                << "}";
        const bool remoteOk = HttpPostJson(endpoint, payload.str());
        if (forwarded) *forwarded = remoteOk;
    }

    return true;
}

bool ControlServer::SendHttp(uintptr_t clientSocket,
                             int status,
                             const std::string& contentType,
                             const std::string& body) const {
    SOCKET s = static_cast<SOCKET>(clientSocket);

    std::string statusText = "OK";
    if (status == 400) statusText = "Bad Request";
    if (status == 404) statusText = "Not Found";
    if (status == 500) statusText = "Internal Server Error";

    std::ostringstream oss;
    oss << "HTTP/1.1 " << status << " " << statusText << "\r\n"
        << "Content-Type: " << contentType << "\r\n"
        << "Content-Length: " << body.size() << "\r\n"
        << "Connection: close\r\n\r\n"
        << body;

    const std::string resp = oss.str();
    const int sent = send(s, resp.data(), static_cast<int>(resp.size()), 0);
    return sent == static_cast<int>(resp.size());
}

std::string ControlServer::BuildStateJson() const {
    if (!state_) {
        return "{\"hasSnapshot\":false}";
    }

    std::lock_guard<std::mutex> lock(state_->mutex);
    const auto& t = state_->latestSnapshot;
    const auto& s = state_->settings;

    std::ostringstream oss;
    oss << "{";
    oss << "\"hasSnapshot\":" << (state_->hasSnapshot ? "true" : "false") << ',';
    oss << "\"activeGame\":\"" << EscapeJson(state_->activeGameExe) << "\",";
    oss << "\"fps\":" << t.observedFps << ',';
    oss << "\"cpu\":" << t.processCpuPercent << ',';
    oss << "\"sysMem\":" << t.systemMemoryUsedPercent << ',';
    oss << "\"gameMem\":" << t.processWorkingSetMB << ',';
    oss << "\"severity\":" << t.inferredSeverity << ',';
    oss << "\"protectedApps\":\"" << EscapeJson(JoinList(s.protectedApps)) << "\",";
    oss << "\"trimTargets\":\"" << EscapeJson(JoinList(s.trimTargets)) << "\",";
    oss << "\"gameProcesses\":\"" << EscapeJson(JoinList(s.gameProcesses)) << "\",";
    oss << "\"feedbackEndpoint\":\"" << EscapeJson(s.feedbackEndpoint) << "\",";
    oss << "\"updateManifestUrl\":\"" << EscapeJson(s.updateManifestUrl) << "\"";
    oss << "}";
    return oss.str();
}

std::string ControlServer::BuildFeedbackJson() const {
    std::ifstream in(feedbackPath_);
    if (!in.is_open()) {
        return "{\"items\":[]}";
    }

    struct FeedbackRow {
        std::string timestamp;
        std::string category;
        std::string game;
        std::string contact;
        std::string message;
    };

    std::vector<FeedbackRow> rows;
    std::string line;
    while (std::getline(in, line)) {
        if (Trim(line).empty()) continue;
        auto fields = Split(line, '|');
        if (fields.size() < 5) continue;
        rows.push_back({fields[0], fields[1], fields[2], fields[3], fields[4]});
    }

    const size_t keep = 200;
    const size_t start = rows.size() > keep ? rows.size() - keep : 0;

    std::ostringstream oss;
    oss << "{\"items\":[";
    bool first = true;
    for (size_t i = rows.size(); i > start; --i) {
        const auto& row = rows[i - 1];
        if (!first) oss << ',';
        first = false;
        oss << "{"
            << "\"timestamp\":\"" << EscapeJson(row.timestamp) << "\","
            << "\"category\":\"" << EscapeJson(row.category) << "\","
            << "\"game\":\"" << EscapeJson(row.game) << "\","
            << "\"contact\":\"" << EscapeJson(row.contact) << "\","
            << "\"message\":\"" << EscapeJson(row.message) << "\""
            << "}";
    }
    oss << "]}";
    return oss.str();
}

std::string ControlServer::BuildHtml() const {
    return R"HTML(
<!doctype html>
<html>
<head>
  <meta charset="utf-8" />
  <meta name="viewport" content="width=device-width, initial-scale=1" />
  <title>God of Frames Control</title>
  <style>
    :root{--bg:#0b1020;--card:#111a33;--line:#22345f;--text:#e9eefc;--muted:#95a6ce;--ok:#2ec27e;--warn:#f6c64d;--accent:#3aa3ff;--danger:#ff6b6b;}
    *{box-sizing:border-box}
    body{margin:0;font-family:Segoe UI,system-ui,sans-serif;background:radial-gradient(1200px 700px at 15% -20%,#1e2a50 0%,var(--bg) 60%);color:var(--text)}
    .wrap{max-width:1000px;margin:22px auto;padding:0 16px}
    .h{font-size:26px;font-weight:700}
    .sub{color:var(--muted);margin-top:4px}
    .grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(170px,1fr));gap:12px;margin:16px 0}
    .card{background:linear-gradient(180deg,#152348 0%,var(--card) 100%);border:1px solid var(--line);border-radius:14px;padding:12px}
    .k{font-size:12px;color:var(--muted);text-transform:uppercase}.v{font-size:24px;font-weight:700}
    .tabs{display:flex;gap:8px;margin-top:12px}
    .tab{padding:8px 12px;border:1px solid var(--line);border-radius:999px;background:#0f1830;color:#dbe8ff;cursor:pointer}
    .tab.active{background:var(--accent);border-color:var(--accent);color:#fff}
    .panel{background:var(--card);border:1px solid var(--line);border-radius:14px;padding:14px;margin-top:12px}
    label{display:block;font-size:12px;color:var(--muted);margin:10px 0 6px}
    input,select,textarea{width:100%;background:#0f1830;color:#dbe8ff;border:1px solid #274175;border-radius:10px;padding:10px}
    textarea{min-height:92px;resize:vertical}
    .btn{margin-top:12px;background:var(--accent);color:#fff;border:none;padding:10px 14px;border-radius:10px;font-weight:700;cursor:pointer}
    .btn-danger{background:var(--danger)}
    .hidden{display:none}
    .fb-list{margin-top:12px;display:grid;gap:8px}
    .fb-item{background:#0f1830;border:1px solid #274175;border-radius:10px;padding:10px}
    .fb-meta{font-size:12px;color:var(--muted);margin-bottom:5px}
    .ok{color:var(--ok)} .warn{color:var(--warn)}
  </style>
</head>
<body>
  <div class="wrap">
    <div class="h">God of Frames Control</div>
    <div class="sub">Live telemetry + settings editor + feedback relay</div>

    <div class="grid">
      <div class="card"><div class="k">Active Game</div><div id="activeGame" class="v">--</div></div>
      <div class="card"><div class="k">FPS</div><div id="fps" class="v">N/A</div></div>
      <div class="card"><div class="k">CPU %</div><div id="cpu" class="v">--</div></div>
      <div class="card"><div class="k">System RAM %</div><div id="sysMem" class="v">--</div></div>
      <div class="card"><div class="k">Severity</div><div id="sev" class="v">--</div></div>
    </div>

    <div class="tabs">
      <button id="tabBtnSettings" class="tab active" onclick="switchTab('settings')">Settings</button>
      <button id="tabBtnFeedback" class="tab" onclick="switchTab('feedback')">Feedback</button>
    </div>

    <div id="tabSettings" class="panel">
      <div class="k">Background App Policy</div>
      <label>Protected Apps (never auto-close) - semicolon separated exe names</label>
      <input id="protectedApps" placeholder="Discord.exe;Steam.exe;obs64.exe" />

      <label>Trim Targets (apps eligible for auto-close)</label>
      <input id="trimTargets" placeholder="Overwolf.exe;RadeonSoftware.exe;XboxPcApp.exe" />

      <label>Watch Games (watch mode list)</label>
      <input id="gameProcesses" placeholder="helldivers2.exe;forhonor.exe" />

      <label>Feedback Relay Endpoint (optional, cross-user feedback sync)</label>
      <input id="feedbackEndpoint" placeholder="https://your-server.example.com/api/ingest" />

      <label>Update Manifest URL (optional, future auto-update checks)</label>
      <input id="updateManifestUrl" placeholder="https://your-server.example.com/api/version" />

      <button class="btn" onclick="saveSettings()">Save Settings</button>
      <div id="saveStatus" class="sub"></div>
    </div>

    <div id="tabFeedback" class="panel hidden">
      <div class="k">Send Feedback</div>
      <label>Category</label>
      <select id="fbCategory">
        <option value="bug">Bug</option>
        <option value="feature">Feature Request</option>
        <option value="performance">Performance</option>
        <option value="ui">UI/UX</option>
      </select>

      <label>Contact (optional)</label>
      <input id="fbContact" placeholder="Email or Discord handle" />

      <label>Message</label>
      <textarea id="fbMessage" placeholder="Describe issue, game, hardware, and what happened."></textarea>

      <button class="btn btn-danger" onclick="submitFeedback()">Submit Feedback</button>
      <div id="fbStatus" class="sub"></div>

      <div class="k" style="margin-top:12px">Recent Feedback (local + synced attempts)</div>
      <div id="fbList" class="fb-list"></div>
    </div>
  </div>

  <script>
    function switchTab(tab){
      const isSettings = tab === 'settings';
      document.getElementById('tabSettings').classList.toggle('hidden', !isSettings);
      document.getElementById('tabFeedback').classList.toggle('hidden', isSettings);
      document.getElementById('tabBtnSettings').classList.toggle('active', isSettings);
      document.getElementById('tabBtnFeedback').classList.toggle('active', !isSettings);
      if(!isSettings){ refreshFeedback(); }
    }

    function esc(v){
      return String(v ?? '')
        .replaceAll('&','&amp;')
        .replaceAll('<','&lt;')
        .replaceAll('>','&gt;')
        .replaceAll('"','&quot;')
        .replaceAll("'",'&#39;');
    }

    async function refreshState(){
      try{
        const r = await fetch('/api/state');
        const s = await r.json();
        document.getElementById('activeGame').textContent = s.activeGame || '--';
        document.getElementById('fps').textContent = (s.fps >= 0 ? s.fps.toFixed(1) : 'N/A');
        document.getElementById('cpu').textContent = (s.cpu ?? 0).toFixed(1);
        document.getElementById('sysMem').textContent = (s.sysMem ?? 0).toFixed(1);
        document.getElementById('sev').textContent = (s.severity ?? 0).toFixed(2);

        if(!document.getElementById('protectedApps').dataset.touched){
          document.getElementById('protectedApps').value = s.protectedApps || '';
        }
        if(!document.getElementById('trimTargets').dataset.touched){
          document.getElementById('trimTargets').value = s.trimTargets || '';
        }
        if(!document.getElementById('gameProcesses').dataset.touched){
          document.getElementById('gameProcesses').value = s.gameProcesses || '';
        }
        if(!document.getElementById('feedbackEndpoint').dataset.touched){
          document.getElementById('feedbackEndpoint').value = s.feedbackEndpoint || '';
        }
        if(!document.getElementById('updateManifestUrl').dataset.touched){
          document.getElementById('updateManifestUrl').value = s.updateManifestUrl || '';
        }
      }catch(e){}
    }

    for(const id of ['protectedApps','trimTargets','gameProcesses','feedbackEndpoint','updateManifestUrl']){
      document.getElementById(id).addEventListener('input', ()=>{ document.getElementById(id).dataset.touched='1'; });
    }

    async function saveSettings(){
      const body = new URLSearchParams();
      body.set('protected_apps', document.getElementById('protectedApps').value);
      body.set('trim_targets', document.getElementById('trimTargets').value);
      body.set('game_processes', document.getElementById('gameProcesses').value);
      body.set('feedback_endpoint', document.getElementById('feedbackEndpoint').value);
      body.set('update_manifest_url', document.getElementById('updateManifestUrl').value);

      const status = document.getElementById('saveStatus');
      status.textContent = 'Saving...';

      try{
        const r = await fetch('/api/settings', {method:'POST', headers:{'Content-Type':'application/x-www-form-urlencoded'}, body:body.toString()});
        if(r.ok){
          status.textContent = 'Saved. Applied to running session.';
          status.className = 'ok';
          for(const id of ['protectedApps','trimTargets','gameProcesses','feedbackEndpoint','updateManifestUrl']){
            delete document.getElementById(id).dataset.touched;
          }
          refreshState();
        }else{
          status.textContent = 'Save failed.';
          status.className = 'warn';
        }
      }catch(e){
        status.textContent = 'Save failed.';
        status.className = 'warn';
      }
    }

    async function submitFeedback(){
      const message = document.getElementById('fbMessage').value.trim();
      const status = document.getElementById('fbStatus');
      if(!message){
        status.textContent = 'Message is required.';
        status.className = 'warn';
        return;
      }

      const body = new URLSearchParams();
      body.set('category', document.getElementById('fbCategory').value);
      body.set('contact', document.getElementById('fbContact').value);
      body.set('message', message);

      status.textContent = 'Submitting...';
      status.className = 'sub';

      try{
        const r = await fetch('/api/feedback', {method:'POST', headers:{'Content-Type':'application/x-www-form-urlencoded'}, body:body.toString()});
        if(!r.ok){
          status.textContent = 'Submit failed.';
          status.className = 'warn';
          return;
        }
        const j = await r.json();
        status.textContent = j.forwarded
          ? 'Feedback saved and synced to relay.'
          : 'Feedback saved locally (relay missing/unreachable).';
        status.className = 'ok';
        document.getElementById('fbMessage').value = '';
        refreshFeedback();
      }catch(e){
        status.textContent = 'Submit failed.';
        status.className = 'warn';
      }
    }

    function renderFeedback(items){
      const list = document.getElementById('fbList');
      if(!items || !items.length){
        list.innerHTML = '<div class="fb-item"><div class="fb-meta">No feedback yet.</div></div>';
        return;
      }

      list.innerHTML = items.map(i => `
        <div class="fb-item">
          <div class="fb-meta">${esc(i.timestamp)} | ${esc(i.category)} | ${esc(i.game)} ${i.contact ? '| ' + esc(i.contact) : ''}</div>
          <div>${esc(i.message)}</div>
        </div>`).join('');
    }

    async function refreshFeedback(){
      try{
        const r = await fetch('/api/feedback');
        const j = await r.json();
        renderFeedback(j.items || []);
      }catch(e){}
    }

    setInterval(refreshState, 1000);
    setInterval(refreshFeedback, 10000);
    refreshState();
    refreshFeedback();
  </script>
</body>
</html>
)HTML";
}

bool ControlServer::HandleClient(uintptr_t clientSocket) {
    SOCKET client = static_cast<SOCKET>(clientSocket);

    std::string request;
    char buffer[4096];
    int n = 0;
    while ((n = recv(client, buffer, sizeof(buffer), 0)) > 0) {
        request.append(buffer, buffer + n);
        if (request.find("\r\n\r\n") != std::string::npos) break;
    }

    if (request.empty()) {
        return false;
    }

    auto headerEnd = request.find("\r\n\r\n");
    std::string headers = (headerEnd == std::string::npos) ? request : request.substr(0, headerEnd);
    std::string body = (headerEnd == std::string::npos) ? "" : request.substr(headerEnd + 4);

    std::istringstream hs(headers);
    std::string requestLine;
    std::getline(hs, requestLine);
    requestLine = Trim(requestLine);

    std::string method = "GET";
    {
        std::istringstream rl(requestLine);
        rl >> method;
    }

    int contentLen = 0;
    std::string line;
    while (std::getline(hs, line)) {
        auto lower = line;
        std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) {
            return static_cast<char>(std::tolower(c));
        });
        auto pos = lower.find("content-length:");
        if (pos != std::string::npos) {
            try { contentLen = std::stoi(Trim(line.substr(pos + 15))); } catch (...) {}
        }
    }

    while (static_cast<int>(body.size()) < contentLen) {
        n = recv(client, buffer, sizeof(buffer), 0);
        if (n <= 0) break;
        body.append(buffer, buffer + n);
    }

    const std::string path = ParsePath(requestLine);

    if (method == "GET" && path == "/") {
        return SendHttp(clientSocket, 200, "text/html; charset=utf-8", BuildHtml());
    }
    if (method == "GET" && path == "/api/state") {
        return SendHttp(clientSocket, 200, "application/json", BuildStateJson());
    }
    if (method == "GET" && path == "/api/feedback") {
        return SendHttp(clientSocket, 200, "application/json", BuildFeedbackJson());
    }
    if (method == "POST" && path == "/api/settings") {
        SettingsPatch patch;

        std::stringstream ss(body);
        std::string kv;
        while (std::getline(ss, kv, '&')) {
            auto eq = kv.find('=');
            if (eq == std::string::npos) continue;
            const std::string key = UrlDecode(kv.substr(0, eq));
            const std::string val = UrlDecode(kv.substr(eq + 1));

            if (key == "protected_apps") {
                patch.protectedApps = ParseList(val);
            } else if (key == "trim_targets") {
                patch.trimTargets = ParseList(val);
            } else if (key == "game_processes") {
                patch.gameProcesses = ParseList(val);
            } else if (key == "feedback_endpoint") {
                patch.feedbackEndpoint = Trim(val);
            } else if (key == "update_manifest_url") {
                patch.updateManifestUrl = Trim(val);
            }
        }

        if (onUpdate_) {
            onUpdate_(patch);
        }

        return SendHttp(clientSocket, 200, "application/json", "{\"ok\":true}");
    }
    if (method == "POST" && path == "/api/feedback") {
        std::string category;
        std::string contact;
        std::string message;

        std::stringstream ss(body);
        std::string kv;
        while (std::getline(ss, kv, '&')) {
            auto eq = kv.find('=');
            if (eq == std::string::npos) continue;
            const std::string key = UrlDecode(kv.substr(0, eq));
            const std::string val = UrlDecode(kv.substr(eq + 1));
            if (key == "category") category = val;
            else if (key == "contact") contact = val;
            else if (key == "message") message = val;
        }

        std::string game;
        if (state_) {
            std::lock_guard<std::mutex> lock(state_->mutex);
            game = state_->activeGameExe;
        }

        bool forwarded = false;
        const bool ok = AppendFeedback(category, message, contact, game, &forwarded);
        if (!ok) {
            return SendHttp(clientSocket, 400, "application/json", "{\"ok\":false,\"error\":\"invalid_feedback\"}");
        }

        return SendHttp(clientSocket, 200, "application/json",
                        std::string("{\"ok\":true,\"forwarded\":") + (forwarded ? "true" : "false") + "}");
    }

    return SendHttp(clientSocket, 404, "text/plain", "Not Found");
}

void ControlServer::ThreadMain() {
    WSADATA wsa{};
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
        running_ = false;
        return;
    }

    SOCKET listenSock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (listenSock == INVALID_SOCKET) {
        WSACleanup();
        running_ = false;
        return;
    }

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(static_cast<u_short>(port_));
    inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);

    int opt = 1;
    setsockopt(listenSock, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&opt), sizeof(opt));

    if (bind(listenSock, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == SOCKET_ERROR) {
        closesocket(listenSock);
        WSACleanup();
        running_ = false;
        return;
    }

    if (listen(listenSock, 8) == SOCKET_ERROR) {
        closesocket(listenSock);
        WSACleanup();
        running_ = false;
        return;
    }

    while (running_) {
        fd_set readSet;
        FD_ZERO(&readSet);
        FD_SET(listenSock, &readSet);
        timeval tv{};
        tv.tv_sec = 0;
        tv.tv_usec = 300000;

        int rc = select(0, &readSet, nullptr, nullptr, &tv);
        if (rc <= 0) {
            continue;
        }

        SOCKET client = accept(listenSock, nullptr, nullptr);
        if (client == INVALID_SOCKET) {
            continue;
        }

        HandleClient(static_cast<uintptr_t>(client));
        shutdown(client, SD_BOTH);
        closesocket(client);
    }

    shutdown(listenSock, SD_BOTH);
    closesocket(listenSock);
    WSACleanup();
}

#else

ControlServer::ControlServer() {}
ControlServer::~ControlServer() {}
bool ControlServer::Start(int, std::shared_ptr<WebUiState>, UpdateCallback) { return false; }
void ControlServer::Stop() {}
std::string ControlServer::Url() const { return ""; }

#endif

