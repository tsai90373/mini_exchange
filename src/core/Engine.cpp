#include "Engine.hpp"

#include <algorithm>
#include <iostream>
#include <limits>

bool Engine::SendNew(const Request& req) {
    if (!RiskCheck(req)) {
        return false;
    }

    const ClOrdId cid = AllocateClOrdId();
    auto [it, inserted] = orders_.try_emplace(cid, std::make_unique<Order>(cid));
    Order& order = *it->second;

    OrderRaw* raw    = backend_.Begin(order);
    raw->id_         = cid;
    raw->side_       = req.side;
    raw->pri_        = req.pri;
    raw->ini_qty_    = req.qty;
    raw->leave_qty_  = req.qty;
    raw->filled_qty_ = 0;
    raw->ord_st_     = OrdSt::SENDING;
    raw->symb_       = req.symb;
    raw->price_type_ = req.price_type;
    raw->order_type_ = req.order_type;
    raw->octype_     = req.octype;
    raw->account_    = req.account;
    raw->op_type_    = OpType::New;
    backend_.Commit(raw);
    return true;
}

bool Engine::SendChg(ClOrdId id, Qty new_qty, Price new_pri) {
    auto it = orders_.find(id);
    if (it == orders_.end()) {
        return false;  // 找不到對應 Order
    }
    Order& order = *it->second;

    // TODO: 檢查 Tail()->ord_st_ 是否允許改單（SENDING 時可能要 block，
    //       因為新單回報還沒回來，broker 那邊還沒有 ordno 可改）。
    //       目前先放行，由策略自己保證時序。

    OrderRaw* raw  = backend_.Begin(order);  // 已從 tail 複製狀態
    raw->leave_qty_= new_qty;
    if (new_pri != 0) {
        raw->pri_ = new_pri;
    }
    raw->ord_st_ = OrdSt::SENDING;
    // 三向分支對應 wire spec § 4 op_type：
    //   qty == 0      → Cancel（CONTEXT.md：no separate SendCancel API）
    //   new_pri != 0  → UpdatePrice
    //   else          → UpdateQty
    if (new_qty == 0) {
        raw->op_type_ = OpType::Cancel;
    } else if (new_pri != 0) {
        raw->op_type_ = OpType::UpdatePrice;
    } else {
        raw->op_type_ = OpType::UpdateQty;
    }
    backend_.Commit(raw);
    return true;
}

void Engine::OnTick(const Tick& ev) {
    // TODO(IStrategy): fan out to strategies' on_tick callbacks.
    //   V1 has no IStrategy interface yet — Engine.hpp only forward-declares it.
    std::cerr << "[engine] OnTick code=" << ev.code
              << " close=" << ev.deal
              << " vol=" << ev.volume
              << " simtrade=" << int(ev.simtrade)
              << " seq=" << ev.env.seq_num << '\n';
}

void Engine::OnBidAsk(const BidAsk& ev) {
    // TODO(IStrategy): fan out to strategies' on_bidask callbacks.
    std::cerr << "[engine] OnBidAsk code=" << ev.code
              << " bid1=" << ev.bid_price[0]
              << " ask1=" << ev.ask_price[0]
              << " seq=" << ev.env.seq_num << '\n';
}

// 狀態機（CONTEXT.md：每筆 broker event append 一筆 OrderRaw，audit trail）。
// Begin 已從 tail 複製狀態並 reset op_type_ = Unknown → PythonOrderAdapter
// 不會把這筆 commit 當成新請求重送。
//
//   accepted:  New          SENDING → NEWASK（fill 先到則保持不回退）
//              UpdateQty/Px → filled>0 ? PARTIAL_FILLED : NEWASK
//              Cancel       → CANCELLED, leave=0
//   rejected:  New          → FAILED, leave=0
//              Chg/Cancel   → filled>0 ? PARTIAL_FILLED : NEWASK（見下）
//   終態單的遲到 ack：狀態不動，只留 audit 紀錄。
void Engine::OnOrderAck(const OrderAckEvent& ev) {
    Order* order = LookupOrder(ev.client_order_id);
    if (!order) return;

    OrderRaw* raw = backend_.Begin(*order);
    const OrdSt cur = raw->ord_st_;  // == tail 狀態（Begin 複製而來）

    if (IsTerminal(cur)) {
        std::cerr << "[engine] late ack for terminal order cid=" << raw->id_
                  << " st=" << int(cur) << " (audit only)\n";
    } else if (ev.accepted) {
        switch (ev.op_type) {
            case OpType::New:
                // fill 早於 ack：已是 PARTIAL_FILLED 就不回退成 NEWASK
                if (cur == OrdSt::SENDING) raw->ord_st_ = OrdSt::NEWASK;
                break;
            case OpType::UpdateQty:
            case OpType::UpdatePrice:
                raw->ord_st_ = raw->filled_qty_ > 0 ? OrdSt::PARTIAL_FILLED
                                                    : OrdSt::NEWASK;
                break;
            case OpType::Cancel:
                raw->ord_st_    = OrdSt::CANCELLED;
                raw->leave_qty_ = 0;
                break;
            default:
                std::cerr << "[engine] WARN ack with op_type=Unknown cid="
                          << raw->id_ << " op_code=" << ev.op_code << '\n';
                break;
        }
    } else if (ev.op_type == OpType::New) {
        raw->ord_st_    = OrdSt::FAILED;
        raw->leave_qty_ = 0;
    } else {
        // Chg/Cancel 被拒：broker 端的單維持原狀。SendChg 樂觀寫入的
        // leave_qty_/pri_ 可能已與 broker 真值脫鉤 — V1 只把狀態復原成
        // 「還活著」並大聲 log，數值對帳等 Phase 6 reconciliation。
        raw->ord_st_ = raw->filled_qty_ > 0 ? OrdSt::PARTIAL_FILLED
                                            : OrdSt::NEWASK;
        std::cerr << "[engine] WARN chg/cancel rejected cid=" << raw->id_
                  << " op_code=" << ev.op_code << " op_msg=" << ev.op_msg
                  << " — leave_qty/pri may be desynced from broker\n";
    }
    backend_.Commit(raw);
}

void Engine::OnFill(const FillEvent& ev) {
    Order* order = LookupOrder(ev.client_order_id);
    if (!order) return;

    OrderRaw* raw = backend_.Begin(*order);
    const Qty q = ev.quantity;
    if (q > raw->leave_qty_) {
        // 樂觀 cancel（SendChg 先把 leave 寫 0）與在途 fill 的 race：
        // broker 的 fill 是事實 — filled 全額入帳，leave 只能扣到 0。
        std::cerr << "[engine] WARN fill qty " << q << " > leave "
                  << raw->leave_qty_ << " cid=" << raw->id_
                  << " (cancel/fill race?)\n";
    }
    raw->filled_qty_ += q;
    raw->leave_qty_  -= std::min(q, raw->leave_qty_);
    raw->ord_st_ = raw->leave_qty_ == 0 ? OrdSt::FULL_FILLED
                                        : OrdSt::PARTIAL_FILLED;
    backend_.Commit(raw);
    // 成交價 ev.price 不進 OrderRaw（沒有 fill-price 欄位）——Phase 2 的
    // Portfolio::ApplyFill 直接吃 FillEvent，Position / P&L 不在 Engine scope。
}

void Engine::OnConnEvent(const ConnEvent& ev) {
    std::cerr << "[engine] OnConnEvent src=" << int(ev.source)
              << " code=" << int(ev.code)
              << " info=" << ev.info << '\n';
    if (ev.code == EventCode::Disconnected) {
        // ADR-0001 fail-fast on disconnect. Flag rather than throw, so the
        // handler remains pure and the main loop owns the exit path.
        stop_requested_ = true;
    }
}

bool Engine::RiskCheck(const Request& req) {
    // TODO: 系統層級風控（部位上限、單筆數量、價格合理性、total_profit_ 停損…）
    (void)req;
    return true;
}

ClOrdId Engine::AllocateClOrdId() {
    return cid_counter_++;
}

Order* Engine::LookupOrder(const std::string& wire_cid) {
    ClOrdId cid = 0;
    try {
        size_t pos = 0;
        const unsigned long v = std::stoul(wire_cid, &pos);
        if (pos != wire_cid.size() ||
            v > std::numeric_limits<ClOrdId>::max()) {
            throw std::invalid_argument("trailing garbage or overflow");
        }
        cid = static_cast<ClOrdId>(v);
    } catch (...) {
        std::cerr << "[engine] FATAL unparsable client_order_id on wire: '"
                  << wire_cid << "' — fail-fast (ADR-0001)\n";
        stop_requested_ = true;
        return nullptr;
    }

    auto it = orders_.find(cid);
    if (it == orders_.end()) {
        // 本 process 沒發過這個 id：不是 bug 就是重啟後的孤兒回報。
        // 兩者 V1 都無法安全處理（安全的孤兒處理 = Phase 6 reconciliation）。
        std::cerr << "[engine] FATAL ack/fill for unknown client_order_id="
                  << cid << " — fail-fast (ADR-0001)\n";
        stop_requested_ = true;
        return nullptr;
    }
    return it->second.get();
}
