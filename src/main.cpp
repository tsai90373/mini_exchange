#include<cassert>
#include<string>
#include"engine/Exchange.hpp"
#include"engine/Types.hpp"
#include"network/Client.hpp"
#include"network/Session.hpp"
#include"network/Server.hpp"

int main(int argc, char* argv[]) {
    if (argc > 1 && std::string(argv[1]) == "client") {
        Client client({"127.0.0.1", 8080});
        client.Run();
    }
    else {
        Exchange exchange;
        // server 跟 main 同生命週期
        OrderSessionFactory fac{exchange};
        Server svr = Server{8080, &fac};
        svr.Run();
    }



    // 情況1: buy 價格不夠，直接掛單
    // Symbol sym = makeSymbol("AAPL");
    
    // // TODO: 應該改為 exchange 內部管理可下單 symbol
    // exchange.books_[sym] = new OrderBook();

    // Order buy1(1, sym, Side::kBuy, 100, 10);
    // exchange.SendNew(buy1);
    // OrderBook* book = exchange.books_[sym];
    // assert(book->bids_[100].size() == 1);
    // assert(book->bid1_ == 100);

    // // 情況2: sell 價格可以成交
    // Order sell1(2, sym, Side::kSell, 100, 10);
    // exchange.SendNew(sell1);
    // assert(book->bids_.empty());
    // assert(exchange.trade_logs_.size() == 1);
    // assert(exchange.trade_logs_[0].qty_ == 10);
 

    // // 情況3: partial fill
    // Order buy2(3, sym, Side::kBuy, 100, 5);
    // Order sell2(4, sym, Side::kSell, 100, 10);
    // exchange.SendNew(buy2);
    // exchange.SendNew(sell2);
    // assert(book->asks_[100].size() == 1);  // 剩餘掛到 asks
    // assert(exchange.trade_logs_[1].qty_ == 5);

    return 0;
}
