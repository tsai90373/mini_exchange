#include "FileLogSubscriber.hpp"

#include <chrono>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <stdexcept>

#include <nlohmann/json.hpp>

#include "core/EnumStrings.hpp"
#include "core/Order.hpp"

using nlohmann::json;

namespace {

// Wall clock（非 tools/Timestamp.hpp 的 CLOCK_MONOTONIC — 那個給 latency 量測，
// 這裡要的是人看得懂、能跟 broker 對帳單對時的時間）。
uint64_t WallNs() {
    using namespace std::chrono;
    return static_cast<uint64_t>(
        duration_cast<nanoseconds>(system_clock::now().time_since_epoch()).count());
}

int TodayYyyymmdd() {
    const std::time_t now = std::time(nullptr);
    std::tm tm{};
    localtime_r(&now, &tm);
    return (tm.tm_year + 1900) * 10000 + (tm.tm_mon + 1) * 100 + tm.tm_mday;
}

}  // namespace

FileLogSubscriber::FileLogSubscriber(std::string dir) : dir_(std::move(dir)) {
    std::filesystem::create_directories(dir_);
    RollIfNeeded();
}

void FileLogSubscriber::RollIfNeeded() {
    const int today = TodayYyyymmdd();
    if (today == open_yyyymmdd_ && out_.is_open()) return;

    char name[32];
    std::snprintf(name, sizeof(name), "orderraw-%08d.jsonl", today);
    path_ = dir_ + "/" + name;

    if (out_.is_open()) out_.close();
    out_.open(path_, std::ios::app);  // 重啟同一天 → append，不覆蓋
    if (!out_) {
        throw std::runtime_error("FileLogSubscriber: cannot open " + path_);
    }
    open_yyyymmdd_ = today;
}

void FileLogSubscriber::OnCommit(const OrderRaw& raw) {
    RollIfNeeded();

    const json line = {
        {"rxsno",           raw.rxsno_},
        {"ts_ns",           WallNs()},
        {"client_order_id", raw.id_},
        {"op_type",         ToString(raw.op_type_)},
        {"ord_st",          ToString(raw.ord_st_)},
        {"code",            raw.symb_},
        {"action",          ToString(raw.side_)},
        {"price",           raw.pri_},
        {"ini_qty",         raw.ini_qty_},
        {"leave_qty",       raw.leave_qty_},
        {"filled_qty",      raw.filled_qty_},
    };
    out_ << line.dump() << '\n';
    out_.flush();
    ++line_count_;

    // 寫失敗 = audit trail 斷了 → fail-fast。throw 會沿 Backend::Commit
    // 一路炸出 main loop，符合 ADR-0001「壞了就停」而非帶病續跑。
    if (!out_) {
        throw std::runtime_error("FileLogSubscriber: write failed on " + path_);
    }
}
