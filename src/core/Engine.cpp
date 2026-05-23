#include "Engine.hpp"

bool Engine::SendNew(const Request& req) {
    if (!RiskCheck(req)) {
        return false;
    }

    const ClOrdId cid = AllocateClOrdId();
    auto [it, inserted] = orders_.try_emplace(cid, std::make_unique<Order>(cid));
    Order& order = *it->second;

    OrderRaw* raw  = backend_.Begin(order);
    raw->id_       = cid;
    raw->side_     = req.side;
    raw->pri_      = req.pri;
    raw->ini_qty_  = req.qty;
    raw->leave_qty_= req.qty;
    raw->filled_qty_ = 0;
    raw->ord_st_   = OrdSt::SENDING;
    // TODO: raw->symb_ = req.symb;  Symbol 是 char[4]，需要 memcpy 或改成 struct wrapper
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
    backend_.Commit(raw);
    return true;
}

void Engine::OnReportIn(/* InboundEvent */) {
    // TODO: 待 inbound event struct 定義後實作。
    //       骨架：
    //         auto it = orders_.find(event.client_order_id);
    //         if (it == orders_.end()) return;  // 或 log
    //         OrderRaw* raw = backend_.Begin(*it->second);
    //         依 event 內容改 ord_st_ / leave_qty_ / filled_qty_ / ordno_
    //         backend_.Commit(raw);
}

bool Engine::RiskCheck(const Request& req) {
    // TODO: 系統層級風控（部位上限、單筆數量、價格合理性、total_profit_ 停損…）
    (void)req;
    return true;
}

ClOrdId Engine::AllocateClOrdId() {
    return cid_counter_++;
}
