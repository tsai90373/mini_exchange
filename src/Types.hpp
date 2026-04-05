#pragma once
#include<cstdint>
#include<array>


using OrdId = uint32_t;
using Symbol = std::array<char, 6>;
using Price = uint64_t;
using Qty = uint32_t;

enum class Side {
    Buy,
    Sell
};