#pragma once
#include <cstdint>
#include <fstream>
#include <string>
#include <string_view>

#include "core/Subscriber.hpp"

// 每筆 Backend commit 寫一行 JSON 到 <dir>/orderraw-YYYYMMDD.jsonl。
// audit trail 的檔案形式：行數恆等於 Backend::HistorySize()。
//
// Flush 策略：每行 flush。crash 時已 commit 的 raw 不掉；retail 訊息量
// (每秒個位數 commit) 下 flush 成本可忽略，等 profiling 咬到再談 buffer。
//
// 跨日：每次 OnCommit 檢查 local date（夜盤跨午夜換檔）。
class FileLogSubscriber : public Subscriber {
public:
    // dir 不存在會建立。開檔失敗 throw std::runtime_error —
    // 沒有 audit log 就不該開盤（ADR-0001 fail-fast）。
    explicit FileLogSubscriber(std::string dir);

    void OnCommit(const OrderRaw& raw) override;
    std::string_view Name() const override { return "file-log"; }

    const std::string& CurrentPath() const { return path_; }
    uint64_t LineCount() const { return line_count_; }

private:
    // 回傳今天的 yyyymmdd；與 open_yyyymmdd_ 不同時開新檔。
    void RollIfNeeded();

    std::string   dir_;
    std::string   path_;
    std::ofstream out_;
    int           open_yyyymmdd_ = 0;
    uint64_t      line_count_    = 0;
};
