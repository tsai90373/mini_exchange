# Mini Exchange

A low-latency order matching engine written in C++, built to explore systems programming
concepts used in real-world trading infrastructure.

## Architecture

```
Client → TCP → Server (epoll) → Session → Exchange (Matching Engine)
                                    ↓
                               ExecReport → Client
```

- **Server**: Non-blocking I/O with Linux epoll, SessionFactory pattern to decouple
  network layer from business logic
- **Session**: Wire protocol parsing (length-prefix framing), separated wire types
  from domain objects
- **Exchange**: Price-time priority order book, supports New/Change/Cancel with
  fill reports

## Build & Run

Requires Linux (uses epoll). Tested with GCC on Docker.

```bash
make

# Terminal 1 — server
./exchange

# Terminal 2 — client
./exchange client
# Input format: <symbol> <type> <orderId> <price> <qty> <side>
# Example: AAPL N 1 100 10 B
```

## Roadmap

- [ ] Lock-free MPSC queue (LMAX Disruptor pattern)
- [ ] Latency histogram (HdrHistogram)
- [ ] Async logger (spdlog)
- [ ] Unit tests (GoogleTest)
- [ ] Market data publisher
- [ ] FIX protocol gateway
