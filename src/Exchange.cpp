#include"Order.hpp"
#include"Exchange.hpp"
#include"OrderBook.hpp"


// TODO: 是否要 const ord?
bool Exchange::SendNew(Order& ord) {
    // TODO: 可能會有風險，應該用 find
    if (auto book = books_[ord.symb_]) {
        const auto log_begin = tradeLogs_.size();
        Price req_pri = ord.price_;
        Qty req_qty = ord.iniQty_;

        bool is_buy = ord.side_ == Side::Buy; 
        // 新單買單
        if (is_buy) {
            if (book->ask_1 > 0 && req_pri >= book->ask_1) 
                req_qty = match<Asks>(book->asks_, req_qty, req_pri, is_buy);

            ord.leaveQty_ = req_qty;
            ord.filledQty_ = ord.iniQty_ - ord.leaveQty_;
            // 更新 ask1
            if (book->asks_.empty()) 
                book->ask_1 = 0;
            else 
                book->ask_1 = book->asks_.begin()->first;

            // 有剩餘量: 掛到 bids 上面
            if (req_qty) {
                book->bids_[req_pri].push(&ord);
                // 既然可以直接成交，代表一定是最高價
                book->bid_1 = req_pri;
            }
            for (size_t i = log_begin; i < tradeLogs_.size(); ++i)
                tradeLogs_[i].aggressiveOrdId_ = ord.ordId_;
            return true;
        }
        // 新單賣單
        if (book->bid_1 > 0 && req_pri <= book->bid_1)
            req_qty = match<Bids>(book->bids_, req_qty, req_pri, is_buy);
        ord.leaveQty_ = req_qty;
        ord.filledQty_ = ord.iniQty_ - ord.leaveQty_;

        // 更新 bid_1
        if (book->bids_.empty()) 
            book->bid_1 = 0;
        else 
            book->bid_1 = book->bids_.begin()->first;

        // 有剩餘量: 掛到 asks 上面
        if (req_qty) {
            book->asks_[req_pri].push(&ord);
            // 既然可以直接成交，代表一定是最低價
            book->ask_1 = req_pri;
        }
        for (size_t i = log_begin; i < tradeLogs_.size(); ++i)
            tradeLogs_[i].aggressiveOrdId_ = ord.ordId_;
        
        return true;
    }
    
    return false;
}
