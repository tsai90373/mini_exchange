#include"Order.hpp"
#include"Exchange.hpp"
#include"OrderBook.hpp"
#include<memory>


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
                auto newOrd = std::make_unique<Order>(ord);
                Order* ptr = newOrd.get();
                orderPool_.insert_or_assign(ord.ordId_, std::move(newOrd));
                book->bids_[req_pri].push(ptr);
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
            /*  
                unordered_map 的 [] 必須確定 default constructor 存在
                orderPool_[ord.ordId_] -> 若 value 不存在，會先 call default constructor 再 assign
                要告訴 compiler 這個 key 一定存在 -> 用 at() 
            */
            auto newOrd = std::make_unique<Order>(ord);    // unique pointer
            Order* ptr = newOrd.get();                     // raw pointer
            orderPool_.insert_or_assign(ord.ordId_, std::move(newOrd));
            book->asks_[req_pri].push(ptr);
            // 既然可以直接成交，代表一定是最低價
            book->ask_1 = req_pri;
        }
        for (size_t i = log_begin; i < tradeLogs_.size(); ++i)
            tradeLogs_[i].aggressiveOrdId_ = ord.ordId_;
        
        return true;
    }
    
    return false;
}

/*
    看台灣證交所電文只有三種單：
    1. 新單
    2. 改量（減量）
    3. 刪單
    =======================
    - 改單應該帶入哪些欄位？
        OrdId (必要)
        帶入量 （需檢查是否 < 原始委託量）
        帶入價 or 帶入價量 (只要改價格其實就是重新排隊了，直接 call 新單流程)
*/
bool Exchange::SendChg(ChgRequest& req) {
    // TODO: 不確定是否需要一個新的資料結構，目前需要的欄位都有姑且先使用
    Price req_pri = req.newPrice_;
    Qty leaveQty = req.newLeaveQty;

    auto it = orderPool_.find(req.ordId_);
    if (it == orderPool_.end())
        return false;
    Order &orig_ord = *it->second;

    // 改量不能向上
    if (leaveQty > orig_ord.leaveQty_)
        return false;
    
    // ====檢查成功=====
    // 如果改價格了，那就是新單了
    if (req.newPrice_ != orig_ord.price_) {
        orig_ord.leaveQty_ = 0;
        // TODO: 應該要給予一個新的 ID?
        Order newOrd = orig_ord;
        newOrd.iniQty_ = req.newLeaveQty;
        newOrd.price_ = req.newPrice_;
        SendNew(newOrd);
        return true;
    }
    // 如果是只有改量
    orig_ord.leaveQty_ = req.newLeaveQty;
    return true;
}
bool Exchange::SendDel(OrdId ordId) {
    auto it = orderPool_.find(ordId);
    if (it == orderPool_.end())
        return false;
    Order& orig_ord = *it->second;

    orig_ord.leaveQty_ = 0;
    return true;
}