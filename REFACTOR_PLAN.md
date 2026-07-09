# TickEngine Refactor Plan — nautilus-informed, 2026-07

Supersedes `plan.md` (rough sketch). Goal restated: after this refactor, writing a new
strategy means implementing `IStrategy` and editing a risk config — everything else
(order lifecycle, position tracking, risk gating, logging, recovery) is engine-provided.

**What stays:** two-process ZMQ IPC (ADR-0001), `Engine`/`Backend` split (ADR-0004),
`client_order_id` identity (ADR-0005), `Order`/`OrderRaw` append-only lifecycle,
`PythonFeedAdapter` / `PythonOrderAdapter`. The Backend + Subscriber spine is already
the nautilus MessageBus/Cache publication pattern — we extend it, we don't replace it.

**What's borrowed from nautilus (mapped):**

| nautilus | TickEngine equivalent (this plan) |
|---|---|
| `cache/` + `portfolio/` | `Portfolio` + `Position` (Phase 2) |
| `trading/strategy.pyx` callbacks | `IStrategy` event methods (Phase 3) |
| `risk/engine.pyx` (TradingState, pre-trade checks, throttler) | `RiskGate` (Phase 4) |
| `cancel_all_orders` / `close_all_positions` | `Engine` safety commands (Phase 5) |
| `live/reconciliation.py` | boot-time snapshot reconciliation (Phase 6) |
| backtest/live symmetry | replay discipline: strategies touch only Engine interfaces (rule, all phases) |

Division of labor per the workflow guardrail: **hot path (order state machine,
position math, risk checks) — Ryan writes, AI reviews. Boilerplate (log writer,
config parsing, control socket, wire plumbing, tests scaffolding) — AI writes.**

---

## Phase 0 — Finish the order state machine (prerequisite, not refactor)

`Engine::OnOrderAck` / `OnFill` / `OnConnEvent` are currently log-only TODOs.
Nothing downstream (positions, callbacks) can exist until inbound events actually
mutate order state.

**Changes**
- `src/core/Engine.cpp`:
  - `OnOrderAck`: look up `orders_[ev.client_order_id]` → `backend_.Begin(order)` →
    set `ord_st_` (`NEWASK` on new-ack, `CANCELLED` on cancel-ack, `FAILED` on reject),
    update `leave_qty_` → `Commit`. `op_type_` stays `Unknown` (broker-initiated).
  - `OnFill`: `Begin` → `filled_qty_ += ev.qty`, `leave_qty_ -= ev.qty`,
    `ord_st_ = leave_qty_ == 0 ? FULL_FILLED : PARTIAL_FILLED` → `Commit`.
  - Unknown `client_order_id` on ack/fill: V1 fail-fast (log + stop) until Phase 6.
- `src/core/Types.hpp`: no change yet (`OrdSt` already covers these states).

**Done when:** unit test drives New → ack → partial fill → fill and `Order::Tail()`
shows correct `ord_st_ / leave_qty_ / filled_qty_` at each step; paper-trade round
trip against Shioaji simulation shows the same chain in the log.

**Effort:** 1–2 evenings. Hot path → Ryan writes.

---

## Phase 1 — Log-writer Subscriber (cheap, do early: it debugs every later phase)

**Changes**
- New `src/backend/FileLogSubscriber.{hpp,cpp}`: implements `Subscriber`; on each
  committed `OrderRaw` appends one JSON line
  (`rxsno, ts, client_order_id, op_type, ord_st, symb, side, pri, ini/leave/filled_qty`)
  to a daily file `logs/orderraw-YYYYMMDD.jsonl`. Timestamp via existing
  `src/tools/Timestamp.hpp`.
- Registration in the bridge main: `backend.Subscribe(&log_writer)` alongside the
  ZMQ outbound subscriber.

**Done when:** a paper-trade session produces a `.jsonl` whose lines replay the full
OrderRaw chain; `wc -l` matches `Backend::HistorySize()`.

**Effort:** 1 evening. Boilerplate → AI writes.

---## Phase 2 — Position & Portfolio (the load-bearing new component)

**New files**
- `src/core/Position.hpp`:
  ```cpp
  struct Position {           // per Symbol, netted
      Symbol  symb;
      int32_t net_qty      = 0;   // signed: + long / - short
      double  avg_price    = 0;   // of the open side
      int64_t realized_pnl = 0;   // in TWD, using per-symbol point multiplier
  };
  ```
- `src/core/Portfolio.{hpp,cpp}`: owns `unordered_map<Symbol, Position>`;
  `ApplyFill(const OrderRaw&, Qty fill_qty, Price fill_pri)` does the netting math
  (reduce-then-flip on crossing zero); read API `GetPosition(symb)`,
  `NetQty(symb)`, `TotalAbsExposure()`, `RealizedPnl()`.

**Changes**
- `Engine`: owns a `Portfolio portfolio_`; `OnFill` (Phase 0 code) additionally calls
  `portfolio_.ApplyFill(...)`. Strategies read it via the Engine facade (Phase 3).
- Retire the `total_profit_` int in `Engine` — replaced by `portfolio_.RealizedPnl()`.
- Contract multipliers (TXF 200/pt, MXF 50/pt, TMF 10/pt) live in a small
  `src/core/InstrumentInfo.hpp` table — also used by Phase 4 notional checks.

**Deliberately out of scope:** unrealized P&L / mark-to-market (needs a tick→portfolio
feed; add later behind `Portfolio::MarkPrice(symb, pri)` if a risk rule demands it),
multi-account, options greeks.

**Done when:** unit tests cover long→add→reduce→flip→flat fill sequences with exact
`net_qty / avg_price / realized_pnl`; a paper session's end state matches Shioaji's
`list_positions()` by hand-check.

**Effort:** 2–3 evenings. Netting math is hot path → Ryan writes (60-sec whiteboard
the flip case first); table/scaffolding AI writes.

**Docs:** add `Position`, `Portfolio` to CONTEXT.md (netted-by-symbol is the one
surprising choice — a CONTEXT.md entry, below ADR threshold).

---

## Phase 3 — IStrategy + event routing (replaces the `IStrategy` TODO)

**New files**
- `src/core/IStrategy.hpp`:
  ```cpp
  class IStrategy {
  public:
      virtual ~IStrategy() = default;
      // lifecycle
      virtual void OnStart() {}
      virtual void OnStop()  {}
      // market data
      virtual void OnTick(const Tick&)     {}
      virtual void OnBidAsk(const BidAsk&) {}
      // order events — every one carries the just-committed OrderRaw
      virtual void OnOrderAccepted(const OrderRaw&) {}
      virtual void OnOrderRejected(const OrderRaw&) {}   // broker said no
      virtual void OnOrderDenied  (const OrderRaw&, const std::string& reason) {}  // RiskGate said no
      virtual void OnOrderCanceled(const OrderRaw&) {}
      virtual void OnOrderFilled  (const OrderRaw&) {}   // partial or full
      // identity
      virtual const char* Name() const = 0;
  };
  ```
  (nautilus has ~15 order callbacks; these six are the ones a TAIFEX retail flow can
  actually trigger. Add more only when a real strategy needs them.)
- `src/core/StrategyId.hpp`: `using StrategyId = uint8_t;` index into the registry.

**Changes**
- `Order` gains `StrategyId owner_` (set at `SendNew`; Chg inherits). This upgrades
  CONTEXT.md's "attribution lives in Backend log entries" — routing needs it on the
  Order. Update CONTEXT.md accordingly.
- `Engine::SendNew(const Request&, IStrategy* owner)` — signature change.
- `Engine::OnTick/OnBidAsk`: dispatch to strategies subscribed to that symbol
  (simple `unordered_map<Symbol, vector<IStrategy*>>`, filled by
  `Engine::SubscribeMd(strategy, symb)`).
- `Engine::OnOrderAck/OnFill` (Phase 0 code): after `Commit`, route the resulting
  OrderRaw to the owning strategy's matching callback.
- Facade for strategies (methods on Engine, so strategies never see Backend/ZMQ):
  `SendNew`, `SendChg`, `GetPosition(symb)`, `Now()`.
  **Replay rule (enforced by convention + review): strategies include only
  `IStrategy.hpp`, `Types.hpp`, `InboundEvents.hpp` — never adapter/ZMQ/chrono headers.**

**Done when:** a trivial `EchoStrategy` (logs every callback, sends one order on the
first tick) runs end-to-end in paper trading and observably receives
accepted → filled callbacks; a second registered strategy receives ticks but not the
first strategy's order events.

**Effort:** 3–4 evenings. Dispatch is hot path → Ryan; EchoStrategy + registry AI.

---

## Phase 4 — RiskGate (expands the `RiskCheck` stub)

**New files**
- `src/core/RiskGate.{hpp,cpp}`:
  ```cpp
  enum class TradingState : uint8_t { ACTIVE, REDUCING, HALTED };

  struct RiskConfig {              // per engine; per-symbol overrides later
      Qty      max_qty_per_order    = 3;
      int64_t  max_notional_per_order = 0;      // 0 = disabled; TWD, uses InstrumentInfo
      int32_t  max_abs_net_qty      = 5;        // per symbol, post-order
      uint32_t max_orders_per_sec   = 5;        // sliding-window throttler
      int64_t  max_daily_loss       = -30'000;  // TWD; breach → auto REDUCING
  };

  class RiskGate {
  public:
      // nullptr = pass; otherwise a static deny-reason string.
      const char* Check(const Request&, const Portfolio&, TradingState);
      void OnOrderSent(Timestamp);   // feeds the throttler window
  };
  ```
  Check order (cheapest first, mirroring nautilus `_check_order`):
  state gate (HALTED denies all; REDUCING denies exposure-increasing, decided via
  `Portfolio::NetQty` vs `Request.side`) → price sanity (>0, tick-size multiple) →
  qty (>0, ≤ max) → notional → post-order position limit → throttler.
- Throttler: fixed ring of last N send timestamps, deny if N-th newest is < 1s old.
  No allocation, no background thread — fits the single-threaded loop.

**Changes**
- `Types.hpp`: add `OrdSt::DENIED = -3`. A denied Request still produces an Order
  with one OrderRaw (`ord_st_ = DENIED`, committed to Backend — audit trail), but
  `PythonOrderAdapter` skips it (same mechanism as `op_type_ == Unknown`) so it
  never crosses the wire. **No wire format change.**
- `Engine::SendNew/SendChg`: call `risk_gate_.Check(...)` before `Begin`; on deny,
  commit the DENIED raw and fire `owner->OnOrderDenied(raw, reason)`; on pass,
  `risk_gate_.OnOrderSent(now)`.
- `Engine::SetTradingState(TradingState)` + auto-transition: `Portfolio::RealizedPnl()
  < max_daily_loss` after any fill → REDUCING + log loud.
- Config: hardcoded `RiskConfig` defaults in the header first; file-loaded config is
  a later nicety, not this refactor.

**Done when:** unit tests per check (each has a deny case + boundary pass case);
integration test: strategy that fires on every tick gets exactly
`max_orders_per_sec` sends + the rest denied with throttler reason; REDUCING lets a
closing order through and denies an opening one.

**Effort:** 3–4 evenings. Check logic is the module Ryan owns end-to-end (it's the
"manage rules of risk control" deliverable); scaffolding/tests AI.

**Docs:** `TradingState`, `RiskGate`, `Denied` → CONTEXT.md. Below ADR threshold
(reversible, unsurprising given ADR-0001 already names `IRiskCheck`).

---

## Phase 5 — Safety commands + control channel

**Changes**
- `Engine::CancelAllOrders()`: for every `orders_` entry with `Tail()->leave_qty_ > 0`
  and a live state, `SendChg(id, 0)`. Bypasses the throttler (mass-cancel must not
  self-deny) — RiskGate gets a `is_cancel` fast path (cancels are always
  exposure-reducing).
- `Engine::CloseAllPositions()`: for each `Portfolio` position with `net_qty != 0`,
  `SendNew` opposite side, `PriceType::MKP`, `OCType::Auto`. Goes through RiskGate
  normally (REDUCING permits it by construction).
- Control channel: third ZMQ socket (`REP`, `ipc:///tmp/tickengine-ctl`) polled in
  the existing main loop; text commands `state active|reducing|halted`, `flatten`
  (= HALTED + CancelAll + CloseAll), `pos`, `pnl`. Plus a ~40-line `tickctl` CLI.
  Chosen over stdin so it works when the engine runs detached/tmux/cloud, and over
  signals because commands carry arguments and replies.

**Done when:** during a paper session with a live position and a resting order,
`tickctl flatten` results in zero working orders and zero net position at Shioaji
within seconds, and every resulting OrderRaw is in the jsonl log.

**Effort:** 2 evenings. Almost all boilerplate → AI writes; Ryan reviews the flatten
semantics.

**Docs:** control channel is a new process-boundary interface → one short ADR-0006
(alternatives: stdin, signals; chosen: ZMQ REP).

---

## Phase 6 — Minimal boot reconciliation (supersedes V1 fail-fast, needs ADR-0007)

Policy (deliberately the simplest safe one): **on boot, cancel every working order;
adopt broker positions as truth.** "Flat orders, true positions." Orphan-order
adoption (rebuilding an Order from broker state) is explicitly out of scope — a
canceled resting order costs nothing to re-send by the strategy.

**Wire additions** (`wire_format.md` — also clear the four staleness notes flagged
in CONTEXT.md while in there):
- C++→Py `snapshot_request`
- Py→C++ `position_snapshot` (list of `{symbol, net_qty, avg_price}` from
  `list_positions()`), `open_orders_snapshot` (list of ordnos from `list_trades()`
  status-filtered), each with a `last` flag.

**Changes**
- Python adapter (`/quant` side): handle `snapshot_request`; issue cancels for all
  open orders via Shioaji; reply with snapshots after cancels ack.
- `Engine`: boot state `RECONCILING` — inbound ticks dropped, strategy callbacks
  suppressed until both snapshots applied; seed `Portfolio` from
  `position_snapshot`; then `OnStart()` to strategies and go `ACTIVE`.
- Strategies that need context after restart read `GetPosition()` in `OnStart()`.

**Done when:** kill -9 the engine mid-paper-session while long with a resting order;
restart; engine comes up flat-orders/true-position, strategy `OnStart` sees the
position, and no duplicate entry order is sent.

**Effort:** 4–6 evenings (both processes + failure cases). Split: Python side +
plumbing AI; the RECONCILING state machine Ryan.

---

## Phase 7 (parking lot — deliberately not planned)

- **Replay driver**: feed recorded tick jsonl through `Engine::OnTick` with a naive
  fill model. Cheap because of the Phase 3 replay rule. Do when first strategy tweak
  needs validation.
- **Perf final phase** (per PLAIN.md): LatencyRecorder histograms across the tick→order
  path; shm ring buffer vs ZMQ experiment; JSON→MessagePack if profiling indicts it.
- Unrealized P&L / mark-to-market risk rules; per-symbol RiskConfig overrides;
  config file loading.

---

## Sequencing summary

| Phase | Deliverable | Effort (evenings) | Blocks |
|---|---|---|---|
| 0 | inbound events mutate order state | 1–2 | everything |
| 1 | OrderRaw jsonl log | 1 | — |
| 2 | Portfolio/Position | 2–3 | 4, 5, 6 |
| 3 | IStrategy + routing | 3–4 | 4 (deny callback) |
| 4 | RiskGate + TradingState + throttler | 3–4 | 5 |
| 5 | flatten + tickctl | 2 | — |
| 6 | boot reconciliation | 4–6 | live money |
| 7 | replay / perf | parked | — |

Rough total for 0–6: ~16–22 evenings. Every phase ends runnable and paper-tradeable;
stopping after any phase leaves a working engine. Live money requires Phase 6.
