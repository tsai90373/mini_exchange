#pragma once

#include<map>
#include<vector>
#include"Types.hpp"
#include"OrderBook.hpp"


using OrderBooks = std::map<Symbol, OrderBook*>;

class Exchange {
private:
    struct TradeLog {
        OrdId aggressiveOrdId_;
        OrdId passiveOrdId_;
        Price price_;
        Qty qty_;
    };

    std::vector<TradeLog> tradeLogs_;

    template<typename BookSide>
    Qty match(BookSide& counterside, Qty req_qty, Price req_pri, bool is_buy) {
        // 成交：但是可能不只吃掉 ask1，必須一路往上找
        // 1. 先拿到ask1的queue
        // 2. 在ask1的queue一個一個配對看數量是否足夠
        while (req_qty > 0) {
            // TODO: 這邊每次一直重新拿看起來是不好的作法?
            auto& p = counterside.begin()->first;
            if ((p > req_pri && is_buy) || (p < req_pri && !is_buy))
                break;

            auto& q = counterside.begin()->second;
            Order* old_ord = q.front();
            // 情況1: 新單沒有完全成交
            if (old_ord->leaveQty_ < req_qty) {
                const Qty matched_qty = old_ord->leaveQty_;
                // neword 減掉 leaveqty
                req_qty -= matched_qty;

                // oldord直接滿足並且pop
                old_ord->filledQty_ = old_ord->iniQty_;
                old_ord->leaveQty_ = 0;
                tradeLogs_.emplace_back(TradeLog{0, old_ord->ordId_, old_ord->price_, matched_qty});
                q.pop();
            }
            // 情況2: 可以完全成交
            else {
                const Qty matched_qty = req_qty;
                // old_ord 減掉 qty
                old_ord->filledQty_ += req_qty;
                old_ord->leaveQty_ -= req_qty;
                tradeLogs_.emplace_back(TradeLog{0, old_ord->ordId_, old_ord->price_, matched_qty});
                if (old_ord->leaveQty_ == 0) {
                    q.pop();
                }
                // new ord 滿足直接結束
                req_qty = 0;
            }
            if (q.empty()) {
                counterside.erase(counterside.begin());
                if (counterside.empty()) 
                    break;
            }
        }
        return req_qty;
    };

public:
    // Books should be private, temparirly move to public for test
    OrderBooks books_;
    bool SendNew(Order&);

};
