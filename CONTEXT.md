# TickEngine

Domain glossary for the C++ trading engine and its Python adapter. Use these terms verbatim in code, comments, commits, and issues. When a concept is missing here, that is a signal to grill — not to silently invent.

## Language

### Order lifecycle

**Request**:
One outbound instruction from C++ to the broker. Comes in two kinds: **New Request** (creates a new Order) and **Chg Request** (modifies an existing Order's price or quantity).
_Avoid_: `OrderRequest`, `Send`, `Msg`.

**New Request**:
A Request that creates a fresh Order. Issued via `Engine::SendNew`.

**Chg Request**:
A Request that targets an existing Order to change its price or quantity. Issued via `Engine::SendChg`. A Chg with `qty = 0` is a **Cancel** — there is no separate `SendCancel` API.

**Cancel**:
A Chg Request with `qty = 0`. Not modeled as a distinct concept in this engine — cancel is just "change the leave quantity to zero".
_Avoid_: `Kill`, `Delete`, `Pull`.

**OrderRaw**:
One snapshot of an Order's state at a moment in time — Request fields plus dynamic state (`status`, `leave_qty`, `filled_qty`). Produced on **every** state transition (Request emit, ack, fill, cancel-ack). Append-only; never mutated in place.
_Avoid_: `OrderState`, `OrderSnapshot`, `OrderUpdate`.

**Order**:
The container that chains every OrderRaw produced over the lifetime of one initial New Request, including any subsequent Chg Requests against it. Identified by a single `client_order_id`.
_Avoid_: `Trade` (that's the executed counterpart), `Position` (that's the aggregate result of many filled Orders).

## Relationships

- A **Strategy** emits a **New Request** → C++ creates an **Order** to track its lifetime
- A **Strategy** may emit zero or more **Chg Requests** against an existing **Order** (same `client_order_id`)
- Every Request (New, Chg, Cancel) and every inbound broker event (ack, fill) appends one **OrderRaw** to its **Order**
- One initial **New Request** maps to exactly one **Order**

### op_type

**op_type**:
The operation a Request performs on an Order. Canonical values: `"New"`, `"UpdatePrice"`, `"UpdateQty"`, `"Cancel"`. These four strings are the engine's own vocabulary — broker adapters translate to/from per-broker terminology. The C++ `Engine::SendChg(qty=0)` ergonomic maps internally to `op_type: "Cancel"` on the wire.
_Avoid_: `action` (that means Buy/Sell, a different field), `operation`, `op` (too short), `kind`.

### Order identity

**client_order_id**:
The engine-generated string that uniquely identifies one Order over its lifetime. **The only identifier the engine, Strategies, and wire format ever speak in.** V1 format: `{strategy_name}-{boot_yymmdd_hhmm}-{counter}` (e.g., `DT-TEX-260517-1330-001`). See `docs/adr/0003-order-identity.md`.
_Avoid_: `client_id`, `order_id` (ambiguous — both names overlap with broker IDs), `oid`.

**broker_order_id** _(out of engine scope)_:
Shioaji's 10-char `trade.order.id`. Used inside the Python adapter for its own logging. **Not carried on any wire message.** Listed here only so future contributors know not to plumb it through.

**ordno** _(out of engine scope)_:
TAIFEX's 5-char `trade.order.ordno`. The only identifier present on **both** `FuturesOrderEvent` and `FuturesDealEvent`, so the Shioaji adapter uses it as the correlation key. **Adapter-internal.** Never crosses the wire.

### Engine internals

**Engine** _(Core)_:
The coordinator. Owns strategy registry, risk gate, the `client_order_id → Order` index (`orders_`), and the `cid_counter_`. Does **not** own `OrderRaw` storage — for any state change it calls `Backend::Begin / Commit`. Single-threaded main loop (ADR-0001).
_Avoid_: `OmsCore` (that's the f9omstw name; we use `Engine`).

**Backend**:
Append-only storage of every `OrderRaw` ever produced, plus monotonic `RxSno` allocation and a `Subscriber` callback list. Lives inside the Engine process as a member referenced by `Engine`. The single publication point: ZMQ outbound adapter, log writer, and audit are all `Subscribe`-ers. See `docs/adr/0004-core-backend-split.md`.
_Avoid_: `History`, `Store`, `Journal`.

**RxSno**:
Monotonic uint32 assigned by `Backend::Commit` to each `OrderRaw`. The sequence number across all OrderRaws system-wide (not per-Order). `Backend::GetBySno(sno)` gives O(1) lookup. Not on the wire (see `wire_format.md` § 7).

## Flagged ambiguities

- **`Request` (domain) vs `order_request` (wire `msg_type`)** — same concept at two layers. The C++ class is `Request`; the JSON `msg_type` is `order_request`. Don't mix the names when speaking about one specific layer.
- **`wire_format.md` § 4 and § 8 are stale w.r.t. the canonical `op_type` vocabulary** — § 4 needs `op_type` added to the `order_request` schema; § 8 should drop the "改單先不支援" line. Update when convenient.
- **`wire_format.md` § 5 and § 6 are stale w.r.t. ADR-0003** — drop the `broker_order_id` field from `order_ack` and `fill` schemas. § 4's "client_order_id 的角色" subsection should be rewritten to describe a single `client_order_id ↔ ordno` map.
- **`ClOrdId` is currently `uint32_t` in `Types.hpp`** — ADR-0003 mandates the string form `{strategy}-{boot_yymmddhhmm}-{counter}`. Type migration is tracked as separate work; until then, `Engine::AllocateClOrdId` returns a counter.
