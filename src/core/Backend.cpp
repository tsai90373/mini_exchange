#include "Backend.hpp"

OrderRaw* Backend::Begin(Order& order) {
    if (order.Empty()) {
        // 新單第一筆：以預設值建構，caller 接著覆寫 side / pri / qty / ord_st 等
        history_.emplace_back(order.Id(), &order);
    } else {
        // 改價 / 改量 / 回報：從 tail 複製狀態，caller 只覆寫變動欄位
        history_.emplace_back(*order.Tail());
        // rxsno_ 屬於每筆 raw 自己，不該繼承
        history_.back().rxsno_ = 0;
        // op_type_ 描述「這筆 commit 是什麼操作」，每次都不同；繼承會讓
        // PythonOrderAdapter 把 ack/fill 觸發的 commit 誤認成新請求重發。
        // caller (SendChg / OnOrderAck / OnFill) 必須顯式 set。
        history_.back().op_type_ = OpType::Unknown;
    }
    return &history_.back();
}

void Backend::Commit(OrderRaw* raw) {
    raw->rxsno_ = ++last_sno_;
    raw->GetOrder()->Append(raw);
    // 同步通知；W11 之後可改成 async drain
    for (auto sub : subscribers_) {
        sub->OnCommit(*raw);
    }
}

void Backend::Subscribe(Subscriber* sub) {
    subscribers_.push_back(std::move(sub));
}

const OrderRaw* Backend::GetBySno(RxSno sno) const {
    // V1 不變式：每個 Begin 一定配一個 Commit，
    // 因此 history_.size() == last_sno_，rxsno 與 deque index 一一對應。
    if (sno == 0 || sno > last_sno_) return nullptr;
    return &history_[sno - 1];
}
