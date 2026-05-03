#pragma once
#include <cstdint>
#include <time.h>

// CLOCK_MONOTONIC: 保證遞增，不受 NTP 跳動影響，適合算 latency
// vDSO 優化下成本約 4ns，不會真正進 kernel
// TODO: 理解 CLOCK_MONOTONIC 是什麼
inline uint64_t NowNs() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return static_cast<uint64_t>(ts.tv_sec) * 1'000'000'000ULL + ts.tv_nsec;
}
