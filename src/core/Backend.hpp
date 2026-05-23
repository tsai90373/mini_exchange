#pragma once
#include <cstddef>
#include <deque>
#include <functional>
#include <vector>
#include "Order.hpp"
#include "Types.hpp"

// Append-only 歷史儲存 + RxSno 分配 + 事件發佈出口。
//
// 為什麼從 Engine 拆出來：
//   1. OrderRaw 需要全系統單調的 RxSno。Backend 是這個計數器的自然歸屬。
//   2. 「全系統歷史」需要單一來源，否則 audit / replay / log writer 都要
//      掃所有 Order 合併。
//   3. ZMQ outbound、log writer、W12 AI Audit 都是 OrderRaw 異動的訂閱者；
//      Backend 是單一發佈點，Engine 不必知道誰在訂閱。
//
// 執行緒：V1 單執行緒（ADR-0001 凍結至 W11）。Commit 同步呼叫所有 subscribers。
// W11 之後切換 lockfree queue + drain thread 是局部改動，對 Engine 透明。
class Backend {
public:
    // 為 order 開啟一筆新的 OrderRaw 異動。
    //
    // 若 order 沒有歷史（新單第一筆）：raw 以預設值建構。
    // 若 order 已有 tail：raw 從 tail 複製狀態，caller 再覆寫要改的欄位
    //   （例如改價只改 pri_、改數量只改 leave_qty_）。
    //
    // V1 不變式：每個 Begin 必須配對一個 Commit。Risk check / 前置驗證請在
    // Begin 之前完成。
    OrderRaw* Begin(Order& order);

    // 完成此筆異動：
    //   1. 分配 rxsno_ (= ++last_sno_)
    //   2. 掛進 order.raws_
    //   3. 同步通知所有 subscribers
    void Commit(OrderRaw* raw);

    // ZMQ outbound、log writer、audit 各自註冊一個 subscriber。
    // 註冊順序即觸發順序。
    void Subscribe(ISubscriber* sub);

    // 給 audit / replay：按 RxSno 取得歷史條目。
    // 回傳 nullptr 表示 sno 越界。
    const OrderRaw* GetBySno(RxSno sno) const;

    RxSno  LastSno() const { return last_sno_; }
    size_t HistorySize() const { return history_.size(); }

private:
    // pointer-stable：std::deque push_back / emplace_back 不會 invalidate
    // 既有元素的指標。Order::raws_ 與 subscriber 收到的 raw* / OrderRaw&
    // 都依賴這個保證。
    //
    // TODO(W11): 切換多執行緒時，把 history_ + subscriber 觸發改成
    //   「Commit 推入 ring buffer → drain thread 消費 + publish」。
    //   API 不變，Engine 不受影響。
    std::deque<OrderRaw> history_;
    RxSno last_sno_ = 0;
    std::vector<ISubscriber*> subscribers_;
};
