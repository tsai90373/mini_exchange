#include<memory>
#include<vector>
#include<iostream>
#include"Order.hpp"
#include"Exchange.hpp"
#include"OrderBook.hpp"
#include"Wire.hpp"
#include"Timestamp.hpp"



ReportList Exchange::sendNew(Order& ord) {
    reports_.clear();

    // 先根據 symbid 查 symb
    auto symbInfo = symbMap_.find(ord.symbId_);
    if (symbInfo == symbMap_.end()) {
        // TODO: return 應該要有 error?
        return ReportList{};
    }

    // // TODO: 可能會有風險，應該用 find -> Q: 原本這裡有風險，但是現在已經確定symb存在所以book存在
    // if (books_.find(ord.symbId_) == books_.end()) {
    //     books_[ord.symbId_] = new OrderBook();
    // }
    // 目前是確定有 symbinfo 一定會有 orderbook
    auto bookInfo = books_.find(ord.symbId_);
    if (bookInfo == books_.end() || bookInfo->second == nullptr) {
        return ReportList{};
    }

    OrderBook* book = bookInfo->second;
    if (!book->isValidPrice(ord.price_)) {
        return ReportList{};
    }

    // 交易所填入 Order Id
    ord.ordId_ = ordIdCounter;
    ordIdCounter++;

    const auto log_begin = tradeLogs_.size();
    Price req_pri = ord.price_;
    Qty req_qty = ord.iniQty_;
    bool is_buy = ord.side_ == Side::Buy;
    ScopedRecord sendnew_guard(sendnew_latency_);  // 兩條 return path 都自動記錄

    const size_t req_pri_idx = book->getPriIndex(req_pri);

    // 新單買單
    if (is_buy) {
        if (book->hasAsk() && req_pri_idx >= book->ask1_) {
            uint64_t match_start_ts = now_ns();
            req_qty = match(*book, req_qty, req_pri, is_buy);
            match_latency_.record(now_ns() - match_start_ts);
        }

        reports_.push_back(genReport(ord, 'N'));
        ord.leaveQty_  = req_qty;
        ord.filledQty_ = ord.iniQty_ - ord.leaveQty_;
        reports_.push_back(genReport(ord, 'F'));

        // 有剩餘量：掛到 bids
        if (req_qty) {
            auto newOrd = std::make_unique<Order>(ord);
            Order* ptr = newOrd.get();
            orderPool_.insert_or_assign(ord.ordId_, std::move(newOrd));
            book->bids_[req_pri_idx].push(ptr);
            // best bid 只在新價更高、或原本沒 bid 時更新
            if (!book->hasBid() || req_pri_idx > book->bid1_) {
                book->bid1_ = req_pri_idx;
            }
        }
        for (size_t i = log_begin; i < tradeLogs_.size(); ++i)
            tradeLogs_[i].aggressiveOrdId_ = ord.ordId_;
        return reports_;
    }

    // 新單賣單
    if (book->hasBid() && req_pri_idx <= book->bid1_) {
        uint64_t match_start_ts = now_ns();
        req_qty = match(*book, req_qty, req_pri, is_buy);
        match_latency_.record(now_ns() - match_start_ts);
    }

    reports_.push_back(genReport(ord, 'N'));
    ord.leaveQty_  = req_qty;
    ord.filledQty_ = ord.iniQty_ - ord.leaveQty_;
    reports_.push_back(genReport(ord, 'F'));

    // 有剩餘量：掛到 asks
    if (req_qty) {
        auto newOrd = std::make_unique<Order>(ord);
        Order* ptr = newOrd.get();
        orderPool_.insert_or_assign(ord.ordId_, std::move(newOrd));
        book->asks_[req_pri_idx].push(ptr);
        // best ask 只在新價更低、或原本沒 ask 時更新
        if (!book->hasAsk() || req_pri_idx < book->ask1_) {
            book->ask1_ = req_pri_idx;
        }
    }
    for (size_t i = log_begin; i < tradeLogs_.size(); ++i)
        tradeLogs_[i].aggressiveOrdId_ = ord.ordId_;
    return reports_;
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
bool Exchange::sendChg(ChgRequest& req) {
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
        sendNew(newOrd);
        return true;
    }
    // 如果是只有改量
    orig_ord.leaveQty_ = req.newLeaveQty;
    return true;
}
bool Exchange::sendDel(OrdId ordId) {
    auto it = orderPool_.find(ordId);
    if (it == orderPool_.end())
        return false;
    Order& orig_ord = *it->second;

    orig_ord.leaveQty_ = 0;
    return true;
}

ExecReport Exchange::genReport(Order& ord, char exType) {

    ExecReport rpt;
    // 新單回報
    if (exType == 'N') {
        // 回傳新單請求的價量
        rpt.execType = 'N';
        rpt.size = sizeof(ExecReport);
        rpt.ordId = ord.ordId_;
        rpt.price = ord.price_;
        rpt.qty = ord.iniQty_;
        rpt.side = ord.side_ == Side::Buy ? 'B' : 'S';
        return rpt;
    }
    // 成交回報
    else if (exType == 'F') {
        rpt.execType = 'F';
        rpt.size = sizeof(ExecReport);
        rpt.ordId = ord.ordId_;
        rpt.price = ord.price_;
        // 成交量 & 剩餘量
        rpt.qty = ord.filledQty_;
        rpt.leaveQty = ord.leaveQty_;
        rpt.side = ord.side_ == Side::Buy ? 'B' : 'S';
        return rpt;
    }

    return rpt;

}

void Exchange::LoadSymbol() {
    Symbol symb1{"2330", Market::TSE, 100, 110, 90};
    symb1.TickSize_ = getTickSize(90);
    // 注意：這裡 symbol 必須要有 default constructor，因為會先用 default 再 copy construct
    symbMap_["2330"] = symb1;

    Symbol symb2{"2308", Market::TSE, 50, 55, 45};
    symb2.TickSize_ = getTickSize(45);
    symbMap_["2308"] = symb2;

    Symbol symb3{"2603", Market::TSE, 70, 77, 63};
    symb3.TickSize_ = getTickSize(63);
    symbMap_["2603"] = symb3;

    Symbol symb4{"2345", Market::TSE, 500, 550, 450};
    symb4.TickSize_ = getTickSize(450);
    symbMap_["2345"] = symb4;

    Symbol symb5{"6669", Market::TSE, 30, 33, 27};
    symb5.TickSize_ = getTickSize(27);
    symbMap_["6669"] = symb5;
}

void Exchange::GenOrderBooks() {
    for (auto pair: symbMap_) {
        Symbol symb = pair.second;
        uint64_t len = (symb.UpLmt_ - symb.DnLmt_) / symb.TickSize_ + 1;

        OrderBook* ordBook = new OrderBook();
        ordBook->bids_ = std::vector<std::queue<Order*>>(len);
        ordBook->asks_ = std::vector<std::queue<Order*>>(len);
        ordBook->bid1_ = ordBook->bids_.size();
        ordBook->ask1_ = ordBook->asks_.size();
        ordBook->DnLmt_ = symb.DnLmt_;
        // ordBook->Ref_ = symb.Ref_;
        // ordBook->UpLmt_ = symb.UpLmt_;
        ordBook->TickSize_ = symb.TickSize_;
        books_[symb.id_] = ordBook;
    }
}
