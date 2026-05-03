#pragma once

#include "Types.hpp"
#include <cstdint>


class Order {
public:
    // 此為允許以 Order o; 的方式宣告，但在現在的情況不應該有空值
    // Order() = default;
    Order(SymbId symb_id, Side side, Price p, Qty q)
        : side_(side), symb_id_(symb_id), price_(p), ini_qty_(q), leave_qty_(q), filled_qty_(0) {}

    /// Note: the initializeation will follow the order of declaration
    /// If there are dependencies in intiailization, there might be mistakes
    OrdId ord_id_;
    Side side_;
    SymbId symb_id_;
    Price price_;
    Qty ini_qty_;
    Qty leave_qty_;
    Qty filled_qty_;

    uint64_t recv_ts  = 0;   // Session 解完 header 後立刻打，代表訂單進系統的時間
    uint64_t match_ts = 0;   // Exchange::SendNew 撮合完成後打
};

struct ChgRequest {
    OrdId ord_id;
    Price new_price; // 0表示不改價
    Qty remaining_qty;
};
