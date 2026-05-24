#pragma once
#include <cstdint>
#include <string_view>

#include <nlohmann/json_fwd.hpp>

class Engine;

// Owns the inbound boundary: ZMQ bytes → engine event objects → Engine::On*.
//
// V1 scope: 5 msg_types from docs/architecture/wire_format.md
// (tick, bidask, order_ack, fill, event). Single inbound port; dispatch by
// msg_type string.
//
// Per-message JSON parse errors are skip+log (ADR-0001's fail-fast applies to
// ZMQ connect / process-level only; per-frame defects don't kill the process).
// Disconnected event flips Engine::ShouldStop() — main loop exits.
class PythonFeedAdapter {
public:
    explicit PythonFeedAdapter(Engine& engine) : engine_(engine) {}

    // Main entry from bridge/main.cc — raw ZMQ frame bytes.
    void OnBytes(std::string_view frame);

    // Test seam: caller has already parsed the envelope.
    void OnEnvelope(const nlohmann::json& envelope);

    uint64_t MsgCount()        const { return msg_count_; }
    uint64_t ParseErrorCount() const { return parse_errs_; }

private:
    void HandleTick    (uint64_t seq, uint64_t ts_ns, const nlohmann::json& data);
    void HandleBidAsk  (uint64_t seq, uint64_t ts_ns, const nlohmann::json& data);
    void HandleOrderAck(uint64_t seq, uint64_t ts_ns, const nlohmann::json& data);
    void HandleFill    (uint64_t seq, uint64_t ts_ns, const nlohmann::json& data);
    void HandleEvent   (uint64_t seq, uint64_t ts_ns, const nlohmann::json& data);

    Engine&  engine_;
    uint64_t msg_count_  = 0;
    uint64_t parse_errs_ = 0;
};
