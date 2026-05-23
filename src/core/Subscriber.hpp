#include <string_view>


class OrderRaw;
// Subscriber — observer interface for Backend commit events.
//
// The Backend invokes OnCommit on every registered Subscriber after each
// commit completes. Concrete implementations include ZmqOutboundSubscriber
// (publishes outbound) and LogWriterSubscriber (writes to log).
//
// Lifetime contract: a Subscriber must outlive the Engine. The Engine may
// trigger a final commit during its own destruction, which still calls
// OnCommit — destroying a Subscriber first would be a use-after-free.
// Construct in the order: Backend -> Subscriber -> Engine. 
class Subscriber {
public:
    virtual ~Subscriber() = default;
    virtual void OnCommit(const OrderRaw&) = 0;
    virtual std::string_view Name() const = 0;
};