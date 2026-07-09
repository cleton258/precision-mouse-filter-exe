#include "MetricsMonitor.h"
#include <cstdint>

namespace pmf {
namespace {

uint64_t FileTimeToU64(const FILETIME& ft) {
    ULARGE_INTEGER uli;
    uli.LowPart = ft.dwLowDateTime;
    uli.HighPart = ft.dwHighDateTime;
    return uli.QuadPart;
}

} // namespace

MetricsMonitor::MetricsMonitor(SharedState& state) : state_(state) {
    SYSTEM_INFO si;
    GetSystemInfo(&si);
    numProcessors_ = static_cast<int>(si.dwNumberOfProcessors);
    if (numProcessors_ < 1) numProcessors_ = 1;
}

MetricsMonitor::~MetricsMonitor() {
    Stop();
}

void MetricsMonitor::Start() {
    if (running_.exchange(true)) return;
    thread_ = std::thread(&MetricsMonitor::ThreadMain, this);
}

void MetricsMonitor::Stop() {
    if (!running_.exchange(false)) return;
    if (thread_.joinable()) thread_.join();
}

void MetricsMonitor::ThreadMain() {
    // This thread only ever reports diagnostics; it must never compete with
    // the input thread for CPU time, hence the reduced priority.
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_BELOW_NORMAL);

    FILETIME creationTime, exitTime, prevKernel, prevUser;
    GetProcessTimes(GetCurrentProcess(), &creationTime, &exitTime, &prevKernel, &prevUser);
    uint64_t prevKernelU = FileTimeToU64(prevKernel);
    uint64_t prevUserU = FileTimeToU64(prevUser);

    LARGE_INTEGER freq, prevWall;
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&prevWall);

    constexpr int kSampleIntervalMs = 500;

    while (running_.load()) {
        Sleep(kSampleIntervalMs);
        if (!running_.load()) break;

        FILETIME kernelNow, userNow, ct, et;
        GetProcessTimes(GetCurrentProcess(), &ct, &et, &kernelNow, &userNow);
        uint64_t kernelU = FileTimeToU64(kernelNow);
        uint64_t userU = FileTimeToU64(userNow);

        LARGE_INTEGER wallNow;
        QueryPerformanceCounter(&wallNow);

        double wallElapsedSec = static_cast<double>(wallNow.QuadPart - prevWall.QuadPart) /
                                 static_cast<double>(freq.QuadPart);
        // FILETIME resolution is 100ns.
        double cpuElapsedSec =
            static_cast<double>((kernelU - prevKernelU) + (userU - prevUserU)) / 1.0e7;

        if (wallElapsedSec > 0.0) {
            double pct = (cpuElapsedSec / (wallElapsedSec * numProcessors_)) * 100.0;
            if (pct < 0.0) pct = 0.0;
            state_.cpuUsagePercent.store(pct, std::memory_order_relaxed);
        }

        prevKernelU = kernelU;
        prevUserU = userU;
        prevWall = wallNow;

        PROCESS_MEMORY_COUNTERS pmc{};
        pmc.cb = sizeof(pmc);
        if (GetProcessMemoryInfo(GetCurrentProcess(), &pmc, sizeof(pmc))) {
            double mb = static_cast<double>(pmc.WorkingSetSize) / (1024.0 * 1024.0);
            state_.memoryUsageMB.store(mb, std::memory_order_relaxed);
        }
    }
}

} // namespace pmf
