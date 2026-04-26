#include <gtest/gtest.h>
#include "../src/Exchange.hpp"
#include "../src/Order.hpp"
#include "../src/Types.hpp"

class ExchangeTest : public testing::Test {
protected:
    Exchange exchange;
    Symbol symb;
    void SetUp() override {
        memcpy(symb.id_.data(), "TSMC", 4);
        symb.mkt_ = Market::TSE;
        exchange.AddSymbol(symb);
    }
};

TEST_F(ExchangeTest, BuyResting) {
    Order buy(symb.id_, Side::Buy, 100, 10);
    exchange.sendNew(buy);
    EXPECT_EQ(exchange.books_[symb.id_]->bids_[100].size(), 1);
}

TEST_F(ExchangeTest, Match) {
    Order buy(symb.id_, Side::Buy, 100, 10);
    exchange.sendNew(buy);
    Order sell(symb.id_, Side::Sell, 100, 10);
    exchange.sendNew(sell);
    EXPECT_EQ(exchange.books_[symb.id_]->bids_[100].size(), 0);
    EXPECT_EQ(exchange.books_[symb.id_]->asks_[100].size(), 0);
}

TEST_F(ExchangeTest, PartialMatch) {
    Order buy(symb.id_, Side::Buy, 100, 10);
    exchange.sendNew(buy);
    Order sell(symb.id_, Side::Sell, 100, 5);
    exchange.sendNew(sell);
    EXPECT_EQ(exchange.books_[symb.id_]->bids_[100].size(), 1);
    EXPECT_EQ(exchange.books_[symb.id_]->bids_[100].front()->leaveQty_, 5);
}

TEST_F(ExchangeTest, PriceMismatch) {
    Order buy(symb.id_, Side::Buy, 90, 20);
    exchange.sendNew(buy);
    Order sell(symb.id_, Side::Sell, 100, 10);
    exchange.sendNew(sell);
    EXPECT_EQ(exchange.books_[symb.id_]->bids_[90].size(), 1);
    EXPECT_EQ(exchange.books_[symb.id_]->bids_[90].front()->leaveQty_, 20);
    EXPECT_EQ(exchange.books_[symb.id_]->asks_[100].size(), 1);
    EXPECT_EQ(exchange.books_[symb.id_]->asks_[100].front()->leaveQty_, 10);
}

TEST_F(ExchangeTest, MultilevelSweep) {
    Order buy(symb.id_, Side::Buy, 90, 20);
    exchange.sendNew(buy);
    Order buy2(symb.id_, Side::Buy, 100, 20);
    exchange.sendNew(buy2);
    Order buy3(symb.id_, Side::Buy, 110, 20);
    exchange.sendNew(buy3);

    Order sell(symb.id_, Side::Sell, 80, 100);
    exchange.sendNew(sell);
    EXPECT_EQ(exchange.books_[symb.id_]->bids_[90].size(), 0);
    EXPECT_EQ(exchange.books_[symb.id_]->bids_[100].size(), 0);
    EXPECT_EQ(exchange.books_[symb.id_]->bids_[100].size(), 0);

    EXPECT_EQ(exchange.books_[symb.id_]->asks_[80].size(), 1);
    EXPECT_EQ(exchange.books_[symb.id_]->asks_[80].front()->leaveQty_, 40);
}