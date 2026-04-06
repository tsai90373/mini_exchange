#include<cassert>
#include"Exchange.hpp"
#include"Types.hpp"

int main() {

    Exchange exchange;


    // 情況1: buy 價格不夠，直接掛單
    Symbol sym = makeSymbol("AAPL");
    
    // TODO: 應該改為 exchange 內部管理可下單 symbol
    exchange.books_[sym] = new OrderBook();

    Order buy1(1, sym, Side::Buy, 100, 10);
    exchange.SendNew(buy1);
    OrderBook* book = exchange.books_[sym];
    assert(book->bids_[100].size() == 1);
    assert(book->bid_1 == 100);

    // 情況2: sell 價格可以成交
    Order sell1(2, sym, Side::Sell, 100, 10);
    exchange.SendNew(sell1);
    assert(book->bids_.empty());
    assert(buy1.leaveQty_ == 0);
    assert(sell1.leaveQty_ == 0);

    // 情況3: partial fill
    Order buy2(3, sym, Side::Buy, 100, 5);
    Order sell2(4, sym, Side::Sell, 100, 10);
    exchange.SendNew(buy2);
    exchange.SendNew(sell2);
    assert(buy2.leaveQty_ == 0);
    assert(sell2.leaveQty_ == 5);  // sell 剩 5
    assert(book->asks_[100].size() == 1);  // 剩餘掛到 asks

    return 0;
}
