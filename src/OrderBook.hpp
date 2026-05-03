#pragma once

#include<cmath>
#include<map>
#include<queue>
#include"Types.hpp"
#include"Order.hpp"

// TODO: Object Pool
// using Bids = std::map<Price, std::queue<Order*>, std::greater<Price>>;
// using Asks = std::map<Price, std::queue<Order*>, std::less<Price>>;


using Bids = std::vector<std::queue<Order*>>;
using Asks = std::vector<std::queue<Order*>>;


class OrderBook {
public:
    Bids bids_;
    Asks asks_;
    // ask1_ / bid1_ 都是 index（不是 price）。
    // 哨兵：== bids_.size() / asks_.size() 表示該側為空。
    size_t ask1_ = 0;
    size_t bid1_ = 0;
    Price DnLmt_;
    double TickSize_;

    inline size_t getPriIndex(Price p) const {
        return static_cast<size_t>(std::llround((p - DnLmt_) / TickSize_));
    }

    inline bool isValidPrice(Price p) const {
        return p >= DnLmt_ && getPriIndex(p) < bids_.size();
    }

    inline bool hasAsk() const { return ask1_ < asks_.size(); }
    inline bool hasBid() const { return bid1_ < bids_.size(); }
};

/*
    現在要來優化 std::map
    std::map的問題是存取queue的時間是log(n)，需要改變成 O(1)
    如果用 array 的話有什麼辦法？
    不可能第一高價掛單排array第一個，第二高價排第二個，這樣如果有人的價格在bid1 bid2之間
    那 bid2 以後的位置全部都要往後移

    那另一個暴力方法
    把所有可能的價格都考慮進去，為每個價格都留一個位置
    例如我先做一個價格到index的mapping
    例如對於這個商品 55塊 對應的是 index 20
    然後我就可以透過兩次的O(1)存取拿到該價格的queue?
    不過缺點是在系統啟動時就必須做好所有商品的所有mapping?
    可以透過 price / tickszie 來算是第幾格
    但是商品可能價格的變動 ticksize 甚至會不一樣
    感覺也不好做

    目前想不到更好的辦法

    hashmap的話沒辦法知道bid ask的順序性
    所以似乎也沒辦法

*/

/*
    確定要走 bucket array 現在來思考缺少什麼？
    以台灣的股票市場為例，我需要有一張表格紀錄每個價格區間的 tick size
    最大的問題是會跨tick size的問題
    假設收盤價是100，代表今天漲停是110，跌停是90，但是100以上ticksize是0.5，100以下是0.1
    (price - lowest) / ticksize 會有出現錯誤
    目前的想法是如果跨ticksize就以比較小的ticksize來算
    缺點是會浪費很多位置
    例如100以上明明是0.5一跳，我如果全部用0.1來算，我會預留了100.1 100.2 100.3 100.4 的位置
    但是這樣可以最快速取得index
*/
