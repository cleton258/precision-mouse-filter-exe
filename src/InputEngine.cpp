#include "InputEngine.h"
#include <hidusage.h>
#include <algorithm>
#include <cmath>

namespace pmf {

InputEngine* InputEngine::s_instance = nullptr;

namespace {
constexpr wchar_t kWindowClassName[] = L"PrecisionMouseFilter_MsgWnd";
}

InputEngine::InputEngine(SharedState& state, MouseFilterPipeline& pipeline)
    : state_(state), pipeline_(pipeline) {
    QueryPerformanceFrequency(&qpcFrequency_);
    s_instance = this;
}

InputEngine::~InputEngine() {
    Stop();
    if (s_instance == this) s_instance = nullptr;
}

bool InputEngine::Start() {
    if (running_.exchange(true)) return true;
    thread_ = std::thread(&InputEngine::ThreadMain, this);

    // Wait briefly for the thread to finish initialization (hwnd_ created
    // and raw input registered) so Start() cannot return "success" before
    // the engine can actually receive input.
    for (int i = 0; i < 200 && running_.load() && hwnd_ == nullptr; ++i) {
        Sleep(5);
    }
    return running_.load() && hwnd_ != nullptr;
}

void InputEngine::Stop() {
    if (!running_.exchange(false)) return;
    if (threadId_) {
        PostThreadMessageW(threadId_, WM_QUIT, 0, 0);
    }
    if (thread_.joinable()) thread_.join();
}

void InputEngine::ThreadMain() {
    threadId_ = GetCurrentThreadId();

    // This thread's latency IS the tool's added input latency, so it gets
    // elevated priority and does nothing per-event beyond the filter math
    // and a single SetCursorPos call.
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL);

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = &InputEngine::WndProcStatic;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = kWindowClassName;
    RegisterClassExW(&wc);

    hwnd_ = CreateWindowExW(0, kWindowClassName, L"", 0, 0, 0, 0, 0,
                             HWND_MESSAGE, nullptr, wc.hInstance, nullptr);
    if (!hwnd_) {
        running_.store(false);
        return;
    }

    if (!RegisterRawInput()) {
        // Fail-safe: never install the suppression hook if we cannot
        // reliably receive raw input -- otherwise the cursor could get
        // suppressed with no filtered replacement ever arriving.
        DestroyWindow(hwnd_);
        hwnd_ = nullptr;
        running_.store(false);
        return;
    }

    hook_ = SetWindowsHookExW(WH_MOUSE_LL, &InputEngine::LowLevelMouseProcStatic,
                              GetModuleHandleW(nullptr), 0);
    // If the hook fails to install we keep running in observe-only mode
    // (metrics still work, cursor is simply never suppressed/filtered)
    // rather than tearing down the whole engine.

    MSG msg;
    while (running_.load() && GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    if (hook_) {
        UnhookWindowsHookEx(hook_);
        hook_ = nullptr;
    }
    DestroyWindow(hwnd_);
    hwnd_ = nullptr;
    UnregisterClassW(kWindowClassName, wc.hInstance);
}

bool InputEngine::RegisterRawInput() {
    RAWINPUTDEVICE rid{};
    rid.usUsagePage = HID_USAGE_PAGE_GENERIC;
    rid.usUsage = HID_USAGE_GENERIC_MOUSE;
    rid.dwFlags = RIDEV_INPUTSINK; // receive input even while not foreground
    rid.hwndTarget = hwnd_;
    return RegisterRawInputDevices(&rid, 1, sizeof(rid)) == TRUE;
}

LRESULT CALLBACK InputEngine::WndProcStatic(HWND hwnd, UINT msg, WPARAM w, LPARAM l) {
    if (s_instance) return s_instance->WndProc(hwnd, msg, w, l);
    return DefWindowProcW(hwnd, msg, w, l);
}

LRESULT InputEngine::WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (msg == WM_INPUT) {
        HandleRawInput(reinterpret_cast<HRAWINPUT>(lParam));
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

void InputEngine::HandleRawInput(HRAWINPUT hRawInput) {
    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);

    // Fixed-size stack buffer: RAWINPUT for a mouse report is well under
    // 64 bytes, so this avoids any heap allocation on the hot path.
    BYTE buffer[64];
    UINT size = sizeof(buffer);
    UINT ret = GetRawInputData(hRawInput, RID_INPUT, buffer, &size, sizeof(RAWINPUTHEADER));
    // GetRawInputData returns the number of bytes read, or -1 on error
    if (ret == static_cast<UINT>(-1)) return;
    if (size == 0 || size > sizeof(buffer)) return;

    auto* raw = reinterpret_cast<RAWINPUT*>(buffer);
    if (raw->header.dwType != RIM_TYPEMOUSE) return;

    const RAWMOUSE& mouse = raw->data.mouse;
    if (mouse.usFlags & MOUSE_MOVE_ABSOLUTE) {
        // Absolute-mode devices (some tablets, or input over a remote
        // desktop session) are outside this tool's scope -- do not attempt
        // relative-delta filtering on them.
        return;
    }

    double dx = static_cast<double>(mouse.lLastX);
    double dy = static_cast<double>(mouse.lLastY);
    if (dx == 0.0 && dy == 0.0) return; // e.g. a button-only report

    double dt = 0.001;
    if (haveLastSample_) {
        double elapsed = static_cast<double>(now.QuadPart - lastSampleQpc_.QuadPart) /
                          static_cast<double>(qpcFrequency_.QuadPart);
        dt = std::clamp(elapsed, 0.0001, 0.25);
        emaIntervalSeconds_ = (emaIntervalSeconds_ <= 0.0)
                                  ? elapsed
                                  : (emaIntervalSeconds_ * 0.9 + elapsed * 0.1);
    }
    lastSampleQpc_ = now;
    haveLastSample_ = true;

    bool enabled = state_.filterEnabled.load(std::memory_order_relaxed);
    if (enabled) {
        Vec2 filtered = pipeline_.Process(dx, dy, dt);

        POINT cur;
        GetCursorPos(&cur);
        double targetX = cur.x + filtered.x + remX_;
        double targetY = cur.y + filtered.y + remY_;

        long ix = static_cast<long>(std::lround(targetX));
        long iy = static_cast<long>(std::lround(targetY));
        remX_ = targetX - ix;
        remY_ = targetY - iy;

        SetCursorPos(ix, iy);
    }

    LARGE_INTEGER after;
    QueryPerformanceCounter(&after);
    double procUs = static_cast<double>(after.QuadPart - now.QuadPart) * 1000000.0 /
                    static_cast<double>(qpcFrequency_.QuadPart);

    double prevLatency = state_.avgProcessingLatencyUs.load(std::memory_order_relaxed);
    state_.avgProcessingLatencyUs.store(prevLatency * 0.95 + procUs * 0.05,
                                         std::memory_order_relaxed);
    if (emaIntervalSeconds_ > 0.0) {
        state_.pollingRateHz.store(1.0 / emaIntervalSeconds_, std::memory_order_relaxed);
    }
    state_.lastInputTickMs.store(static_cast<int64_t>(GetTickCount64()),
                                  std::memory_order_relaxed);

    // Diagnostics: only meaningful once the filter actually ran this sample
    // through the pipeline above.
    if (enabled) {
        DiagnosticsSnapshot diag = pipeline_.GetLastDiagnostics();
        state_.jitterScore.store(diag.noiseScore, std::memory_order_relaxed);
        if (diag.spikeClamped) {
            state_.totalSpikeCount.fetch_add(1, std::memory_order_relaxed);
        }
        if (diag.eventLossDetected) {
            state_.totalLostEventCount.fetch_add(1, std::memory_order_relaxed);
        }
        double prevPeak = state_.peakSpeedCountsPerSec.load(std::memory_order_relaxed);
        if (diag.outputSpeedCountsPerSec > prevPeak) {
            state_.peakSpeedCountsPerSec.store(diag.outputSpeedCountsPerSec, std::memory_order_relaxed);
        }
    }
}

LRESULT CALLBACK InputEngine::LowLevelMouseProcStatic(int code, WPARAM w, LPARAM l) {
    if (s_instance) return s_instance->HandleLowLevelMouse(code, w, l);
    return CallNextHookEx(nullptr, code, w, l);
}

LRESULT InputEngine::HandleLowLevelMouse(int code, WPARAM wParam, LPARAM lParam) {
    if (code == HC_ACTION && wParam == WM_MOUSEMOVE) {
        auto* info = reinterpret_cast<MSLLHOOKSTRUCT*>(lParam);

        bool enabled = state_.filterEnabled.load(std::memory_order_relaxed);
        if (!enabled) {
            return CallNextHookEx(nullptr, code, wParam, lParam);
        }
        if (info->flags & LLMHF_INJECTED) {
            // Our own SetCursorPos-driven update (or another tool's
            // synthetic input) -- never suppress or re-filter it.
            return CallNextHookEx(nullptr, code, wParam, lParam);
        }
        // Genuine hardware-driven movement: suppress the OS's own
        // unfiltered cursor update. The filtered equivalent for this same
        // physical sample is applied separately, from the raw-input handler
        // above -- the two are order-independent (see README).
        return 1;
    }
    // Only WM_MOUSEMOVE is ever intercepted. Clicks, scroll, and everything
    // else always pass through untouched.
    return CallNextHookEx(nullptr, code, wParam, lParam);
}

} // namespace pmf
