#pragma once
#include<cstdint>
#include<array>


using OrdId = uint32_t;
using Symbol = std::array<char, 6>;
using Price = uint64_t;
using Qty = uint32_t;

inline Symbol makeSymbol(const char* s) {
    Symbol sym;
    for (int i = 0; i < strlen(s); i++) {
        sym[i] = s[i];
    } 
    return sym;
}

enum class Side {
    Buy,
    Sell
};