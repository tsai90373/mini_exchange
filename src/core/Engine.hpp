#pragma once
#include <cstdint>
#include <memory>
#include <unordered_map>
#include <vector>
#include "Backend.hpp"
#include "InboundEvents.hpp"
#include "Order.hpp"
#include "Types.hpp"

class IStrategy;  // TODO: 定義介面（on_tick / on_fill ...）

// PriceType / OrderType / OCType / Account / OpType 已搬到 Types.hpp。
struct Request {
    Symbol    symb = {};
    Side      side = Side::kBuy;
    Price     pri  = 0;
    Qty       qty  = 0;
    PriceType price_type = PriceType::LMT;
    OrderType order_type = OrderType::ROD;
    OCType    octype     = OCType::Auto;
    Account   account    = Account::FutOpt;
};

// Core：協調者。職責：策略註冊、風控、下單流程、client_order_id → Order 索引。
//
// 跟 Backend 的關係：Engine 持有 Backend 的 reference，下單流程透過顯式
// backend_.Begin() / Commit() 寫入歷史。Engine 自己不擁有 OrderRaw 的本體。
//
// 跟 ZMQ outbound 的關係：Engine 不直接知道 ZMQ。outbound adapter 在啟動時
// 對 Backend 註冊一個 Subscriber，每筆 Commit 自動觸發送出。
class Engine {
public:
    explicit Engine(Backend& backend) : backend_(backend) {}

    void Register(IStrategy* s) { strategies_.push_back(s); }

    // 策略呼叫。回傳 false = 風控攔下或前置檢查失敗，沒寫進 Backend。
    bool SendNew(const Request& req);

    // CONTEXT.md：qty=0 即取消，不再有獨立的 SendCancel API。
    // new_pri 預設 0 表示不改價、只改量。
    // TODO: 若未來需要區分「保持原價」vs「改為 0 元」，改用 std::optional<Price>。
    bool SendChg(ClOrdId id, Qty new_qty, Price new_pri = 0);

    // PythonFeedAdapter 把 ZMQ 上解出來的 5 種 inbound msg 推進來。
    // V1 全部 log + TODO；真實 dispatch / 狀態機等 IStrategy 介面到位後補上。
    void OnTick      (const Tick&          ev);
    void OnBidAsk    (const BidAsk&        ev);
    void OnOrderAck  (const OrderAckEvent& ev);
    void OnFill      (const FillEvent&     ev);
    void OnConnEvent (const ConnEvent&     ev);

    // main loop 用這個決定要不要退出（OnConnEvent 收到 Disconnected 時會 set）。
    bool ShouldStop() const { return stop_requested_; }

private:
    bool    RiskCheck(const Request& req);
    ClOrdId AllocateClOrdId();

    Backend& backend_;

    // 命名修正（CONTEXT.md L59 已標註的舊命名 total_req_ 棄用）：
    // 這個 map 裡放的是 *Order*（容器），不是 transient 的 Request。
    std::unordered_map<ClOrdId, std::unique_ptr<Order>> orders_;

    std::vector<IStrategy*> strategies_;  // 不擁有

    // monotonic counter，per-process scope。Wire 上序列化為字串。見 ADR-0005。
    uint32_t cid_counter_ = 1;

    int64_t total_profit_ = 0;  // 當日已實現損益；超過上限時可停 Engine

    // OnConnEvent 收到 Disconnected 時 set true；bridge/main.cc 的主迴圈讀這個退出。
    bool stop_requested_ = false;
};
