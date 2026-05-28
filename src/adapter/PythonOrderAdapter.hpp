#pragma once
#include <cstdint>
#include <functional>
#include <string_view>

#include "core/Subscriber.hpp"

class OrderRaw;

// Owns the outbound boundary: OrderRaw commits → wire JSON → ZMQ bytes.
//
// V1 scope: 1 msg_type from docs/architecture/wire_format.md (order_request).
// Dispatch by OrderRaw::op_type_; Unknown means the commit was NOT a strategy
// request (e.g. OnOrderAck / OnFill processing wrote it) — skip those silently
// instead of emitting a duplicate order_request.
//
// Subscriber inheritance is the impl mechanism for Backend → adapter fan-out;
// the user-visible identity is still "adapter" — symmetric with
// PythonFeedAdapter. Backend doesn't know any concrete subscriber.
//
// Construction: takes a SendFn callable so production wires ZMQ socket.send
// and tests wire a recording lambda. Adapter never touches zmq:: types
// directly — single responsibility, easy to unit-test without ZMQ.
class PythonOrderAdapter : public Subscriber {
public:
    using SendFn = std::function<void(std::string_view)>;

    explicit PythonOrderAdapter(SendFn send) : send_(std::move(send)) {}

    // Subscriber interface — Backend calls this on every Commit.
    void OnCommit(const OrderRaw& raw) override;
    std::string_view Name() const override { return "PythonOrderAdapter"; }

    // Counters symmetric to PythonFeedAdapter's MsgCount / ParseErrorCount.
    uint64_t MsgCount()       const { return msg_count_; }
    uint64_t SkippedCount()   const { return skipped_; }
    uint64_t SendErrorCount() const { return send_errs_; }

private:
    SendFn   send_;
    uint64_t seq_num_   = 0;  // outbound monotonic, V1 sender-side only
    uint64_t msg_count_ = 0;
    uint64_t skipped_   = 0;
    uint64_t send_errs_ = 0;
};
