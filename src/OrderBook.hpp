#pragma once

#include<map>
#include<queue>
#include"Types.hpp"
#include"Order.hpp"


using Bids = std::map<Price, std::queue<Order>, std::greater<Price>>;
using Asks = std::map<Price, std::queue<Order>, std::less<Price>>;


class OrderBook {
public:
    Bids bids_;
    Asks asks_;
    Price ask_1 = 0;
    Price bid_1 = 0;
};