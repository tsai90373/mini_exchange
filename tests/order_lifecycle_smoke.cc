// Hand-rolled smoke test for the Phase-0 order state machine
// (Engine::OnOrderAck / OnFill).
//
// No adapters, no ZMQ — drives Engine directly with hand-built events and
// asserts the OrderRaw chain via Backend::GetBySno / LastSno.
//
// cid 假設：cid_counter_ 從 1 起跳、每個 Engine instance 獨立，
// 所以第 N 次 SendNew 的 wire id 就是 std::to_string(N)。

#include <cassert>
#include <iostream>
#include <string>

#include "core/Backend.hpp"
#include "core/Engine.hpp"

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
    ev.op_code         = accepted ? "00" : "88";
    ev.op_msg          = accepted ? "" : "rejected-by-fixture";
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

const OrderRaw& LastRaw(const Backend& backend) {
    const OrderRaw* raw = backend.GetBySno(backend.LastSno());
    assert(raw != nullptr);
    return *raw;
}

}  // namespace

int main() {
    // --- Scenario 1: happy path New → ack → partial fill → full fill ---
    {
        Backend backend;
        Engine  engine(backend);

        assert(engine.SendNew(MakeReq(21500.0, 3)));            // cid 1
        assert(LastRaw(backend).ord_st_ == OrdSt::SENDING);

        engine.OnOrderAck(MakeAck("1", OpType::New, true));
        {
            const OrderRaw& raw = LastRaw(backend);
            assert(raw.ord_st_    == OrdSt::NEWASK);
            assert(raw.leave_qty_ == 3);
            assert(raw.op_type_   == OpType::Unknown);  // broker event，adapter 不重送
        }

        engine.OnFill(MakeFill("1", 21500.0, 1));
        {
            const OrderRaw& raw = LastRaw(backend);
            assert(raw.ord_st_     == OrdSt::PARTIAL_FILLED);
            assert(raw.filled_qty_ == 1);
            assert(raw.leave_qty_  == 2);
        }

        engine.OnFill(MakeFill("1", 21500.0, 2));
        {
            const OrderRaw& raw = LastRaw(backend);
            assert(raw.ord_st_     == OrdSt::FULL_FILLED);
            assert(raw.filled_qty_ == 3);
            assert(raw.leave_qty_  == 0);
        }

        // SendNew + ack + fill + fill = 4 raws，同一 Order
        assert(backend.HistorySize() == 4);
        assert(LastRaw(backend).GetOrder()->UpdateCount() == 4);
        assert(!engine.ShouldStop());
    }

    // --- Scenario 2: New rejected → FAILED, leave=0 ---
    {
        Backend backend;
        Engine  engine(backend);

        assert(engine.SendNew(MakeReq(21500.0, 3)));            // cid 1
        engine.OnOrderAck(MakeAck("1", OpType::New, false));
        const OrderRaw& raw = LastRaw(backend);
        assert(raw.ord_st_    == OrdSt::FAILED);
        assert(raw.leave_qty_ == 0);
    }

    // --- Scenario 3: New → ack → cancel → cancel-ack → CANCELLED ---
    {
        Backend backend;
        Engine  engine(backend);

        assert(engine.SendNew(MakeReq(21500.0, 3)));            // cid 1
        engine.OnOrderAck(MakeAck("1", OpType::New, true));
        assert(engine.SendChg(1, 0));                           // qty=0 → Cancel
        engine.OnOrderAck(MakeAck("1", OpType::Cancel, true));
        const OrderRaw& raw = LastRaw(backend);
        assert(raw.ord_st_    == OrdSt::CANCELLED);
        assert(raw.leave_qty_ == 0);
    }

    // --- Scenario 4: fill 早於 ack → 遲到的 New-ack 不回退狀態 ---
    {
        Backend backend;
        Engine  engine(backend);

        assert(engine.SendNew(MakeReq(21500.0, 2)));            // cid 1
        engine.OnFill(MakeFill("1", 21500.0, 2));               // fill 先到
        assert(LastRaw(backend).ord_st_ == OrdSt::FULL_FILLED);

        engine.OnOrderAck(MakeAck("1", OpType::New, true));     // ack 後到
        const OrderRaw& raw = LastRaw(backend);
        assert(raw.ord_st_     == OrdSt::FULL_FILLED);          // terminal 保持
        assert(raw.filled_qty_ == 2);
    }

    // --- Scenario 5: 樂觀 cancel 與在途 fill 的 race → filled 全額入帳 ---
    {
        Backend backend;
        Engine  engine(backend);

        assert(engine.SendNew(MakeReq(21500.0, 2)));            // cid 1
        engine.OnOrderAck(MakeAck("1", OpType::New, true));
        assert(engine.SendChg(1, 0));                           // 樂觀 cancel，leave→0
        engine.OnFill(MakeFill("1", 21500.0, 2));               // 但 broker 已成交
        const OrderRaw& raw = LastRaw(backend);
        assert(raw.ord_st_     == OrdSt::FULL_FILLED);
        assert(raw.filled_qty_ == 2);
        assert(raw.leave_qty_  == 0);                           // clamp，不 underflow
    }

    // --- Scenario 6: Chg rejected → 復原成活著的狀態（NEWASK）---
    {
        Backend backend;
        Engine  engine(backend);

        assert(engine.SendNew(MakeReq(21500.0, 2)));            // cid 1
        engine.OnOrderAck(MakeAck("1", OpType::New, true));
        assert(engine.SendChg(1, 1));                           // UpdateQty 2→1
        assert(LastRaw(backend).ord_st_ == OrdSt::SENDING);
        engine.OnOrderAck(MakeAck("1", OpType::UpdateQty, false));
        const OrderRaw& raw = LastRaw(backend);
        assert(raw.ord_st_ == OrdSt::NEWASK);                   // 單還活著
        assert(!engine.ShouldStop());
    }

    // --- Scenario 7: unknown cid → fail-fast (ADR-0001) ---
    {
        Backend backend;
        Engine  engine(backend);

        engine.OnOrderAck(MakeAck("42", OpType::New, true));    // 沒發過這個 id
        assert(engine.ShouldStop());
        assert(backend.HistorySize() == 0);                     // 沒有寫入任何 raw
    }

    // --- Scenario 8: garbage cid on wire → fail-fast ---
    {
        Backend backend;
        Engine  engine(backend);

        assert(engine.SendNew(MakeReq(21500.0, 1)));            // cid 1
        engine.OnFill(MakeFill("1x", 21500.0, 1));              // trailing garbage
        assert(engine.ShouldStop());
        assert(backend.HistorySize() == 1);                     // 只有 SendNew 那筆
    }

    std::cout << "[smoke] PASS order_lifecycle: 8 scenarios\n";
    return 0;
}
