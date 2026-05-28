// Hand-rolled smoke test for PythonOrderAdapter.
//
// Drives Engine.SendNew / SendChg with a recording lambda, parses each captured
// JSON envelope, and asserts shape + counters per wire_format.md § 4.
//
// No ZMQ — adapter is exercised via SendFn injected at construction.

#include <cassert>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

#include <nlohmann/json.hpp>

#include "adapter/PythonOrderAdapter.hpp"
#include "core/Backend.hpp"
#include "core/Engine.hpp"
#include "core/Order.hpp"
#include "core/Types.hpp"

using nlohmann::json;

namespace {

Request MakeReq(Price pri, Qty qty) {
    Request r;
    r.symb       = "TXFE5";
    r.side       = Side::kBuy;
    r.pri        = pri;
    r.qty        = qty;
    r.price_type = PriceType::LMT;
    r.order_type = OrderType::ROD;
    r.octype     = OCType::Auto;
    r.account    = Account::FutOpt;
    return r;
}

// Verify the common envelope fields plus op_type for one captured frame.
void AssertEnvelope(const std::string& frame,
                    const char*        expected_op_type,
                    const std::string& expected_cid,
                    Price              expected_price,
                    Qty                expected_qty,
                    uint64_t           expected_seq) {
    json env = json::parse(frame);
    assert(env.at("msg_type").get<std::string>() == "order_request");
    assert(env.at("seq_num").get<uint64_t>()     == expected_seq);
    assert(env.at("ts_ns").get<uint64_t>()        > 0);

    const json& data = env.at("data");
    assert(data.at("client_order_id").get<std::string>() == expected_cid);
    assert(data.at("code").get<std::string>()            == "TXFE5");
    assert(data.at("action").get<std::string>()          == "Buy");
    assert(data.at("price").get<double>()                == expected_price);
    assert(data.at("quantity").get<uint32_t>()           == expected_qty);
    assert(data.at("op_type").get<std::string>()         == expected_op_type);
    assert(data.at("price_type").get<std::string>()      == "LMT");
    assert(data.at("order_type").get<std::string>()      == "ROD");
    assert(data.at("octype").get<std::string>()          == "Auto");
    assert(data.at("account").get<std::string>()         == "futopt");
}

}  // namespace

int main() {
    std::vector<std::string> sent;
    auto recorder = [&sent](std::string_view s) { sent.emplace_back(s); };

    // Subscriber lifetime contract: construct Backend → Subscriber → Engine.
    Backend            backend;
    PythonOrderAdapter adapter(recorder);
    backend.Subscribe(&adapter);
    Engine engine(backend);

    // --- 1. SendNew: cid=1, op_type=New ---
    assert(engine.SendNew(MakeReq(21500.0, 3)));
    AssertEnvelope(sent.at(0), "New", "1", 21500.0, 3, 1);

    // --- 2. Cancel cid=1: SendChg(1, 0, 0) → op_type=Cancel, quantity=0, price 沿用 ---
    assert(engine.SendChg(1, 0, 0));
    AssertEnvelope(sent.at(1), "Cancel", "1", 21500.0, 0, 2);

    // --- 3. SendNew (cid=2) → UpdateQty(cid=2, 1) ---
    assert(engine.SendNew(MakeReq(21600.0, 5)));
    AssertEnvelope(sent.at(2), "New", "2", 21600.0, 5, 3);
    assert(engine.SendChg(2, 1, 0));  // qty>0, new_pri==0 → UpdateQty
    AssertEnvelope(sent.at(3), "UpdateQty", "2", 21600.0, 1, 4);

    // --- 4. SendNew (cid=3) → UpdatePrice(cid=3, qty unchanged, new_pri=21505) ---
    assert(engine.SendNew(MakeReq(21500.0, 2)));
    AssertEnvelope(sent.at(4), "New", "3", 21500.0, 2, 5);
    assert(engine.SendChg(3, 2, 21505.0));  // qty>0 且 new_pri!=0 → UpdatePrice
    AssertEnvelope(sent.at(5), "UpdatePrice", "3", 21505.0, 2, 6);

    assert(adapter.MsgCount()       == 6);
    assert(adapter.SkippedCount()   == 0);
    assert(adapter.SendErrorCount() == 0);
    assert(sent.size()              == 6);

    // --- 5. Skipped path: simulate OnOrderAck/OnFill writing a raw with Unknown op_type.
    //        Backend.Begin → caller forgets to set op_type_ → Commit. Adapter must
    //        increment skipped_ and NOT push anything onto `sent`.
    //
    //        We access an Order through public Begin/Commit. Pick cid=3 which is
    //        still in engine.orders_ (we haven't filled or cancelled it cleanly).
    //        Real OnOrderAck would look up the Order via cid → engine.orders_; here
    //        we synthesise the same effect by reaching through an already-committed
    //        OrderRaw's GetOrder() pointer.
    Order* order_3 = sent.empty() ? nullptr : nullptr;  // satisfy unused-warn
    (void)order_3;
    // Easier: re-use the SendNew(cid=4) pathway then mutate via Begin without setting op_type.
    assert(engine.SendNew(MakeReq(21700.0, 1)));  // cid=4, seq=7
    AssertEnvelope(sent.at(6), "New", "4", 21700.0, 1, 7);

    // Now simulate a broker-callback-style commit: grab the order via its committed
    // raw, Begin a new raw (which resets op_type_ to Unknown), and Commit.
    const OrderRaw* head_raw_cid4 = backend.GetBySno(7);
    assert(head_raw_cid4 != nullptr);
    Order* order_cid4 = head_raw_cid4->GetOrder();
    assert(order_cid4 != nullptr);

    OrderRaw* synth = backend.Begin(*order_cid4);
    // intentionally do NOT set synth->op_type_ — Backend.Begin already reset it to Unknown
    backend.Commit(synth);

    assert(adapter.MsgCount()       == 7);  // unchanged from the skipped commit
    assert(adapter.SkippedCount()   == 1);
    assert(adapter.SendErrorCount() == 0);
    assert(sent.size()              == 7);  // no new frame for the skipped raw

    std::cout << "[order-smoke] PASS"
              << " msg_count="   << adapter.MsgCount()
              << " skipped="     << adapter.SkippedCount()
              << " send_errs="   << adapter.SendErrorCount()
              << " frames="      << sent.size() << '\n';
    return 0;
}
