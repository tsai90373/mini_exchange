#pragma once
#include<map>
#include<unordered_map>
#include<vector>
#include<memory>
#include"Types.hpp"
#include"OrderBook.hpp"
#include"Order.hpp"
#include"Wire.hpp"
#include"LatencyRecorder.hpp"


using OrderBooks = std::map<SymbId, OrderBook*>;

class Exchange {
private:
    struct TradeLog {
        OrdId aggressiveOrdId_;
        OrdId passiveOrdId_;
        Price price_;
        Qty qty_;
    };

    uint32_t ordIdCounter = 0;

    std::unordered_map<OrdId, std::unique_ptr<Order>> orderPool_;

    Qty match(OrderBook& book, Qty req_qty, Price req_pri, bool is_buy) {
        const size_t req_pri_idx = book.getPriIndex(req_pri);

        if (is_buy) {
            // 買單吃 ask：ask1_ 從低 idx 往高推進。
            while (req_qty > 0) {
                if (!book.hasAsk()) break;
                if (req_pri_idx < book.ask1_) break;

                auto& q = book.asks_[book.ask1_];
                if (q.empty()) {
                    ++book.ask1_;
                    continue;
                }

                Order* ord = q.front();
                if (ord->leaveQty_ == 0) {  // 已刪單
                    q.pop();
                    orderPool_.erase(ord->ordId_);
                    continue;
                }

                const Qty filled = std::min(ord->leaveQty_, req_qty);
                ord->leaveQty_ -= filled;
                req_qty        -= filled;
                tradeLogs_.emplace_back(TradeLog{0, ord->ordId_, ord->price_, filled});

                if (ord->leaveQty_ == 0) {
                    q.pop();
                    orderPool_.erase(ord->ordId_);
                }

                while (book.hasAsk() && book.asks_[book.ask1_].empty())
                    ++book.ask1_;
            }
            return req_qty;
        }

        // sell 吃 bid：bid1_ 從高 idx 往低推進。
        while (req_qty > 0) {
            if (!book.hasBid()) break;
            if (req_pri_idx > book.bid1_) break;

            auto& q = book.bids_[book.bid1_];
            if (q.empty()) {
                if (book.bid1_ == 0) { book.bid1_ = book.bids_.size(); break; }
                --book.bid1_;
                continue;
            }

            Order* ord = q.front();
            if (ord->leaveQty_ == 0) {
                q.pop();
                orderPool_.erase(ord->ordId_);
                continue;
            }

            const Qty filled = std::min(ord->leaveQty_, req_qty);
            ord->leaveQty_ -= filled;
            req_qty        -= filled;
            tradeLogs_.emplace_back(TradeLog{0, ord->ordId_, ord->price_, filled});

            if (ord->leaveQty_ == 0) {
                q.pop();
                orderPool_.erase(ord->ordId_);
            }

            // 推進 bid1_ 到下一個非空 level（往低 idx 走，注意 underflow）
            while (book.hasBid() && book.bids_[book.bid1_].empty()) {
                if (book.bid1_ == 0) { book.bid1_ = book.bids_.size(); break; }
                --book.bid1_;
            }
        }
        return req_qty;
    };

public:
    Exchange() { 
        LoadSymbol(); 
        GenOrderBooks();
    };
    std::map<SymbId, Symbol> symbMap_;
    // Books should be private, temparirly move to public for test
    OrderBooks books_;
    std::vector<TradeLog> tradeLogs_;
    // Q: 先用最間單的方式，如果有新單成功，就加入新單回報，如果有成交，就送兩個回報
    std::vector<ExecReport> reports_;

    // 1ns ~ 10s 範圍，3 significant figures (0.1% 誤差)
    LatencyRecorder sendnew_latency_      {"sendnew", 1, 10'000'000'000LL, 3};
    LatencyRecorder match_latency_        {"match",   1, 10'000'000'000LL, 3};
    LatencyRecorder e2e_latency_          {"e2e",     1, 10'000'000'000LL, 3};

    ExecReport genReport(Order&, char);

    ReportList sendNew(Order&);
    bool sendChg(ChgRequest&);
    bool sendDel(OrdId);

    // add some testing symbol 
    void LoadSymbol();

    void GenOrderBooks();
};
