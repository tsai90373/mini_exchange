#pragma once
#include<map>
#include<unordered_map>
#include<vector>
#include<memory>
#include"Types.hpp"
#include"OrderBook.hpp"
#include"Order.hpp"
#include"network/Wire.hpp"
#include"tools/LatencyRecorder.hpp"


using OrderBooks = std::map<SymbId, OrderBook*>;

class Exchange {
private:
    struct TradeLog {
        OrdId aggressive_ord_id;
        OrdId passive_ord_id;
        Price price;
        Qty qty;
    };

    uint32_t ord_id_counter_ = 0;

    std::unordered_map<OrdId, std::unique_ptr<Order>> order_pool_;

    Qty Match(OrderBook& book, Qty req_qty, Price req_pri, bool is_buy) {
        const size_t req_pri_idx = book.GetPriIndex(req_pri);

        if (is_buy) {
            // 買單吃 ask：ask1_ 從低 idx 往高推進。
            while (req_qty > 0) {
                if (!book.HasAsk()) break;
                if (req_pri_idx < book.ask1_) break;

                auto& q = book.asks_[book.ask1_];
                if (q.empty()) {
                    ++book.ask1_;
                    continue;
                }

                Order* ord = q.front();
                if (ord->leave_qty_ == 0) {  // 已刪單
                    q.pop();
                    order_pool_.erase(ord->ord_id_);
                    continue;
                }

                const Qty filled = std::min(ord->leave_qty_, req_qty);
                ord->leave_qty_ -= filled;
                req_qty        -= filled;
                trade_logs_.emplace_back(TradeLog{0, ord->ord_id_, ord->price_, filled});

                if (ord->leave_qty_ == 0) {
                    q.pop();
                    order_pool_.erase(ord->ord_id_);
                }

                while (book.HasAsk() && book.asks_[book.ask1_].empty())
                    ++book.ask1_;
            }
            return req_qty;
        }

        // sell 吃 bid：bid1_ 從高 idx 往低推進。
        while (req_qty > 0) {
            if (!book.HasBid()) break;
            if (req_pri_idx > book.bid1_) break;

            auto& q = book.bids_[book.bid1_];
            if (q.empty()) {
                if (book.bid1_ == 0) { book.bid1_ = book.bids_.size(); break; }
                --book.bid1_;
                continue;
            }

            Order* ord = q.front();
            if (ord->leave_qty_ == 0) {
                q.pop();
                order_pool_.erase(ord->ord_id_);
                continue;
            }

            const Qty filled = std::min(ord->leave_qty_, req_qty);
            ord->leave_qty_ -= filled;
            req_qty        -= filled;
            trade_logs_.emplace_back(TradeLog{0, ord->ord_id_, ord->price_, filled});

            if (ord->leave_qty_ == 0) {
                q.pop();
                order_pool_.erase(ord->ord_id_);
            }

            // 推進 bid1_ 到下一個非空 level（往低 idx 走，注意 underflow）
            while (book.HasBid() && book.bids_[book.bid1_].empty()) {
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
    std::map<SymbId, Symbol> symb_info_;
    // Books should be private, temparirly move to public for test
    OrderBooks books_;
    std::vector<TradeLog> trade_logs_;
    // Q: 先用最間單的方式，如果有新單成功，就加入新單回報，如果有成交，就送兩個回報
    std::vector<ExecReport> reports_;

    // 1ns ~ 10s 範圍，3 significant figures (0.1% 誤差)
    LatencyRecorder send_new_latency_      {"SendNew", 1, 10'000'000'000LL, 3};
    LatencyRecorder match_latency_        {"match",   1, 10'000'000'000LL, 3};
    LatencyRecorder e2e_latency_          {"e2e",     1, 10'000'000'000LL, 3};

    ExecReport GenReport(Order&, char);

    ReportList SendNew(Order&);
    bool SendChg(ChgRequest&);
    bool SendDel(OrdId);

    // add some testing symbol 
    void LoadSymbol();

    void GenOrderBooks();
};
