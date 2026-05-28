#pragma once
#include <array>
#include <cstdint>
#include <string>
#include "Types.hpp"

// Wire-shaped C++ mirror of the 5 inbound msg_types defined in
// docs/architecture/wire_format.md. PythonFeedAdapter parses JSON into these,
// then hands them to Engine::On*.
//
// Lives in src/core/ (not src/adapter/) because Engine.hpp's method signatures
// reference these types — placing them under adapter would force core to depend
// on adapter, violating hexagonal layering.

struct InboundEnvelope {
    uint64_t seq_num = 0;   // sender-monotonic; V1 log-only
    uint64_t ts_ns   = 0;   // sender's time.time_ns(), not exchange time
};

// ---------- § 2 tick ----------

// Venue. Wire is a string ("TSE" / "OTC" / "TAIFEX") to stay
// readable & future-proof; adapter does the string→enum lookup.
enum class Exchange : uint8_t {
    Unknown = 0,
    TSE,        // 台灣證交所
    OTC,        // 櫃買中心
    TAIFEX,     // 期交所
};

// Aggressor categorisation (內外盤). Wire codes 0/1/2 preserved so adapter
// translation is a one-step cast; engine code reads symbolic names instead
// of magic uint8 literals.
enum class TickType : uint8_t { Unknown = 0, AskHit = 1, BidHit = 2 };

// 試撮 flag. Wire 0 = real, 1 = test.
enum class Simtrade : uint8_t { Real = 0, Test = 1 };

// 漲跌註記. Shioaji `diff_type` 1~5; Unknown=0 added for missing/garbage
// resilience.
enum class DiffType : uint8_t {
    Unknown   = 0,
    LimitUp   = 1,
    Up        = 2,
    Flat      = 3,
    Down      = 4,
    LimitDown = 5,
};

// V1 取捨：price / amount 欄位用 double。Shioaji 原生用 Decimal；amount /
// total_amount 之後若要做 broker 對帳（engine 自累加 vs broker total），
// IEEE 754 累加 drift 會咬到，屆時改 int64 cents。
struct Tick {
    InboundEnvelope env;
    std::string code;                    // 商品代碼
    Exchange exchange           = Exchange::Unknown;
    uint64_t exchange_ts_ns     = 0;     // µs precision underlying
    double   deal               = 0;
    double   open               = 0;
    double   high               = 0;
    double   low                = 0;
    double   avg_price          = 0;
    uint32_t volume             = 0;
    uint64_t total_volume       = 0;
    double   amount             = 0;     // 單筆成交額 NTD
    double   total_amount       = 0;     // 當日累計成交額 NTD
    TickType tick_type          = TickType::Unknown;
    DiffType chg_type           = DiffType::Unknown;
    double   price_chg          = 0;     // vs 昨收
    double   pct_chg            = 0;     // 漲跌幅 %
    uint64_t bid_side_total_vol = 0;
    uint64_t ask_side_total_vol = 0;
    double   underlying_price   = 0;
    Simtrade simtrade           = Simtrade::Real;
};

// ---------- § 3 bidask ----------
struct BidAsk {
    InboundEnvelope env;
    std::string code;
    uint64_t exchange_ts_ns = 0;
    std::array<double, 5>   bid_price{};
    std::array<uint64_t, 5> bid_volume{};
    std::array<double, 5>   ask_price{};
    std::array<uint64_t, 5> ask_volume{};
    uint64_t bid_total_vol  = 0;
    uint64_t ask_total_vol  = 0;
    double   underlying_price = 0;
    Simtrade simtrade       = Simtrade::Real;
};

// ---------- § 5 order_ack ----------
// OpType 已搬到 Types.hpp（outbound / inbound 共用）。
enum class MarketType : uint8_t { Day, Night, Unknown };

struct OrderAckEvent {
    InboundEnvelope env;
    // Wire 是字串；ClOrdId 內部維持 uint32（ADR-0005）。Engine 端 lookup 時做
    // std::stoul 轉回 uint32。
    std::string client_order_id;
    // broker_order_id deliberately absent — ADR-0003 forbids it on the wire.
    // wire_format.md § 5 still lists it (flagged stale in CONTEXT.md);
    // the adapter parse-and-ignores if present.
    OpType      op_type     = OpType::Unknown;
    std::string op_code;                       // "00" = success
    std::string op_msg;
    bool        accepted    = false;
    MarketType  market_type = MarketType::Unknown;
};

// ---------- § 6 fill ----------
enum class SecurityType : uint8_t { FUT, OPT, Unknown };

struct FillEvent {
    InboundEnvelope env;
    std::string client_order_id;
    // broker_order_id absent — same reasoning as order_ack.
    std::string  exchange_seq;
    std::string  code;
    Side         action          = Side::kBuy;
    double       price           = 0;
    uint32_t     quantity        = 0;
    uint64_t     exchange_ts_ns  = 0;          // ms precision underlying
    SecurityType security_type   = SecurityType::Unknown;
    MarketType   market_type     = MarketType::Unknown;
};

// ---------- § 7 event ----------
enum class EventSource : uint8_t { Quote, Order, Unknown };
enum class EventCode : uint8_t {
    Heartbeat          = 0,
    Connected          = 1,
    Disconnected       = 2,
    Reconnecting       = 3,
    Reconnected        = 4,
    SubscribeSuccess   = 16,
    UnsubscribeSuccess = 17,
    Unknown            = 255,
};

struct ConnEvent {
    InboundEnvelope env;
    EventSource source = EventSource::Unknown;
    EventCode   code   = EventCode::Unknown;
    std::string info;
};
