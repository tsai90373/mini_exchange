#pragma once

#include"Types.hpp"


class Order {
public:
    // 此為允許以 Order o; 的方式宣告，但在現在的情況不應該有空值
    // Order() = default;
    Order(SymbId symbId, Side side, Price p, Qty q) 
        : side_(side), symbId_(symbId), price_(p), iniQty_(q), leaveQty_(q), filledQty_(0) {}

    /// Note: the initializeation will follow the order of declaration
    /// If there are dependencies in intiailization, there might be mistakes
    OrdId ordId_;
    Side side_;
    SymbId symbId_;
    Price price_;
    Qty iniQty_;
    Qty leaveQty_;
    Qty filledQty_;
};

struct ChgRequest {
    OrdId ordId_;
    Price newPrice_; // 0表示不改價
    Qty newLeaveQty;
};
