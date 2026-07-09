#pragma once
#include <windows.h>
#include <psapi.h>
#include <thread>
#include <atomic>
#include "SharedState.h"

namespace pmf {

// Lightweight background thread (low priority, sleeps most of the time)
// that samples this process's CPU usage every ~500ms and publishes it via
// SharedState. Kept off the input hot path entirely so stats collection
// never adds latency to mouse processing.
class MetricsMonitor {
public:
    explicit MetricsMonitor(SharedState& state);
    ~MetricsMonitor();

    void Start();
    void Stop();

private:
    void ThreadMain();

    SharedState& state_;
    std::thread thread_;
    std::atomic<bool> running_{false};
    int numProcessors_ = 1;
};

} // namespace pmf
