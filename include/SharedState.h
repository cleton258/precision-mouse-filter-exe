#pragma once
#include <atomic>
#include <cstdint>

namespace pmf {

// Lock-free state shared between the input (hot-path), UI, and metrics
// threads. The hot path (InputEngine) only ever WRITES the metrics fields and
// READS filterEnabled; it never blocks on a lock or mutex, which matters
// because any stall on that thread is added directly to input latency.
struct SharedState {
    std::atomic<bool> filterEnabled{true};
    std::atomic<double> pollingRateHz{0.0};
    std::atomic<double> avgProcessingLatencyUs{0.0};
    std::atomic<double> cpuUsagePercent{0.0};
    std::atomic<int64_t> lastInputTickMs{0};

    // --- Diagnostics panel additions ---
    std::atomic<double> memoryUsageMB{0.0};
    std::atomic<double> jitterScore{0.0};             // last measured instability, 0..1
    std::atomic<uint64_t> totalSpikeCount{0};          // cumulative, since process start
    std::atomic<uint64_t> totalLostEventCount{0};      // cumulative, since process start
    // Resettable by the UI (e.g. before each auto-calibration phase) to
    // measure the peak speed observed within a specific time window.
    std::atomic<double> peakSpeedCountsPerSec{0.0};
};

} // namespace pmf
