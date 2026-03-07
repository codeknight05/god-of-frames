#include "overlay_manager.h"

#ifdef _WIN32

#include <windows.h>
#include <process.h>

#include <cstdio>
#include <string>

namespace {

constexpr int kHotkeyId = 1001;
constexpr UINT kHotkeyMod = MOD_NOREPEAT;
constexpr UINT kHotkeyVk = VK_F10;
constexpr UINT WM_OVERLAY_REFRESH = WM_APP + 1;
constexpr int kOverlayX = 20;
constexpr int kOverlayY = 20;
constexpr int kFullWidth = 540;
constexpr int kFullHeight = 220;
constexpr int kMiniWidth = 170;
constexpr int kMiniHeight = 56;

const wchar_t* kOverlayClass = L"GodOfFramesOverlayClass";

std::wstring ToWide(const std::string& s) {
    if (s.empty()) return L"";
    int size = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
    if (size <= 0) return L"";
    std::wstring out(size, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, &out[0], size);
    if (!out.empty() && out.back() == L'\0') out.pop_back();
    return out;
}

OverlayManager* GetOverlay(HWND hwnd) {
    return reinterpret_cast<OverlayManager*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
}

LRESULT CALLBACK OverlayWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (msg == WM_NCCREATE) {
        const CREATESTRUCTW* cs = reinterpret_cast<const CREATESTRUCTW*>(lParam);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(cs->lpCreateParams));
        return DefWindowProcW(hwnd, msg, wParam, lParam);
    }

    OverlayManager* overlay = GetOverlay(hwnd);

    switch (msg) {
        case WM_HOTKEY:
            if (wParam == kHotkeyId) {
                if (overlay) {
                    const auto mode = overlay->CycleMode();
                    if (mode == OverlayManager::OverlayMode::Hidden) {
                        ShowWindow(hwnd, SW_HIDE);
                    } else {
                        const bool mini = (mode == OverlayManager::OverlayMode::Mini);
                        SetWindowPos(hwnd, HWND_TOPMOST, kOverlayX, kOverlayY,
                                     mini ? kMiniWidth : kFullWidth,
                                     mini ? kMiniHeight : kFullHeight,
                                     SWP_NOACTIVATE | SWP_SHOWWINDOW);
                        ShowWindow(hwnd, SW_SHOWNA);
                        InvalidateRect(hwnd, nullptr, TRUE);
                    }
                }
            }
            return 0;
        case WM_OVERLAY_REFRESH:
            InvalidateRect(hwnd, nullptr, TRUE);
            return 0;
        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hwnd, &ps);

            RECT rc;
            GetClientRect(hwnd, &rc);

            HBRUSH bg = CreateSolidBrush(RGB(15, 20, 28));
            FillRect(hdc, &rc, bg);
            DeleteObject(bg);

            SetBkMode(hdc, TRANSPARENT);
            SetTextColor(hdc, RGB(255, 170, 60));

            HFONT font = CreateFontW(24, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
                                     DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                                     CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                                     DEFAULT_PITCH | FF_DONTCARE, L"Consolas");
            HFONT old = reinterpret_cast<HFONT>(SelectObject(hdc, font));

            TelemetrySnapshot t;
            bool has = overlay && overlay->GetLatestSnapshot(&t);
            const auto mode = overlay ? overlay->GetMode() : OverlayManager::OverlayMode::Full;
            std::string alertUtf8;
            const bool hasAlert = overlay && overlay->GetActiveAlert(&alertUtf8);
            const std::wstring alertText = ToWide(alertUtf8);

            if (mode == OverlayManager::OverlayMode::Mini) {
                std::wstring fpsLine = L"FPS: N/A";
                if (has && t.observedFps >= 0.0) {
                    wchar_t buffer[64] = {};
                    std::swprintf(buffer, 64, L"FPS: %.0f", t.observedFps);
                    fpsLine = buffer;
                }
                std::wstring hint = L"F10";

                HFONT miniFont = CreateFontW(22, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
                                             DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                                             CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                                             DEFAULT_PITCH | FF_DONTCARE, L"Consolas");
                SelectObject(hdc, miniFont);
                SetTextColor(hdc, RGB(255, 220, 120));
                TextOutW(hdc, 10, 12, fpsLine.c_str(), static_cast<int>(fpsLine.size()));
                if (hasAlert) {
                    std::wstring alertShort = alertText;
                    if (alertShort.size() > 19) {
                        alertShort = alertShort.substr(0, 19) + L"...";
                    }
                    HFONT alertFont = CreateFontW(14, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
                                                  DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                                                  CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                                                  DEFAULT_PITCH | FF_DONTCARE, L"Consolas");
                    SelectObject(hdc, alertFont);
                    SetTextColor(hdc, RGB(255, 120, 120));
                    TextOutW(hdc, 10, 34, alertShort.c_str(), static_cast<int>(alertShort.size()));
                    SelectObject(hdc, miniFont);
                    DeleteObject(alertFont);
                } else {
                    SetTextColor(hdc, RGB(120, 180, 220));
                    TextOutW(hdc, 124, 30, hint.c_str(), static_cast<int>(hint.size()));
                }
                SelectObject(hdc, old);
                DeleteObject(miniFont);

                EndPaint(hwnd, &ps);
                return 0;
            }

            std::wstring line1 = L"God of Frames Overlay  (F10 cycles)";
            TextOutW(hdc, 12, 10, line1.c_str(), static_cast<int>(line1.size()));

            std::wstring l2, l3, l4, l5;
            if (!has) {
                l2 = L"Waiting for telemetry...";
                l3 = L"CPU: --   RAM: --   SYS: --";
                l4 = L"FPS: N/A";
                l5 = L"GAME: --";
            } else {
                wchar_t buffer[256] = {};
                std::swprintf(buffer, 256, L"CPU: %.1f%%   SYS RAM: %.1f%%   GAME RAM: %.0f MB",
                              t.processCpuPercent, t.systemMemoryUsedPercent, t.processWorkingSetMB);
                l2 = buffer;

                if (t.observedFps >= 0.0) {
                    std::swprintf(buffer, 256, L"FPS: %.1f", t.observedFps);
                } else {
                    std::swprintf(buffer, 256, L"FPS: N/A");
                }
                l3 = buffer;

                std::swprintf(buffer, 256, L"Severity: %.2f   PID: %d", t.inferredSeverity, t.pid);
                l4 = buffer;

                std::wstring game = ToWide(t.gameExe);
                l5 = L"GAME: " + game;
            }

            int y2 = 50;
            int y3 = 84;
            int y4 = 118;
            int y5 = 152;

            if (hasAlert) {
                RECT alertRc{10, 44, rc.right - 10, 74};
                HBRUSH alertBg = CreateSolidBrush(RGB(86, 26, 26));
                FillRect(hdc, &alertRc, alertBg);
                DeleteObject(alertBg);

                HFONT alertFont = CreateFontW(16, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
                                              DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                                              CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                                              DEFAULT_PITCH | FF_DONTCARE, L"Consolas");
                SelectObject(hdc, alertFont);
                std::wstring alertLine = L"Severe drop: " + alertText;
                SetTextColor(hdc, RGB(255, 215, 215));
                TextOutW(hdc, 14, 50, alertLine.c_str(), static_cast<int>(alertLine.size()));
                SelectObject(hdc, font);
                DeleteObject(alertFont);

                y2 = 84;
                y3 = 116;
                y4 = 148;
                y5 = 180;
            }

            SetTextColor(hdc, RGB(170, 235, 255));
            TextOutW(hdc, 12, y2, l2.c_str(), static_cast<int>(l2.size()));
            TextOutW(hdc, 12, y3, l3.c_str(), static_cast<int>(l3.size()));
            TextOutW(hdc, 12, y4, l4.c_str(), static_cast<int>(l4.size()));
            TextOutW(hdc, 12, y5, l5.c_str(), static_cast<int>(l5.size()));

            SelectObject(hdc, old);
            DeleteObject(font);

            EndPaint(hwnd, &ps);
            return 0;
        }
        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
        default:
            return DefWindowProcW(hwnd, msg, wParam, lParam);
    }
}

} // namespace

OverlayManager::OverlayManager() {}
OverlayManager::~OverlayManager() { Stop(); }

bool OverlayManager::Start() {
    if (running_) return true;
    running_ = true;

    uintptr_t h = _beginthreadex(nullptr, 0,
        [](void* p) -> unsigned int {
            reinterpret_cast<OverlayManager*>(p)->ThreadMain();
            return 0;
        }, this, 0, &threadId_);

    if (h == 0) {
        running_ = false;
        return false;
    }

    threadHandle_ = reinterpret_cast<void*>(h);
    return true;
}

void OverlayManager::Stop() {
    if (!running_) return;
    running_ = false;

    HWND hwnd = reinterpret_cast<HWND>(hwnd_);
    if (hwnd) {
        PostMessageW(hwnd, WM_CLOSE, 0, 0);
    }

    HANDLE hThread = reinterpret_cast<HANDLE>(threadHandle_);
    if (hThread) {
        WaitForSingleObject(hThread, 3000);
        CloseHandle(hThread);
        threadHandle_ = nullptr;
    }
}

void OverlayManager::UpdateSnapshot(const TelemetrySnapshot& snapshot) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        latest_ = snapshot;
        hasSnapshot_ = true;
    }

    HWND hwnd = reinterpret_cast<HWND>(hwnd_);
    if (hwnd) {
        PostMessageW(hwnd, WM_OVERLAY_REFRESH, 0, 0);
    }
}

void OverlayManager::ShowAlert(const std::string& reason, int durationMs) {
    if (reason.empty()) return;

    {
        std::lock_guard<std::mutex> lock(mutex_);
        alertText_ = reason;
        const ULONGLONG now = GetTickCount64();
        const ULONGLONG duration = static_cast<ULONGLONG>(durationMs > 300 ? durationMs : 300);
        alertExpireTickMs_ = now + duration;
    }

    HWND hwnd = reinterpret_cast<HWND>(hwnd_);
    if (hwnd) {
        PostMessageW(hwnd, WM_OVERLAY_REFRESH, 0, 0);
    }
}

bool OverlayManager::GetActiveAlert(std::string* out) {
    if (!out) return false;

    std::lock_guard<std::mutex> lock(mutex_);
    if (alertText_.empty()) return false;
    const ULONGLONG now = GetTickCount64();
    if (now >= alertExpireTickMs_) {
        alertText_.clear();
        alertExpireTickMs_ = 0;
        return false;
    }

    *out = alertText_;
    return true;
}

bool OverlayManager::GetLatestSnapshot(TelemetrySnapshot* out) const {
    if (!out) return false;
    std::lock_guard<std::mutex> lock(mutex_);
    if (!hasSnapshot_) return false;
    *out = latest_;
    return true;
}

OverlayManager::OverlayMode OverlayManager::GetMode() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return mode_;
}

OverlayManager::OverlayMode OverlayManager::CycleMode() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (mode_ == OverlayMode::Full) {
        mode_ = OverlayMode::Mini;
    } else if (mode_ == OverlayMode::Mini) {
        mode_ = OverlayMode::Hidden;
    } else {
        mode_ = OverlayMode::Full;
    }
    return mode_;
}

void OverlayManager::ThreadMain() {
    HINSTANCE hInst = GetModuleHandleW(nullptr);

    WNDCLASSW wc{};
    wc.lpfnWndProc = OverlayWndProc;
    wc.hInstance = hInst;
    wc.lpszClassName = kOverlayClass;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = reinterpret_cast<HBRUSH>(GetStockObject(BLACK_BRUSH));
    RegisterClassW(&wc);

    HWND hwnd = CreateWindowExW(
        WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_LAYERED | WS_EX_TRANSPARENT,
        kOverlayClass,
        L"God of Frames Overlay",
        WS_POPUP,
        kOverlayX,
        kOverlayY,
        kFullWidth,
        kFullHeight,
        nullptr,
        nullptr,
        hInst,
        this);

    hwnd_ = hwnd;

    if (!hwnd) {
        running_ = false;
        return;
    }

    SetLayeredWindowAttributes(hwnd, 0, 220, LWA_ALPHA);
    ShowWindow(hwnd, SW_SHOW);
    UpdateWindow(hwnd);

    RegisterHotKey(hwnd, kHotkeyId, kHotkeyMod, kHotkeyVk);

    MSG msg;
    while (running_ && GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    UnregisterHotKey(hwnd, kHotkeyId);
    hwnd_ = nullptr;
}

#else

OverlayManager::OverlayManager() {}
OverlayManager::~OverlayManager() {}
bool OverlayManager::Start() { return false; }
void OverlayManager::Stop() {}
void OverlayManager::UpdateSnapshot(const TelemetrySnapshot&) {}
void OverlayManager::ShowAlert(const std::string&, int) {}
bool OverlayManager::GetActiveAlert(std::string*) { return false; }
bool OverlayManager::GetLatestSnapshot(TelemetrySnapshot*) const { return false; }
OverlayManager::OverlayMode OverlayManager::GetMode() const { return OverlayMode::Hidden; }
OverlayManager::OverlayMode OverlayManager::CycleMode() { return OverlayMode::Hidden; }

#endif

