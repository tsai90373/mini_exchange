#include"Order.hpp"
#include"Exchange.hpp"


// TODO: 是否要 const ord?
bool Exchange::SendNew(Order& ord) {
    if (auto book = books_[ord.symb_]) {
        Price req_pri = ord.price_;
        Qty req_qty = ord.iniQty_;

        // TODO: How to Log?
        // TODO: Buy & Sell 邏輯很像能不能用 Template 寫的優雅一點不要這麼重複？
        if (ord.side_ == Side::Buy) { 
            // 1. Greater than the price of ask1: deal!
            if (book->ask_1 > 0 && req_pri >= book->ask_1) {
                // 成交：但是可能不只吃掉 ask1，必須一路往上找
                // 1. 先拿到ask1的queue
                // 2. 在ask1的queue一個一個配對看數量是否足夠
                while (req_qty > 0) {
                    // TODO: 這邊每次一直重新拿看起來是不好的作法?
                    auto& p = book->asks_.begin()->first;
                    if (p > req_pri)
                        break;

                    auto& q = book->asks_.begin()->second;
                    Order& old_ord = q.front();
                    // 情況1: 新單沒有完全成交
                    if (old_ord.leaveQty_ < req_qty) {
                        // neword 減掉 leaveqty
                        req_qty -= old_ord.leaveQty_;

                        // oldord直接滿足並且pop
                        old_ord.filledQty_ = old_ord.iniQty_;
                        old_ord.leaveQty_ = 0;
                        // TODO: LOG()
                        q.pop();
                    }
                    // 情況2: 可以完全成交
                    else {
                        // old_ord 減掉 qty
                        old_ord.filledQty_ += req_qty;
                        old_ord.leaveQty_ -= req_qty;
                        if (old_ord.leaveQty_ == 0) {
                            q.pop();
                        }
                        // new ord 滿足直接結束
                        req_qty = 0;
                    }
                    if (q.empty()) {
                        book->asks_.erase(book->asks_.begin());
                        if (book->asks_.empty()) 
                            break;
                    }
                }
                /*
                    迴圈出來以後
                    1. 如果 req_qty = 0: 直接更新狀態後 return
                    2. 如果還有剩餘: 將ord掛上去bids
                    3. 更新 ask_1
                */ 
                ord.leaveQty_ = req_qty;
                ord.filledQty_ = ord.iniQty_ - ord.leaveQty_;

                if (book->asks_.empty()) 
                    book->ask_1 = 0;
                else 
                    book->ask_1 = book->asks_.begin()->first;

                if (req_qty > 0) {
                    book->bids_[req_pri].push(ord);
                    // 既然可以直接成交，代表一定是最高價
                    book->bid_1 = req_pri;
                }
            } 
            else {
                // 2. Smaller than the price of ask1: put on bids
                book->bids_[ord.price_].push(ord);
                book->bid_1 = std::max(book->bid_1, req_pri);
            }
            return true;
        }
        // 賣單新單
        else {

        }
        return true;
    }
    
    return false;
}