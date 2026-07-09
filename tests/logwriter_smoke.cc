// Hand-rolled smoke test for FileLogSubscriber.
//
// 走真實路徑：Engine SendNew → ack → partial fill → full fill，每筆 commit
// 由 FileLogSubscriber 落地，再從磁碟讀回 jsonl 逐行驗證。
// Done 條件（REFACTOR_PLAN Phase 1）：行數 == Backend::HistorySize()。

#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <unistd.h>

#include <nlohmann/json.hpp>

#include "adapter/FileLogSubscriber.hpp"
#include "core/Backend.hpp"
#include "core/Engine.hpp"

using nlohmann::json;

namespace {

Request MakeReq(double pri, Qty qty) {
    Request req;
    req.symb = "TXFE5";
    req.side = Side::kBuy;
    req.pri  = pri;
    req.qty  = qty;
    return req;
}

OrderAckEvent MakeAck(const std::string& cid, OpType op, bool accepted) {
    OrderAckEvent ev;
    ev.client_order_id = cid;
    ev.op_type         = op;
    ev.accepted        = accepted;
    ev.op_code         = "00";
    return ev;
}

FillEvent MakeFill(const std::string& cid, double pri, Qty qty) {
    FillEvent ev;
    ev.client_order_id = cid;
    ev.code            = "TXFE5";
    ev.price           = pri;
    ev.quantity        = qty;
    return ev;
}

}  // namespace

int main() {
    const std::string dir =
        (std::filesystem::temp_directory_path() /
         ("tickengine_logwriter_smoke_" + std::to_string(::getpid()))).string();
    std::filesystem::remove_all(dir);

    {
        // Lifetime contract (Subscriber.hpp)：Backend → Subscriber → Engine
        Backend           backend;
        FileLogSubscriber log_writer(dir);
        backend.Subscribe(&log_writer);
        Engine engine(backend);

        assert(engine.SendNew(MakeReq(21500.0, 3)));             // cid 1
        engine.OnOrderAck(MakeAck("1", OpType::New, true));
        engine.OnFill(MakeFill("1", 21500.0, 1));
        engine.OnFill(MakeFill("1", 21500.0, 2));

        assert(log_writer.LineCount() == 4);
        assert(log_writer.LineCount() == backend.HistorySize());

        // 讀回磁碟逐行驗證
        std::ifstream in(log_writer.CurrentPath());
        assert(in.good());

        std::string line;
        int         n = 0;
        json        first, last;
        while (std::getline(in, line)) {
            const json j = json::parse(line);  // 每行都是合法 JSON
            ++n;
            assert(j.at("rxsno").get<uint32_t>() == static_cast<uint32_t>(n));
            assert(j.at("client_order_id").get<uint32_t>() == 1);
            assert(j.at("code").get<std::string>() == "TXFE5");
            assert(j.at("ts_ns").get<uint64_t>() > 0);
            if (n == 1) first = j;
            last = j;
        }
        assert(n == 4);

        // 第一行：SendNew 的 raw
        assert(first.at("op_type").get<std::string>() == "New");
        assert(first.at("ord_st").get<std::string>()  == "Sending");
        assert(first.at("leave_qty").get<uint32_t>()  == 3);

        // 最後一行：全部成交，op_type Unknown（broker event，不是 request）
        assert(last.at("op_type").get<std::string>()    == "Unknown");
        assert(last.at("ord_st").get<std::string>()     == "FullFilled");
        assert(last.at("filled_qty").get<uint32_t>()    == 3);
        assert(last.at("leave_qty").get<uint32_t>()     == 0);

        // 重啟同一天 → append 不覆蓋
        FileLogSubscriber second(dir);
        assert(second.CurrentPath() == log_writer.CurrentPath());
    }

    std::filesystem::remove_all(dir);
    std::cout << "[smoke] PASS logwriter: 4 lines round-tripped\n";
    return 0;
}
