#pragma once
// Seperate Wire and Domain
#include<stdint.h>
#include"Types.hpp"

// Client API: Should disable padding or define layout
#pragma pack(push, 1)
struct OrderNewMsg {
    uint32_t size;   
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
    uint64_t price;     // 成交價（N/R 的時候沒意義）
    uint32_t qty;       // 成交量
    uint32_t leaveQty;
    char     side; 
    char     execType;  // 'N'ew 'F'ill 'P'artial 'R'ejected 'C'anceled
    char     rejectReason[16];  // 拒絕原因
};
#pragma pack(pop)

using ReportList = std::vector<ExecReport>;