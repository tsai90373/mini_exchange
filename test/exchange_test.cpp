#include <gtest/gtest.h>
#include <random>
#include "engine/Exchange.hpp"
#include "engine/Order.hpp"
#include "engine/Types.hpp"
#include "tools/Timestamp.hpp"

class ExchangeTest : public testing::Test {
protected:
    Exchange exchange;
    Symbol symb;
    void SetUp() override {
        symb.id_ = "2330";
        symb.mkt_ = Market::kTSE;
        exchange.LoadSymbol();
    }

    OrderBook* Book() {
        return exchange.books_.at(symb.id_);
    }

    size_t PriceIndex(Price price) {
        return Book()->GetPriIndex(price);
    }
};

TEST_F(ExchangeTest, BuyResting) {
    Order buy(symb.id_, Side::kBuy, 100, 10);
    exchange.SendNew(buy);
    EXPECT_EQ(Book()->bids_[PriceIndex(100)].size(), 1);
}

TEST_F(ExchangeTest, Match) {
    Order buy(symb.id_, Side::kBuy, 100, 10);
    exchange.SendNew(buy);
    Order sell(symb.id_, Side::kSell, 100, 10);
    exchange.SendNew(sell);
    EXPECT_EQ(Book()->bids_[PriceIndex(100)].size(), 0);
    EXPECT_EQ(Book()->asks_[PriceIndex(100)].size(), 0);
}

TEST_F(ExchangeTest, PartialMatch) {
    Order buy(symb.id_, Side::kBuy, 100, 10);
    exchange.SendNew(buy);
    // EXPECT_EQ(Book()->bids_[PriceIndex(100)].size(), 1);
    Order sell(symb.id_, Side::kSell, 100, 5);
    exchange.SendNew(sell);
    EXPECT_EQ(Book()->bids_[PriceIndex(100)].size(), 1);
    EXPECT_EQ(Book()->bids_[PriceIndex(100)].front()->leave_qty_, 5);
}

TEST_F(ExchangeTest, PriceMismatch) {
    Order buy(symb.id_, Side::kBuy, 90, 20);
    exchange.SendNew(buy);
    Order sell(symb.id_, Side::kSell, 100, 10);
    exchange.SendNew(sell);
    EXPECT_EQ(Book()->bids_[PriceIndex(90)].size(), 1);
    EXPECT_EQ(Book()->bids_[PriceIndex(90)].front()->leave_qty_, 20);
    EXPECT_EQ(Book()->asks_[PriceIndex(100)].size(), 1);
    EXPECT_EQ(Book()->asks_[PriceIndex(100)].front()->leave_qty_, 10);
}

TEST_F(ExchangeTest, MatchLatencyBenchmark) {
    constexpr int N = 100'000;
    std::mt19937 rng(12345);
    std::uniform_int_distribution<Price> price_dist(90, 110);

    for (int i = 0; i < N; ++i) {
        Order buy(symb.id_, Side::kBuy, price_dist(rng), 1);
        buy.recv_ts = NowNs();
        exchange.SendNew(buy);

        Order sell(symb.id_, Side::kSell, price_dist(rng), 1);
        sell.recv_ts = NowNs();
        exchange.SendNew(sell);
    }

    exchange.send_new_latency_.report();
    exchange.match_latency_.report();
}

TEST_F(ExchangeTest, MultilevelSweep) {
    Order buy(symb.id_, Side::kBuy, 90, 20);
    exchange.SendNew(buy);
    Order buy2(symb.id_, Side::kBuy, 100, 20);
    exchange.SendNew(buy2);
    Order buy3(symb.id_, Side::kBuy, 110, 20);
    exchange.SendNew(buy3);

    Order sell(symb.id_, Side::kSell, 90, 100);
    exchange.SendNew(sell);
    EXPECT_EQ(Book()->bids_[PriceIndex(90)].size(), 0);
    EXPECT_EQ(Book()->bids_[PriceIndex(100)].size(), 0);
    EXPECT_EQ(Book()->bids_[PriceIndex(110)].size(), 0);

    EXPECT_EQ(Book()->asks_[PriceIndex(90)].size(), 1);
    EXPECT_EQ(Book()->asks_[PriceIndex(90)].front()->leave_qty_, 40);
}
