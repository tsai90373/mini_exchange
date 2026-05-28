#include "Engine.hpp"

#include <iostream>

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

void Engine::OnOrderAck(const OrderAckEvent& ev) {
    // TODO(state-machine): 真實處理時做：
    //   ClOrdId cid = std::stoul(ev.client_order_id);  // 可能 throw，要 try/catch
    //   auto it = orders_.find(cid);
    //   if (it == orders_.end()) { /* late ack for purged order, log */ return; }
    //   OrderRaw* raw = backend_.Begin(*it->second);
    //   raw->ord_st_ = ev.accepted ? OrdSt::NEWASK : OrdSt::FAILED;
    //   backend_.Commit(raw);
    // 等 IStrategy 跟「fill 早於 ack」的狀態機設計確定再實作。
    std::cerr << "[engine] OnOrderAck client_order_id=" << ev.client_order_id
              << " op_type=" << int(ev.op_type)
              << " accepted=" << ev.accepted
              << " op_code=" << ev.op_code << '\n';
}

void Engine::OnFill(const FillEvent& ev) {
    // TODO(state-machine): 同 OnOrderAck 的 stoul lookup pattern。
    //   raw->filled_qty_ += ev.quantity;
    //   raw->leave_qty_  -= ev.quantity;
    //   raw->ord_st_     = (raw->leave_qty_ == 0) ? OrdSt::FULL_FILLED : OrdSt::PARTIAL_FILLED;
    // Position / total_profit_ 更新在獨立 module（不在 Engine scope）。
    std::cerr << "[engine] OnFill client_order_id=" << ev.client_order_id
              << " code=" << ev.code
              << " px=" << ev.price
              << " qty=" << ev.quantity << '\n';
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
