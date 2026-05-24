#pragma once
#include <stdint.h>
#include <string>

// 合約代碼。長度從 5（"TXFE5"）到 ~16（"TXO202506C21000"）不等；用 std::string
// 直接吃下，避免固定 buffer 截斷。優化路徑：之後 hot path 需要時可改 small-string
// 或 interning，wire/event 介面型別不變。
using Symbol = std::string;
// TODO: 價格精確度問題，C++處理這種問題需要自定義 Type？
using Price = double;
using Qty = uint32_t;
// ClOrdId 維持 uint32_t monotonic counter，見 ADR-0005。
// Wire 上是字串，序列化時用 std::to_string()；反序列化用 std::stoul()。
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