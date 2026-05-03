#include<memory>
#include<vector>
#include<iostream>
#include"Order.hpp"
#include"Exchange.hpp"
#include"OrderBook.hpp"
#include"network/Wire.hpp"
#include"tools/Timestamp.hpp"



ReportList Exchange::SendNew(Order& ord) {
    reports_.clear();

    // 先根據 symbid 查 symb
    auto symb_info = symb_info_.find(ord.symb_id_);
    if (symb_info == symb_info_.end()) {
        // TODO: return 應該要有 error?
        return ReportList{};
    }

    // // TODO: 可能會有風險，應該用 find -> Q: 原本這裡有風險，但是現在已經確定symb存在所以book存在
    // if (books_.find(ord.symbId_) == books_.end()) {
    //     books_[ord.symbId_] = new OrderBook();
    // }
    // 目前是確定有 symbinfo 一定會有 orderbook
    auto book_info = books_.find(ord.symb_id_);
    if (book_info == books_.end() || book_info->second == nullptr) {
        return ReportList{};
    }

    OrderBook* book = book_info->second;
    if (!book->IsValidPrice(ord.price_)) {
        return ReportList{};
    }

    // 交易所填入 Order Id
    ord.ord_id_ = ord_id_counter_;
    ord_id_counter_++;

    const auto log_begin = trade_logs_.size();
    Price req_pri = ord.price_;
    Qty req_qty = ord.ini_qty_;
    bool is_buy = ord.side_ == Side::kBuy;
    ScopedRecord latency_guard(send_new_latency_);  // 兩條 return path 都自動記錄

    const size_t req_pri_idx = book->GetPriIndex(req_pri);

    // 新單買單
    if (is_buy) {
        if (book->HasAsk() && req_pri_idx >= book->ask1_) {
            uint64_t match_start_ts = NowNs();
            req_qty = Match(*book, req_qty, req_pri, is_buy);
            match_latency_.record(NowNs() - match_start_ts);
        }

        reports_.push_back(GenReport(ord, 'N'));
        ord.leave_qty_  = req_qty;
        ord.filled_qty_ = ord.ini_qty_ - ord.leave_qty_;
        reports_.push_back(GenReport(ord, 'F'));

        // 有剩餘量：掛到 bids
        if (req_qty) {
            auto new_ord = std::make_unique<Order>(ord);
            Order* ptr = new_ord.get();
            order_pool_.insert_or_assign(ord.ord_id_, std::move(new_ord));
            book->bids_[req_pri_idx].push(ptr);
            // best bid 只在新價更高、或原本沒 bid 時更新
            if (!book->HasBid() || req_pri_idx > book->bid1_) {
                book->bid1_ = req_pri_idx;
            }
        }
        for (size_t i = log_begin; i < trade_logs_.size(); ++i)
            trade_logs_[i].aggressive_ord_id = ord.ord_id_;
        return reports_;
    }

    // 新單賣單
    if (book->HasBid() && req_pri_idx <= book->bid1_) {
        uint64_t match_start_ts = NowNs();
        req_qty = Match(*book, req_qty, req_pri, is_buy);
        match_latency_.record(NowNs() - match_start_ts);
    }

    reports_.push_back(GenReport(ord, 'N'));
    ord.leave_qty_  = req_qty;
    ord.filled_qty_ = ord.ini_qty_ - ord.leave_qty_;
    reports_.push_back(GenReport(ord, 'F'));

    // 有剩餘量：掛到 asks
    if (req_qty) {
        auto new_ord = std::make_unique<Order>(ord);
        Order* ptr = new_ord.get();
        order_pool_.insert_or_assign(ord.ord_id_, std::move(new_ord));
        book->asks_[req_pri_idx].push(ptr);
        // best ask 只在新價更低、或原本沒 ask 時更新
        if (!book->HasAsk() || req_pri_idx < book->ask1_) {
            book->ask1_ = req_pri_idx;
        }
    }
    for (size_t i = log_begin; i < trade_logs_.size(); ++i)
        trade_logs_[i].aggressive_ord_id = ord.ord_id_;
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
bool Exchange::SendChg(ChgRequest& req) {
    // TODO: 不確定是否需要一個新的資料結構，目前需要的欄位都有姑且先使用
    Price req_pri = req.new_price;
    Qty leave_qty = req.remaining_qty;

    auto it = order_pool_.find(req.ord_id);
    if (it == order_pool_.end())
        return false;
    Order &orig_ord = *it->second;

    // 改量不能向上
    if (leave_qty > orig_ord.leave_qty_)
        return false;
    
    // ====檢查成功=====
    // 如果改價格了，那就是新單了
    if (req.new_price != orig_ord.price_) {
        orig_ord.leave_qty_ = 0;
        // TODO: 應該要給予一個新的 ID?
        Order new_ord = orig_ord;
        new_ord.ini_qty_ = req.remaining_qty;
        new_ord.price_ = req.new_price;
        SendNew(new_ord);
        return true;
    }
    // 如果是只有改量
    orig_ord.leave_qty_ = req.remaining_qty;
    return true;
}
bool Exchange::SendDel(OrdId ordId) {
    auto it = order_pool_.find(ordId);
    if (it == order_pool_.end())
        return false;
    Order& orig_ord = *it->second;

    orig_ord.leave_qty_ = 0;
    return true;
}

ExecReport Exchange::GenReport(Order& ord, char ex_type) {

    ExecReport rpt;
    // 新單回報
    if (ex_type == 'N') {
        // 回傳新單請求的價量
        rpt.exec_type = 'N';
        rpt.size = sizeof(ExecReport);
        rpt.ord_id = ord.ord_id_;
        rpt.price = ord.price_;
        rpt.qty = ord.ini_qty_;
        rpt.side = ord.side_ == Side::kBuy ? 'B' : 'S';
        return rpt;
    }
    // 成交回報
    else if (ex_type == 'F') {
        rpt.exec_type = 'F';
        rpt.size = sizeof(ExecReport);
        rpt.ord_id = ord.ord_id_;
        rpt.price = ord.price_;
        // 成交量 & 剩餘量
        rpt.qty = ord.filled_qty_;
        rpt.leave_qty = ord.leave_qty_;
        rpt.side = ord.side_ == Side::kBuy ? 'B' : 'S';
        return rpt;
    }

    return rpt;

}

void Exchange::LoadSymbol() {
    Symbol symb1{"2330", Market::kTSE, 100, 110, 90};
    symb1.tick_size_ = GetTickSize(90);
    // 注意：這裡 symbol 必須要有 default constructor，因為會先用 default 再 copy construct
    symb_info_["2330"] = symb1;

    Symbol symb2{"2308", Market::kTSE, 50, 55, 45};
    symb2.tick_size_ = GetTickSize(45);
    symb_info_["2308"] = symb2;

    Symbol symb3{"2603", Market::kTSE, 70, 77, 63};
    symb3.tick_size_ = GetTickSize(63);
    symb_info_["2603"] = symb3;

    Symbol symb4{"2345", Market::kTSE, 500, 550, 450};
    symb4.tick_size_ = GetTickSize(450);
    symb_info_["2345"] = symb4;

    Symbol symb5{"6669", Market::kTSE, 30, 33, 27};
    symb5.tick_size_ = GetTickSize(27);
    symb_info_["6669"] = symb5;
}

void Exchange::GenOrderBooks() {
    for (auto pair: symb_info_) {
        Symbol symb = pair.second;
        uint64_t len = (symb.uplmt_ - symb.dnlmt_) / symb.tick_size_ + 1;

        OrderBook* ord_book = new OrderBook();
        ord_book->bids_ = std::vector<std::queue<Order*>>(len);
        ord_book->asks_ = std::vector<std::queue<Order*>>(len);
        ord_book->bid1_ = ord_book->bids_.size();
        ord_book->ask1_ = ord_book->asks_.size();
        ord_book->dnlmt_ = symb.dnlmt_;
        // ord_book->ref_ = symb.ref_;
        // ord_book->uplmt_ = symb.uplmt_;
        ord_book->tick_size_ = symb.tick_size_;
        books_[symb.id_] = ord_book;
    }
}
