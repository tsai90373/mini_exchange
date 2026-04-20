#include<cassert>
#include<string>
#include"Exchange.hpp"
#include"Types.hpp"
#include"Client.hpp"
#include"Server.hpp"

int main(int argc, char* argv[]) {



    if (argc > 1 && std::string(argv[1]) == "client") {
        Client client({"127.0.0.1", 8080});
        client.run();
    }
    else {
        Exchange exchange;
        Server* svr = new Server{8080, exchange};
        svr->run();
    }



    // 情況1: buy 價格不夠，直接掛單
    // Symbol sym = makeSymbol("AAPL");
    
    // // TODO: 應該改為 exchange 內部管理可下單 symbol
    // exchange.books_[sym] = new OrderBook();

    // Order buy1(1, sym, Side::Buy, 100, 10);
    // exchange.SendNew(buy1);
    // OrderBook* book = exchange.books_[sym];
    // assert(book->bids_[100].size() == 1);
    // assert(book->bid_1 == 100);

    // // 情況2: sell 價格可以成交
    // Order sell1(2, sym, Side::Sell, 100, 10);
    // exchange.SendNew(sell1);
    // assert(book->bids_.empty());
    // assert(exchange.tradeLogs_.size() == 1);
    // assert(exchange.tradeLogs_[0].qty_ == 10);
 

    // // 情況3: partial fill
    // Order buy2(3, sym, Side::Buy, 100, 5);
    // Order sell2(4, sym, Side::Sell, 100, 10);
    // exchange.SendNew(buy2);
    // exchange.SendNew(sell2);
    // assert(book->asks_[100].size() == 1);  // 剩餘掛到 asks
    // assert(exchange.tradeLogs_[1].qty_ == 5);

    return 0;
}
