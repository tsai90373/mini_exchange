#include "PythonOrderAdapter.hpp"

#include <chrono>
#include <iostream>
#include <string>

#include <nlohmann/json.hpp>

#include "core/Order.hpp"
#include "core/Types.hpp"

using nlohmann::json;

namespace {

const char* ToString(Side s) {
    return s == Side::kSell ? "Sell" : "Buy";
}

const char* ToString(OpType o) {
    switch (o) {
        case OpType::New:         return "New";
        case OpType::Cancel:      return "Cancel";
        case OpType::UpdatePrice: return "UpdatePrice";
        case OpType::UpdateQty:   return "UpdateQty";
        case OpType::Unknown:     return "Unknown";
    }
    return "Unknown";
}

const char* ToString(PriceType p) {
    switch (p) {
        case PriceType::LMT:     return "LMT";
        case PriceType::MKT:     return "MKT";
        case PriceType::MKP:     return "MKP";
        case PriceType::Unknown: return "Unknown";
    }
    return "Unknown";
}

const char* ToString(OrderType o) {
    switch (o) {
        case OrderType::ROD:     return "ROD";
        case OrderType::IOC:     return "IOC";
        case OrderType::FOK:     return "FOK";
        case OrderType::Unknown: return "Unknown";
    }
    return "Unknown";
}

const char* ToString(OCType o) {
    switch (o) {
        case OCType::Auto:     return "Auto";
        case OCType::New:      return "New";
        case OCType::Cover:    return "Cover";
        case OCType::DayTrade: return "DayTrade";
        case OCType::Unknown:  return "Unknown";
    }
    return "Unknown";
}

// wire_format.md § 4：account 走小寫，對齊 Shioaji 序列化字串。
const char* ToString(Account a) {
    switch (a) {
        case Account::Stock:   return "stock";
        case Account::FutOpt:  return "futopt";
        case Account::Unknown: return "Unknown";
    }
    return "Unknown";
}

uint64_t NowNs() {
    using namespace std::chrono;
    return static_cast<uint64_t>(
        duration_cast<nanoseconds>(system_clock::now().time_since_epoch()).count());
}

}  // namespace

void PythonOrderAdapter::OnCommit(const OrderRaw& raw) {
    // Unknown 代表這筆 commit 不是 strategy request（例如 OnOrderAck / OnFill 寫的 raw）。
    // 跳過、不對外發送，避免把 broker callback 觸發的 commit 誤認成新請求重發。
    if (raw.op_type_ == OpType::Unknown) {
        ++skipped_;
        return;
    }

    try {
        json env = {
            {"msg_type", "order_request"},
            {"seq_num",  ++seq_num_},
            {"ts_ns",    NowNs()},
            {"data", {
                {"client_order_id", std::to_string(raw.id_)},
                {"code",       raw.symb_},
                {"action",     ToString(raw.side_)},
                {"price",      raw.pri_},
                {"quantity",   raw.leave_qty_},
                {"op_type",    ToString(raw.op_type_)},
                {"price_type", ToString(raw.price_type_)},
                {"order_type", ToString(raw.order_type_)},
                {"octype",     ToString(raw.octype_)},
                {"account",    ToString(raw.account_)},
            }}
        };
        send_(env.dump());
        ++msg_count_;
    } catch (const json::exception& e) {
        ++send_errs_;
        std::cerr << "[order-adapter] json error cid=" << raw.id_
                  << ": " << e.what() << '\n';
    } catch (const std::exception& e) {
        // catch-all for ZMQ throws inside send_() — socket disconnected / EAGAIN 等。
        ++send_errs_;
        std::cerr << "[order-adapter] send error cid=" << raw.id_
                  << ": " << e.what() << '\n';
    }
}
