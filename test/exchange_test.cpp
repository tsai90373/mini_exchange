#include <gtest/gtest.h>
#include <random>
#include "../src/Exchange.hpp"
#include "../src/Order.hpp"
#include "../src/Types.hpp"
#include "../src/Timestamp.hpp"

class ExchangeTest : public testing::Test {
protected:
    Exchange exchange;
    Symbol symb;
    void SetUp() override {
        symb.id_ = "2330";
        symb.mkt_ = Market::TSE;
        exchange.LoadSymbol();
    }

    OrderBook* book() {
        return exchange.books_.at(symb.id_);
    }

    size_t priceIndex(Price price) {
        return book()->getPriIndex(price);
    }
};

TEST_F(ExchangeTest, BuyResting) {
    Order buy(symb.id_, Side::Buy, 100, 10);
    exchange.sendNew(buy);
    EXPECT_EQ(book()->bids_[priceIndex(100)].size(), 1);
}

TEST_F(ExchangeTest, Match) {
    Order buy(symb.id_, Side::Buy, 100, 10);
    exchange.sendNew(buy);
    Order sell(symb.id_, Side::Sell, 100, 10);
    exchange.sendNew(sell);
    EXPECT_EQ(book()->bids_[priceIndex(100)].size(), 0);
    EXPECT_EQ(book()->asks_[priceIndex(100)].size(), 0);
}

TEST_F(ExchangeTest, PartialMatch) {
    Order buy(symb.id_, Side::Buy, 100, 10);
    exchange.sendNew(buy);
    // EXPECT_EQ(book()->bids_[priceIndex(100)].size(), 1);
    Order sell(symb.id_, Side::Sell, 100, 5);
    exchange.sendNew(sell);
    EXPECT_EQ(book()->bids_[priceIndex(100)].size(), 1);
    EXPECT_EQ(book()->bids_[priceIndex(100)].front()->leaveQty_, 5);
}

TEST_F(ExchangeTest, PriceMismatch) {
    Order buy(symb.id_, Side::Buy, 90, 20);
    exchange.sendNew(buy);
    Order sell(symb.id_, Side::Sell, 100, 10);
    exchange.sendNew(sell);
    EXPECT_EQ(book()->bids_[priceIndex(90)].size(), 1);
    EXPECT_EQ(book()->bids_[priceIndex(90)].front()->leaveQty_, 20);
    EXPECT_EQ(book()->asks_[priceIndex(100)].size(), 1);
    EXPECT_EQ(book()->asks_[priceIndex(100)].front()->leaveQty_, 10);
}

TEST_F(ExchangeTest, MatchLatencyBenchmark) {
    constexpr int N = 100'000;
    std::mt19937 rng(12345);
    std::uniform_int_distribution<Price> price_dist(90, 110);

    for (int i = 0; i < N; ++i) {
        Order buy(symb.id_, Side::Buy, price_dist(rng), 1);
        buy.recv_ts = now_ns();
        exchange.sendNew(buy);

        Order sell(symb.id_, Side::Sell, price_dist(rng), 1);
        sell.recv_ts = now_ns();
        exchange.sendNew(sell);
    }

    exchange.sendnew_latency_.report();
    exchange.match_latency_.report();
}

TEST_F(ExchangeTest, MultilevelSweep) {
    Order buy(symb.id_, Side::Buy, 90, 20);
    exchange.sendNew(buy);
    Order buy2(symb.id_, Side::Buy, 100, 20);
    exchange.sendNew(buy2);
    Order buy3(symb.id_, Side::Buy, 110, 20);
    exchange.sendNew(buy3);

    Order sell(symb.id_, Side::Sell, 90, 100);
    exchange.sendNew(sell);
    EXPECT_EQ(book()->bids_[priceIndex(90)].size(), 0);
    EXPECT_EQ(book()->bids_[priceIndex(100)].size(), 0);
    EXPECT_EQ(book()->bids_[priceIndex(110)].size(), 0);

    EXPECT_EQ(book()->asks_[priceIndex(90)].size(), 1);
    EXPECT_EQ(book()->asks_[priceIndex(90)].front()->leaveQty_, 40);
}
