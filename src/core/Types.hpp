#pragma once
#include <stdint.h>
#include <string>

using Symbol = char[4];
// TODO: 價格精確度問題，C++處理這種問題需要自定義 Type？ 
using Price = double;
using Qty = uint32_t;
using ClOrdId = uint32_t;
using RxSno = uint32_t;
using OrdNo = char[5];

enum class Side {
    kBuy = 0,
    kSell = 1
};

// TODO: 了解 enum/enum class差異
// TODO: 去仔細看 f9omstw 是怎麼設計 有哪些 state
enum class OrdSt {
    FAILED = -2,
    CANCELLED = -1,
    SENDING = 0,
    NEWASK = 1,
    PARTIAL_FILLED = 2,
    FULL_FILLED = 3,
};