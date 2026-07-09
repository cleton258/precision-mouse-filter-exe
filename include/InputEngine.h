#pragma once
#include <windows.h>
#include <thread>
#include <atomic>
#include "SharedState.h"
#include "MouseFilterPipeline.h"

namespace pmf {

// Owns two things that MUST live on the same OS thread:
//   1) A hidden message-only window registered for Raw Input (WM_INPUT),
//      which is our source of true, unaccelerated per-report mouse deltas.
//   2) A system-wide low-level mouse hook (WH_MOUSE_LL), whose only job is
//      to suppress the OS's own (unfiltered) cursor-move pipeline so that
//      the filtered value computed from (1) is the only thing that actually
//      moves the cursor.
//
// Both require a thread that is continuously pumping messages, so they share
// one dedicated, high-priority thread. That thread does the minimum possible
// work per event (no allocation, no locks) to keep latency low; all it does
// is: read the raw delta, run it through MouseFilterPipeline, and call
// SetCursorPos. Stats are published via lock-free atomics in SharedState.
//
// Re-entrancy safety: SetCursorPos generates its own WM_MOUSEMOVE, which
// would otherwise loop back into our own hook. Windows tags any
// programmatically-injected input with LLMHF_INJECTED in the hook's
// MSLLHOOKSTRUCT::flags; we check that flag to always let injected/synthetic
// movement pass through untouched, so we never suppress or re-filter our own
// output.
//
// This class has no knowledge of windows, processes, or games: it only ever
// reads (dx, dy) from the mouse device and writes an absolute cursor
// position. It cannot implement aim assistance because it never looks at
// screen content or any other process.
class InputEngine {
public:
    InputEngine(SharedState& state, MouseFilterPipeline& pipeline);
    ~InputEngine();

    InputEngine(const InputEngine&) = delete;
    InputEngine& operator=(const InputEngine&) = delete;

    // Starts the input thread. Returns false if raw input registration
    // failed, in which case the suppression hook is never installed (a
    // deliberate fail-safe: we will never suppress cursor movement unless we
    // can reliably reinject a filtered replacement for it).
    bool Start();
    void Stop();

private:
    void ThreadMain();
    bool RegisterRawInput();
    void HandleRawInput(HRAWINPUT hRawInput);

    static LRESULT CALLBACK WndProcStatic(HWND, UINT, WPARAM, LPARAM);
    LRESULT WndProc(HWND, UINT, WPARAM, LPARAM);

    static LRESULT CALLBACK LowLevelMouseProcStatic(int, WPARAM, LPARAM);
    LRESULT HandleLowLevelMouse(int code, WPARAM wParam, LPARAM lParam);

    SharedState& state_;
    MouseFilterPipeline& pipeline_;

    std::thread thread_;
    std::atomic<bool> running_{false};
    HWND hwnd_ = nullptr;
    HHOOK hook_ = nullptr;
    DWORD threadId_ = 0;

    LARGE_INTEGER qpcFrequency_{};
    LARGE_INTEGER lastSampleQpc_{};
    bool haveLastSample_ = false;
    double emaIntervalSeconds_ = 0.0;

    // Sub-pixel remainder carried between events so integer rounding never
    // leaks gain over time (keeps the 1:1 response exact on average, even at
    // sub-pixel-per-report speeds / low sensitivity).
    double remX_ = 0.0;
    double remY_ = 0.0;

    static InputEngine* s_instance;
};

} // namespace pmf
