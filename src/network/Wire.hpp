#pragma once
// Seperate Wire and Domain
#include<stdint.h>
#include"engine/Types.hpp"


enum class MsgType : uint16_t {
    OrderNew = 1,
    OrderChg = 2,
    OrderDel = 3,
};

#pragma pack(push, 1)
struct MsgHeader {
    uint32_t size;     // body 的 bytes 數
    MsgType msg_type;  // 1=OrderNew, 2=OrderChg, 3=OrderDel
};


// Client API: Should disable padding or define layout
struct OrderNewBody {
    SymbId   symb_id;
    char     side;   // 'B' or 'S'
    Price price;
    Qty qty;
};

struct OrderNewRequest {
    MsgHeader header;
    OrderNewBody body;
};

struct OrderChgBody {
    OrdId ord_id;
    Price price;
    Qty qty;
};

struct OrderChgRequest {
    MsgHeader header;
    OrderChgBody body;
};

#pragma pack(pop)

#pragma pack(push, 1)
struct ExecReport {
    uint32_t size;
    uint32_t ord_id;
    uint64_t price;     // 成交價（N/R 的時候沒意義）
    uint32_t qty;       // 成交量
    uint32_t leave_qty;
    char     side;
    char     exec_type;  // 'N'ew 'F'ill 'P'artial 'R'ejected 'C'anceled
    char     reject_reason[16];  // 拒絕原因
    uint64_t recv_ts;   // order 進系統的時間（從 Order 帶過來）
    uint64_t send_ts;   // report write() 前打；send_ts - recv_ts = e2e exchange latency
};
#pragma pack(pop)

using ReportList = std::vector<ExecReport>;