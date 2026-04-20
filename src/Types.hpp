#pragma once
#include<cstdint>
#include<array>


using OrdId = uint32_t;
using SymbId = std::array<char, 6>;
using Price = uint64_t;
using Qty = uint32_t;

// inline Symbol makeSymbol(const char* s) {
//     Symbol sym;
//     for (int i = 0; i < strlen(s); i++) {
//         sym[i] = s[i];
//     } 
//     return sym;
// }

enum class Side {
    Buy,
    Sell
};

enum class Market {
    TSE = 'T',
    OTC = 'O',
};

class Symbol {
public:
    bool operator<(const Symbol& other) const {
        return id_ < other.id_;
    }
    SymbId id_;
    Market mkt_;
    Price UpLmt_;
    Price DnLmt_;
};

// Wire format for new order (client → server)
// Layout: [size(4)][ordId(4)][symbId(6)][side(1)][price(8)][qty(4)]
#pragma pack(push, 1)
struct OrderNewMsg {
    uint32_t size;   // bytes after this field
    uint32_t ordId;
    SymbId   symbId;
    char     side;   // 'B' or 'S'
    uint64_t price;
    uint32_t qty;
};
#pragma pack(pop)

#pragma pack(push, 1)
struct ExecReport {
    uint32_t size;
    uint32_t ordId;
    char     execType;  // 'N'ew 'F'ill 'P'artial 'R'ejected 'C'anceled
    uint64_t price;     // 成交價（N/R 的時候沒意義）
    uint32_t qty;       // 成交量
    uint32_t leaveQty;
    char     rejectReason[16];  // 拒絕原因
};
#pragma pack(pop)