// Hand-rolled smoke test for PythonFeedAdapter.
//
// Feeds the 5 happy-path JSON fixtures from docs/architecture/wire_format.md
// plus 2 sad-path fixtures, then asserts adapter counters and Engine stop flag.
//
// No ZMQ — adapter is exercised directly via OnBytes(string_view).

#include <cassert>
#include <iostream>
#include <string>

#include "adapter/PythonFeedAdapter.hpp"
#include "core/Backend.hpp"
#include "core/Engine.hpp"

// ---------- fixtures (verbatim from wire_format.md) ----------

constexpr const char* kTickJson = R"({
  "msg_type": "tick",
  "seq_num": 1,
  "ts_ns": 1715769600123456789,
  "data": {
    "code": "TXFE5",
    "exchange": "TAIFEX",
    "exchange_ts_ns": 1715769600100000000,
    "close": 21500.0,
    "open": 21480.0,
    "high": 21520.0,
    "low": 21470.0,
    "avg_price": 21490.3,
    "volume": 3,
    "total_volume": 12345,
    "amount": 1290000.0,
    "total_amount": 5310750000.0,
    "tick_type": 1,
    "chg_type": 2,
    "price_chg": 25.0,
    "pct_chg": 0.1163,
    "bid_side_total_vol": 6500,
    "ask_side_total_vol": 5800,
    "underlying_price": 21495.5,
    "simtrade": 0
  }
})";

constexpr const char* kBidAskJson = R"({
  "msg_type": "bidask",
  "seq_num": 2,
  "ts_ns": 1715769600123456789,
  "data": {
    "code": "TXFE5",
    "exchange_ts_ns": 1715769600100000000,
    "bid_price":  [21499.0, 21498.0, 21497.0, 21496.0, 21495.0],
    "bid_volume": [12, 25, 18, 30, 14],
    "ask_price":  [21500.0, 21501.0, 21502.0, 21503.0, 21504.0],
    "ask_volume": [10, 22, 17, 28, 19],
    "bid_total_vol": 99,
    "ask_total_vol": 96,
    "underlying_price": 21495.5,
    "simtrade": 0
  }
})";

constexpr const char* kOrderAckJson = R"({
  "msg_type": "order_ack",
  "seq_num": 3,
  "ts_ns": 1715769600300000000,
  "data": {
    "client_order_id": "tickengine-0001",
    "broker_order_id": "de616839",
    "op_type": "New",
    "op_code": "00",
    "op_msg": "",
    "accepted": true,
    "market_type": "Day"
  }
})";

constexpr const char* kFillJson = R"({
  "msg_type": "fill",
  "seq_num": 4,
  "ts_ns": 1715769600400000000,
  "data": {
    "client_order_id": "tickengine-0001",
    "broker_order_id": "de616839",
    "exchange_seq": "987654",
    "code": "TXFE5",
    "action": "Buy",
    "price": 21500.0,
    "quantity": 1,
    "exchange_ts_ns": 1715769600350000000,
    "security_type": "FUT",
    "market_type": "Day"
  }
})";

// Non-Disconnected event — should NOT trigger ShouldStop().
constexpr const char* kHeartbeatJson = R"({
  "msg_type": "event",
  "seq_num": 5,
  "ts_ns": 1715769600500000000,
  "data": {
    "source": "quote",
    "event_code": 0,
    "info": "Heartbeat"
  }
})";

// Disconnected — MUST trigger ShouldStop().
constexpr const char* kDisconnectJson = R"({
  "msg_type": "event",
  "seq_num": 6,
  "ts_ns": 1715769600600000000,
  "data": {
    "source": "quote",
    "event_code": 2,
    "info": "Disconnected"
  }
})";

constexpr const char* kBrokenJson = R"({"msg_type": "tick", "seq_num":)";

constexpr const char* kUnknownTypeJson = R"({
  "msg_type": "futuristic_msg_that_doesnt_exist_yet",
  "seq_num": 999,
  "ts_ns": 1715769600700000000,
  "data": {}
})";

// ---------- driver ----------

int main() {
    Backend           backend;
    Engine            engine(backend);
    PythonFeedAdapter adapter(engine);

    // --- Happy path: 5 valid msg_types ---
    adapter.OnBytes(kTickJson);
    adapter.OnBytes(kBidAskJson);
    adapter.OnBytes(kOrderAckJson);
    adapter.OnBytes(kFillJson);
    adapter.OnBytes(kHeartbeatJson);

    assert(adapter.MsgCount()        == 5);
    assert(adapter.ParseErrorCount() == 0);
    assert(!engine.ShouldStop());  // Heartbeat does NOT stop

    // --- Sad path: broken JSON + unknown msg_type ---
    adapter.OnBytes(kBrokenJson);
    adapter.OnBytes(kUnknownTypeJson);

    assert(adapter.MsgCount()        == 5);   // unchanged
    assert(adapter.ParseErrorCount() == 2);

    // --- Disconnect: must flip ShouldStop() ---
    adapter.OnBytes(kDisconnectJson);

    assert(adapter.MsgCount()        == 6);
    assert(adapter.ParseErrorCount() == 2);
    assert(engine.ShouldStop());

    std::cout << "[smoke] PASS"
              << " msg_count="    << adapter.MsgCount()
              << " parse_errs="   << adapter.ParseErrorCount()
              << " should_stop="  << engine.ShouldStop() << '\n';
    return 0;
}
