#pragma once
#include<cstdint>
#include<array>
#include<string>
#include<vector>


using OrdId = uint32_t;
using SymbId = std::string;
using Price = uint64_t;
using Qty = uint32_t;

inline double GetTickSize(uint64_t price) {
    if (price >= 10 && price < 50)
        return 0.05;
    else if (price >= 50 && price < 100)
        return 0.1;
    else if (price >= 100 && price < 500)
        return 0.5;
    else if (price > 500 && price < 1000)
        return 1;
    return 0;
}

enum class Side {
    kBuy,
    kSell
};

enum class Market {
    kTSE = 'T',
    kOTC = 'O',
};

class Symbol {
public:
    bool operator<(const Symbol& other) const {
        return id_ < other.id_;
    }
    Symbol() = default;
    Symbol(SymbId id, Market mkt, Price ref, Price uplmt, Price dnlmt)
        : id_(id), mkt_(mkt), ref_(ref), uplmt_(uplmt), dnlmt_(dnlmt) {};
    SymbId id_;
    Market mkt_;
    Price ref_;
    Price uplmt_;
    Price dnlmt_;
    double tick_size_;
};

