#pragma once
#include <vector>
#include "Types.hpp"

class Order;

// 一次訂單異動的快照。Append-only：產生後就不應再變動內容（CONTEXT.md 已定義）。
//
// 生命週期：
//   1. Engine 收到下單/改單需求 → Backend.Begin(order) 在 history_ 配置一筆
//   2. Caller 覆寫需要改的欄位（ord_st_, leave_qty_ ...）
//   3. Backend.Commit(raw) 分配 rxsno_、掛進 order.raws_、通知 subscribers
//
// OrderRaw 的本體住在 Backend.history_ (std::deque)，
// Order 只持有指向它的 pointer (見 Order::raws_)。
class OrderRaw {
public:
    OrderRaw(ClOrdId id, Order* owner) : id_(id), order_(owner) {}

    // 預設 copyable，給 Backend.Begin() 從前一筆 tail 複製狀態用。
    OrderRaw(const OrderRaw&) = default;
    OrderRaw& operator=(const OrderRaw&) = default;

    // 在 Backend.Commit 時填入；commit 前固定為 0。
    RxSno    rxsno_     = 0;
    ClOrdId  id_;
    OrdNo    ordno_     = {};
    Symbol   symb_      = {};
    Price    pri_       = 0;
    Side     side_      = Side::kBuy;
    Qty      ini_qty_   = 0;
    Qty      leave_qty_ = 0;
    Qty      filled_qty_= 0;
    OrdSt    ord_st_    = OrdSt::SENDING;

    Order* GetOrder() const { return order_; }

private:
    Order* order_;  // 不擁有：order 本身住在 Engine.orders_ 裡
};

// 訂單容器：以 client_order_id 識別，持有此 Order 從新單到現在所有 OrderRaw 的導航
// 索引（不擁有 raw 本體）。
//
// raws_ 是 vector<OrderRaw*>，指標指向 Backend.history_ 裡的條目。Backend 用
// std::deque 儲存，push_back 不會 invalidate 既有指標，所以這些指標終身有效。
class Order {
public:
    explicit Order(ClOrdId id) : id_(id) {}

    // Backend.Commit() 內部呼叫：把剛 commit 的 raw 掛到本 Order 的歷史末端。
    void Append(OrderRaw* raw) { raws_.push_back(raw); }

    ClOrdId   Id()          const { return id_; }
    OrderRaw* Head()        const { return raws_.front(); }
    OrderRaw* Tail()        const { return raws_.back(); }
    size_t    UpdateCount() const { return raws_.size(); }
    bool      Empty()       const { return raws_.empty(); }
    const std::vector<OrderRaw*>& All() const { return raws_; }

private:
    ClOrdId id_;
    std::vector<OrderRaw*> raws_;  // 不擁有；raw 本體在 Backend.history_
};
