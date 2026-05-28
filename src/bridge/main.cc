#include <iostream>
#include <string_view>

#include <zmq.hpp>

#include "adapter/PythonFeedAdapter.hpp"
#include "adapter/PythonOrderAdapter.hpp"
#include "core/Backend.hpp"
#include "core/Engine.hpp"

namespace {

constexpr const char* kInboundEndpoint  = "tcp://*:5555";
constexpr const char* kOutboundEndpoint = "tcp://localhost:5556";

}  // namespace

int main() {
    std::cout.setf(std::ios::unitbuf);

    zmq::context_t ctx{1};
    zmq::socket_t inbound{ctx, zmq::socket_type::pull};
    inbound.bind(kInboundEndpoint);
    zmq::socket_t outbound{ctx, zmq::socket_type::push};
    outbound.connect(kOutboundEndpoint);

    // Subscriber 必須在 Engine 之前構造，逆序解構時 Engine 先死、Subscriber 後死。
    // 見 Subscriber.hpp 的 lifetime contract。
    Backend backend;
    PythonOrderAdapter order_adapter([&outbound](std::string_view payload) {
        outbound.send(zmq::buffer(payload), zmq::send_flags::dontwait);
    });
    backend.Subscribe(&order_adapter);

    Engine engine(backend);
    PythonFeedAdapter feed_adapter(engine);

    std::cout << "[cpp] adapter ready, PULL " << kInboundEndpoint
              << " PUSH " << kOutboundEndpoint << '\n';

    while (!engine.ShouldStop()) {
        zmq::message_t msg;
        const auto rc = inbound.recv(msg, zmq::recv_flags::none);
        if (!rc) continue;
        feed_adapter.OnBytes(std::string_view{
            static_cast<const char*>(msg.data()), msg.size()});
    }

    std::cerr << "[cpp] engine requested stop, exiting"
              << " inbound_msgs=" << feed_adapter.MsgCount()
              << " outbound_msgs=" << order_adapter.MsgCount()
              << " outbound_skipped=" << order_adapter.SkippedCount()
              << " outbound_errs=" << order_adapter.SendErrorCount()
              << '\n';
    return 0;
}
