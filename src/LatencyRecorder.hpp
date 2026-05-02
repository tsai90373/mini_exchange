#pragma once
#include <hdr/hdr_histogram.h>
#include <cstdint>
#include <cstdio>
#include <stdexcept>
#include "Timestamp.hpp"

// RAII wrapper around hdr_histogram_c.
//
// HdrHistogram 用 bucket 計數而非存每筆原始值：
//   - O(1) record，O(1) percentile query
//   - memory 固定 (約 100KB per instance)
//   - 適合 real-time hot path 記錄
//
// 使用方式:
//   LatencyRecorder rec("e2e", 1, 10'000'000'000LL, 3);  // 1ns ~ 10s, 3位有效數字
//   rec.record(send_ts - recv_ts);
//   rec.report();
class LatencyRecorder {
public:
    // min_ns: 最小可記錄值 (ns)
    // max_ns: 最大可記錄值 (ns)，超過會被 clamp 到 max
    // significant_figures: 有效數字位數 (1~5)，決定精度 vs memory trade-off
    //   3 = 0.1% 誤差，約 100KB，業界常用值
    LatencyRecorder(const char* label, int64_t min_ns, int64_t max_ns, int significant_figures)
        : label_(label)
    {
        if (hdr_init(min_ns, max_ns, significant_figures, &hist_) != 0)
            throw std::runtime_error("hdr_init failed");
    }

    ~LatencyRecorder() {
        hdr_close(hist_);
    }

    // non-copyable: histogram 是 C 的 heap 物件，複製語意複雜
    LatencyRecorder(const LatencyRecorder&) = delete;
    LatencyRecorder& operator=(const LatencyRecorder&) = delete;

    void record(uint64_t latency_ns) {
        hdr_record_value(hist_, static_cast<int64_t>(latency_ns));
    }

    void reset() {
        hdr_reset(hist_);
    }

    void report() const {
        printf("=== %s (n=%lld) ===\n", label_, hist_->total_count);
        printf("  p50    = %lld ns\n", hdr_value_at_percentile(hist_, 50.0));
        printf("  p99    = %lld ns\n", hdr_value_at_percentile(hist_, 99.0));
        printf("  p99.9  = %lld ns\n", hdr_value_at_percentile(hist_, 99.9));
        printf("  p99.99 = %lld ns\n", hdr_value_at_percentile(hist_, 99.99));
        printf("  max    = %lld ns\n", hdr_max(hist_));
        printf("  mean   = %.1f ns\n", hdr_mean(hist_));
    }

private:
    const char*     label_;
    hdr_histogram*  hist_ = nullptr;
};

// 在 scope 結束時自動呼叫 record(now - start)
// 用法：在函數開頭宣告，不管哪條 return path 都會自動記錄
// TODO: 現在的 Scoped Record 是綁定 latencyRecorder，未來可以改成 Template + callback 的形式
struct ScopedRecord {
    LatencyRecorder& rec;
    uint64_t start;

    ScopedRecord(LatencyRecorder& r) : rec(r), start(now_ns()) {}
    ~ScopedRecord() { rec.record(now_ns() - start); }

    ScopedRecord(const ScopedRecord&) = delete;
    ScopedRecord& operator=(const ScopedRecord&) = delete;
};
