#include <iostream>
#include <string_view>

#include <zmq.hpp>

#include "adapter/PythonFeedAdapter.hpp"
#include "core/Backend.hpp"
#include "core/Engine.hpp"

namespace {

constexpr const char* kInboundEndpoint = "tcp://*:5555";
// Outbound (C++ → Py order_request) deferred to the Backend Subscriber rework;
// see plan §Deferred. Endpoint kept here for reference, socket not opened.
// constexpr const char* kOutboundEndpoint = "tcp://localhost:5556";

}  // namespace

int main() {
    std::cout.setf(std::ios::unitbuf);

    zmq::context_t ctx{1};
    zmq::socket_t inbound{ctx, zmq::socket_type::pull};
    inbound.bind(kInboundEndpoint);

    Backend backend;
    Engine engine(backend);
    PythonFeedAdapter adapter(engine);

    std::cout << "[cpp] adapter ready, PULL " << kInboundEndpoint << '\n';

    while (!engine.ShouldStop()) {
        zmq::message_t msg;
        const auto rc = inbound.recv(msg, zmq::recv_flags::none);
        if (!rc) continue;
        adapter.OnBytes(std::string_view{
            static_cast<const char*>(msg.data()), msg.size()});
    }

    std::cerr << "[cpp] engine requested stop, exiting\n";
    return 0;
}
