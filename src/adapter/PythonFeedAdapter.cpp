#include "PythonFeedAdapter.hpp"

#include <iostream>
#include <string>

#include <nlohmann/json.hpp>

#include "core/Engine.hpp"
#include "core/InboundEvents.hpp"

using nlohmann::json;

namespace {

OpType ParseOpType(const std::string& s) {
    if (s == "New")         return OpType::New;
    if (s == "Cancel")      return OpType::Cancel;
    if (s == "UpdatePrice") return OpType::UpdatePrice;
    if (s == "UpdateQty")   return OpType::UpdateQty;
    return OpType::Unknown;
}

MarketType ParseMarketType(const std::string& s) {
    if (s == "Day")   return MarketType::Day;
    if (s == "Night") return MarketType::Night;
    return MarketType::Unknown;
}

SecurityType ParseSecurityType(const std::string& s) {
    if (s == "FUT") return SecurityType::FUT;
    if (s == "OPT") return SecurityType::OPT;
    return SecurityType::Unknown;
}

EventSource ParseEventSource(const std::string& s) {
    if (s == "quote") return EventSource::Quote;
    if (s == "order") return EventSource::Order;
    return EventSource::Unknown;
}

EventCode ParseEventCode(uint8_t code) {
    switch (code) {
        case 0:  return EventCode::Heartbeat;
        case 1:  return EventCode::Connected;
        case 2:  return EventCode::Disconnected;
        case 3:  return EventCode::Reconnecting;
        case 4:  return EventCode::Reconnected;
        case 16: return EventCode::SubscribeSuccess;
        case 17: return EventCode::UnsubscribeSuccess;
        default: return EventCode::Unknown;
    }
}

TickType ParseTickType(uint8_t v) {
    switch (v) {
        case 1:  return TickType::AskHit;
        case 2:  return TickType::BidHit;
        default: return TickType::Unknown;
    }
}

// Wire spec says 0/1; treat anything non-1 as Real (matches the old uint8_t
// default-0 = Real behaviour).
Simtrade ParseSimtrade(uint8_t v) {
    return v == 1 ? Simtrade::Test : Simtrade::Real;
}

Exchange ParseExchange(const std::string& s) {
    if (s == "TSE")    return Exchange::TSE;
    if (s == "OTC")    return Exchange::OTC;
    if (s == "TAIFEX") return Exchange::TAIFEX;
    return Exchange::Unknown;
}

DiffType ParseDiffType(uint8_t v) {
    switch (v) {
        case 1:  return DiffType::LimitUp;
        case 2:  return DiffType::Up;
        case 3:  return DiffType::Flat;
        case 4:  return DiffType::Down;
        case 5:  return DiffType::LimitDown;
        default: return DiffType::Unknown;
    }
}

Side ParseSide(const std::string& s) {
    // wire_format.md § 6: "Buy" / "Sell". Default to kBuy on garbage
    // — parser-level type check upstream catches the missing-field case.
    return s == "Sell" ? Side::kSell : Side::kBuy;
}

// Copy a JSON array of 5 doubles into a std::array. Silently zero-fills shorter
// arrays — wire_format.md § 3 mandates exactly 5 levels, so a short array is a
// publisher bug we tolerate without crashing.
template <typename T, size_t N>
void CopyFixedArray(const json& src, std::array<T, N>& dst) {
    if (!src.is_array()) return;
    const size_t n = std::min(src.size(), N);
    for (size_t i = 0; i < n; ++i) dst[i] = src[i].get<T>();
}

}  // namespace

void PythonFeedAdapter::OnBytes(std::string_view frame) {
    json env;
    try {
        env = json::parse(frame);
    } catch (const json::parse_error& e) {
        ++parse_errs_;
        std::cerr << "[adapter] json parse error: " << e.what() << '\n';
        return;
    }
    OnEnvelope(env);
}

void PythonFeedAdapter::OnEnvelope(const json& env) {
    std::string msg_type;
    uint64_t    seq_num = 0;
    uint64_t    ts_ns   = 0;
    try {
        msg_type = env.at("msg_type").get<std::string>();
        seq_num  = env.value("seq_num", uint64_t{0});
        ts_ns    = env.value("ts_ns",   uint64_t{0});
    } catch (const json::exception& e) {
        ++parse_errs_;
        std::cerr << "[adapter] envelope error: " << e.what() << '\n';
        return;
    }

    const json& data = env.value("data", json::object());

    try {
        if      (msg_type == "tick")      HandleTick    (seq_num, ts_ns, data);
        else if (msg_type == "bidask")    HandleBidAsk  (seq_num, ts_ns, data);
        else if (msg_type == "order_ack") HandleOrderAck(seq_num, ts_ns, data);
        else if (msg_type == "fill")      HandleFill    (seq_num, ts_ns, data);
        else if (msg_type == "event")     HandleEvent   (seq_num, ts_ns, data);
        else {
            // Forward-compat: Python may emit a new msg_type before C++ rebuild.
            // Hard-fail would lock deployment order; log + skip is friendlier.
            ++parse_errs_;
            std::cerr << "[adapter] unknown msg_type=" << msg_type
                      << " seq=" << seq_num << '\n';
            return;
        }
        ++msg_count_;
    } catch (const json::exception& e) {
        ++parse_errs_;
        std::cerr << "[adapter] handler error msg_type=" << msg_type
                  << " seq=" << seq_num
                  << ": " << e.what() << '\n';
    }
}

void PythonFeedAdapter::HandleTick(uint64_t seq, uint64_t ts_ns, const json& data) {
    Tick ev;
    ev.env.seq_num           = seq;
    ev.env.ts_ns             = ts_ns;
    ev.code                  = data.value("code",                std::string{});
    ev.exchange              = ParseExchange(data.value("exchange", std::string{}));
    ev.exchange_ts_ns        = data.value("exchange_ts_ns",      uint64_t{0});
    ev.deal                  = data.value("close",               0.0);
    ev.open                  = data.value("open",                0.0);
    ev.high                  = data.value("high",                0.0);
    ev.low                   = data.value("low",                 0.0);
    ev.avg_price             = data.value("avg_price",           0.0);
    ev.volume                = data.value("volume",              uint32_t{0});
    ev.total_volume          = data.value("total_volume",        uint64_t{0});
    ev.amount                = data.value("amount",              0.0);
    ev.total_amount          = data.value("total_amount",        0.0);
    ev.tick_type             = ParseTickType(data.value("tick_type", uint8_t{0}));
    ev.chg_type              = ParseDiffType(data.value("chg_type",  uint8_t{0}));
    ev.price_chg             = data.value("price_chg",           0.0);
    ev.pct_chg               = data.value("pct_chg",             0.0);
    ev.bid_side_total_vol    = data.value("bid_side_total_vol",  uint64_t{0});
    ev.ask_side_total_vol    = data.value("ask_side_total_vol",  uint64_t{0});
    ev.underlying_price      = data.value("underlying_price",    0.0);
    ev.simtrade              = ParseSimtrade(data.value("simtrade", uint8_t{0}));
    engine_.OnTick(ev);
}

void PythonFeedAdapter::HandleBidAsk(uint64_t seq, uint64_t ts_ns, const json& data) {
    BidAsk ev;
    ev.env.seq_num         = seq;
    ev.env.ts_ns           = ts_ns;
    ev.code                = data.value("code",             std::string{});
    ev.exchange_ts_ns      = data.value("exchange_ts_ns",   uint64_t{0});
    CopyFixedArray(data.value("bid_price",  json::array()), ev.bid_price);
    CopyFixedArray(data.value("bid_volume", json::array()), ev.bid_volume);
    CopyFixedArray(data.value("ask_price",  json::array()), ev.ask_price);
    CopyFixedArray(data.value("ask_volume", json::array()), ev.ask_volume);
    ev.bid_total_vol       = data.value("bid_total_vol",   uint64_t{0});
    ev.ask_total_vol       = data.value("ask_total_vol",   uint64_t{0});
    ev.underlying_price    = data.value("underlying_price", 0.0);
    ev.simtrade            = ParseSimtrade(data.value("simtrade", uint8_t{0}));
    engine_.OnBidAsk(ev);
}

void PythonFeedAdapter::HandleOrderAck(uint64_t seq, uint64_t ts_ns, const json& data) {
    OrderAckEvent ev;
    ev.env.seq_num     = seq;
    ev.env.ts_ns       = ts_ns;
    ev.client_order_id = data.value("client_order_id", std::string{});
    ev.op_type         = ParseOpType    (data.value("op_type",     std::string{}));
    ev.op_code         = data.value("op_code", std::string{});
    ev.op_msg          = data.value("op_msg",  std::string{});
    ev.accepted        = data.value("accepted", false);
    ev.market_type     = ParseMarketType(data.value("market_type", std::string{}));
    engine_.OnOrderAck(ev);
}

void PythonFeedAdapter::HandleFill(uint64_t seq, uint64_t ts_ns, const json& data) {
    FillEvent ev;
    ev.env.seq_num     = seq;
    ev.env.ts_ns       = ts_ns;
    ev.client_order_id = data.value("client_order_id", std::string{});
    ev.exchange_seq    = data.value("exchange_seq",    std::string{});
    ev.code            = data.value("code",            std::string{});
    ev.action          = ParseSide        (data.value("action",        std::string{}));
    ev.price           = data.value("price",           0.0);
    ev.quantity        = data.value("quantity",        uint32_t{0});
    ev.exchange_ts_ns  = data.value("exchange_ts_ns",  uint64_t{0});
    ev.security_type   = ParseSecurityType(data.value("security_type", std::string{}));
    ev.market_type     = ParseMarketType  (data.value("market_type",   std::string{}));
    engine_.OnFill(ev);
}

void PythonFeedAdapter::HandleEvent(uint64_t seq, uint64_t ts_ns, const json& data) {
    ConnEvent ev;
    ev.env.seq_num = seq;
    ev.env.ts_ns   = ts_ns;
    ev.source      = ParseEventSource(data.value("source", std::string{}));
    ev.code        = ParseEventCode  (data.value("event_code", uint8_t{255}));
    ev.info        = data.value("info", std::string{});
    engine_.OnConnEvent(ev);
}
