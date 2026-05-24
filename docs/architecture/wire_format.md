# Wire Format (Python ↔ C++)

V1 草稿。所有訊息是 **UTF-8 JSON**，每筆一個 ZMQ frame。

---

## 1. 共通信封 (Envelope)

每筆訊息都包成這個外框：

```json
{
  "msg_type": "tick",
  "seq_num": 12345,
  "ts_ns": 1715769600123456789,
  "data": { ... }
}
```

| 欄位 | 型別 | 說明 |
|------|------|------|
| `msg_type` | string | 見下面表，決定 `data` schema |
| `seq_num` | uint64 | 該方向單調遞增；V1 收端只 log，不做 gap 偵測 |
| `ts_ns` | uint64 | 送端打的 epoch nanoseconds（不是交易所時間，是本機時間）|
| `data` | object | 內容物，依 `msg_type` 而定 |

### msg_type 一覽

| msg_type | 方向 | 用途 |
|----------|------|------|
| `tick` | Py → C++ | 逐筆成交 |
| `bidask` | Py → C++ | 五檔快照 |
| `order_request` | C++ → Py | 策略要下單 |
| `order_ack` | Py → C++ | 永豐回 op_code（送出成功 / 拒絕） |
| `fill` | Py → C++ | 成交回報 |
| `event` | Py → C++ | 連線狀態（連上/斷線/重連） |

---

## 2. tick (Py → C++)

對應 Shioaji 的 `TickFOPv1`（期貨／選擇權）。股票之後再加 `TickSTKv1` 的對應。

```json
{
  "msg_type": "tick",
  "seq_num": 1,
  "ts_ns": 1715769600123456789,
  "data": {
    "code": "TXFE5",
    "exchange": "TAIFEX",
    "exchange_ts_ns": 1715769600100000000,
    "close": 21500.0,
    "open": 21480.0,
    "high": 21520.0,
    "low": 21470.0,
    "avg_price": 21490.3,
    "volume": 3,
    "total_volume": 12345,
    "amount": 1290000.0,
    "total_amount": 5310750000.0,
    "tick_type": 1,
    "chg_type": 2,
    "price_chg": 25.0,
    "pct_chg": 0.1163,
    "bid_side_total_vol": 6500,
    "ask_side_total_vol": 5800,
    "underlying_price": 21495.5,
    "simtrade": 0
  }
}
```

| 欄位 | 型別 | 對應 Shioaji |
|------|------|--------------|
| `code` | string | `tick.code` |
| `exchange` | string enum | `tick.exchange.value`；`"TSE"` / `"OTC"` / `"OES"` / `"TAIFEX"` |
| `exchange_ts_ns` | uint64 | `tick.datetime` → epoch ns（**底層精度 µs**，見設計決策 3） |
| `close` | float | `tick.close`（Decimal → float） |
| `open` / `high` / `low` | float | 同上 |
| `avg_price` | float | `tick.avg_price`（broker 端算的當日均價） |
| `volume` | uint32 | `tick.volume`（這筆量） |
| `total_volume` | uint64 | `tick.vol_sum` |
| `amount` | float | `tick.amount`（單筆成交額 NTD = price × volume × 契約乘數）⚠️ 見精度註 |
| `total_amount` | float | `tick.amount_sum`（當日累計成交額 NTD）⚠️ 見精度註 |
| `tick_type` | uint8 | `tick.tick_type`；1=外盤買, 2=內盤賣, 0=不明 |
| `chg_type` | uint8 | `tick.diff_type`；1=漲停, 2=漲, 3=平盤, 4=跌, 5=跌停 |
| `price_chg` | float | `tick.diff_price`（vs 昨收） |
| `pct_chg` | float | `tick.diff_rate`（漲跌幅 %） |
| `bid_side_total_vol` | uint64 | `tick.trade_bid_vol_sum` |
| `ask_side_total_vol` | uint64 | `tick.trade_ask_vol_sum` |
| `underlying_price` | float | `tick.target_kind_price`（期權才有意義；期貨會是該期貨自己的價，可忽略） |
| `simtrade` | uint8 | `tick.simtrade`；**0=正式撮合, 1=試撮**（盤前 8:30–9:00、收盤前集合競價、夜盤試撮會是 1） |

⚠️ **精度註（V1 取捨）：** `amount` / `total_amount` / 價格類欄位 wire 上是 JSON number，C++ 端目前全 parse 成 `double`。Shioaji 原生用 `Decimal`。`total_amount` 為 broker 端 tick-by-tick 累加值，理論上 IEEE 754 累加會 drift；V1 純當顯示 / 策略 feature，不做 engine 自累加 == broker_total_amount 的對帳。要做對帳時改 `int64` cents 或 `Decimal` 替代。

**simtrade 處理規則**：C++ 端策略主迴圈必須先檢查 `simtrade`：
- 一般策略：`if (simtrade) return;`，試撮的價量不可進撮合假設、不可成交、不可下單觸發
- 盤前分析策略：另開分支吃 simtrade==1 的封包

V1 沒帶的 TickFOPv1 欄位：`date` / `time` tuple（與 `datetime` → `exchange_ts_ns` 重複，省）。其他原本 deferred 的欄位（`amount` / `total_amount` / `avg_price` / `chg_*`）已在 § 2 全部加入。

---

## 3. bidask (Py → C++)

對應 `BidAskFOPv1`。五檔固定 5 層。

```json
{
  "msg_type": "bidask",
  "seq_num": 2,
  "ts_ns": 1715769600123456789,
  "data": {
    "code": "TXFE5",
    "exchange_ts_ns": 1715769600100000000,
    "bid_price":  [21499.0, 21498.0, 21497.0, 21496.0, 21495.0],
    "bid_volume": [12, 25, 18, 30, 14],
    "ask_price":  [21500.0, 21501.0, 21502.0, 21503.0, 21504.0],
    "ask_volume": [10, 22, 17, 28, 19],
    "bid_total_vol": 99,
    "ask_total_vol": 96,
    "underlying_price": 21495.5,
    "simtrade": 0
  }
}
```

| 欄位 | 型別 | 對應 Shioaji |
|------|------|--------------|
| `code` | string | `bidask.code` |
| `exchange_ts_ns` | uint64 | `bidask.datetime` → epoch ns（µs 精度，見設計決策 3） |
| `bid_price[5]` | float[5] | `bidask.bid_price` |
| `bid_volume[5]` | uint64[5] | `bidask.bid_volume` |
| `ask_price[5]` | float[5] | `bidask.ask_price` |
| `ask_volume[5]` | uint64[5] | `bidask.ask_volume` |
| `bid_total_vol` | uint64 | `bidask.bid_total_vol`（五檔買量加總，省一次 C++ 運算） |
| `ask_total_vol` | uint64 | `bidask.ask_total_vol` |
| `underlying_price` | float | `bidask.underlying_price`（選擇權報價時對應標的現價，做 vol 策略必備） |
| `simtrade` | uint8 | `bidask.simtrade`；**0=正式, 1=試撮**，處理規則同 tick |

V1 沒帶的 BidAskFOPv1 欄位（之後需要再加）：
- `diff_bid_vol` / `diff_ask_vol`：與上筆的量差，C++ 端比較前後快照可得
- `first_derived_bid_price` / `first_derived_bid_vol` / `first_derived_ask_price` / `first_derived_ask_vol`：一檔隱含委託（台期所組合單拆腳產生的衍生委託），做組合單套利或做市才需要

---

## 4. order_request (C++ → Py)

策略產生的下單請求。

```json
{
  "msg_type": "order_request",
  "seq_num": 100,
  "ts_ns": 1715769600200000000,
  "data": {
    "client_order_id": "tickengine-0001",
    "code": "TXFE5",
    "action": "Buy",
    "price": 21500.0,
    "quantity": 1,
    "price_type": "LMT",
    "order_type": "ROD",
    "octype": "Auto",
    "account": "futopt"
  }
}
```

| 欄位 | 型別 | 說明 |
|------|------|------|
| `client_order_id` | string | **C++ 自己產的 ID**，必填，全局唯一。後續 `order_ack` / `fill` 都會帶這個回來，C++ 用這個做關聯 |
| `code` | string | 合約代碼（如 `TXFE5`、`TXO202506C21000`） |
| `action` | enum | `"Buy"` / `"Sell"` |
| `price` | float | 限價單必填；市價單填 0 |
| `quantity` | int | 口數 |
| `price_type` | enum | `"LMT"` / `"MKT"` / `"MKP"` |
| `order_type` | enum | `"ROD"` / `"IOC"` / `"FOK"`（市價單必須 IOC 或 FOK） |
| `octype` | enum | `"Auto"` / `"New"` / `"Cover"` / `"DayTrade"`，見下方註 |
| `account` | enum | `"stock"` / `"futopt"` |

### octype 字串值的注意事項

Python 端 `sj.constant.FuturesOCType.NewPosition` 是常數名稱，**它的 `.value` 是字串 `"New"`**（不是 `"NewPosition"`）。Shioaji 在 order callback 序列化出來的 `oc_type` 字串也是 `"New"` / `"Cover"` / `"Auto"` / `"DayTrade"`。

所以 wire format 統一用 Shioaji 的序列化字串：

| wire `octype` | Python adapter 對應的常數 |
|---------------|---------------------------|
| `"Auto"` | `FuturesOCType.Auto` |
| `"New"` | `FuturesOCType.NewPosition` |
| `"Cover"` | `FuturesOCType.Cover` |
| `"DayTrade"` | `FuturesOCType.DayTrade` |

Python adapter 在 `order_request → api.place_order()` 之間建一個 dict 對照表完成轉換。

### client_order_id 的角色（重要）

永豐有兩個訂單識別欄位：

- `trade.order.id`（10 碼，例 `"de616839"`）：API 層全局識別，**FuturesOrderEvent 有，FuturesDealEvent 沒有**
- `trade.order.ordno`（5 碼，例 `"000009"`）：交易所層識別，**FuturesOrderEvent 與 FuturesDealEvent 都有**

因此 fill 跟 order_ack 能共通對到的鍵只有 `ordno`。Python adapter 的對照表流程：

1. C++ 送 `order_request` 帶 `client_order_id = "X"`
2. Python adapter 呼叫 `api.place_order()`，拿到 `trade.order.id = "Y"` 與 `trade.order.ordno = "Z"`
3. Python adapter 內部存兩張表：`Y ↔ X` 與 `Z ↔ X`
4. 收到 FuturesOrderEvent → 用 `Y` 反查 `X`；收到 FuturesDealEvent → 用 `Z` 反查 `X`
5. 把 `X` 一律放進 `client_order_id`，把 `Y` 放進 `broker_order_id`（deal 時 Python 補查得）送回 C++

C++ 端永遠用 `client_order_id` 對帳，`broker_order_id` 只給 log 用。

---

## 5. order_ack (Py → C++)

永豐確認收單／拒絕。對應 `OrderState.FuturesOrder` callback（`FuturesOrderEvent`）。

```json
{
  "msg_type": "order_ack",
  "seq_num": 3,
  "ts_ns": 1715769600300000000,
  "data": {
    "client_order_id": "tickengine-0001",
    "broker_order_id": "de616839",
    "op_type": "New",
    "op_code": "00",
    "op_msg": "",
    "accepted": true,
    "market_type": "Day"
  }
}
```

| 欄位 | 型別 | 對應 Shioaji | 說明 |
|------|------|--------------|------|
| `client_order_id` | string | (adapter 對照) | C++ 當初送的 ID |
| `broker_order_id` | string | `event["order"]["id"]` | 永豐 10 碼 id；拒絕時可能為空字串 |
| `op_type` | enum | `event["operation"]["op_type"]` | `"New"` / `"Cancel"` / `"UpdatePrice"` / `"UpdateQty"` |
| `op_code` | string | `event["operation"]["op_code"]` | `"00"` = 成功；其他 = 失敗代碼 |
| `op_msg` | string | `event["operation"]["op_msg"]` | 失敗訊息（成功時為空字串） |
| `accepted` | bool | 衍生欄位 | = `op_code == "00"`，C++ 端方便用 |
| `market_type` | enum | `event["order"]["market_type"]` | `"Day"` / `"Night"`；台指夜盤跟日盤的單要分開計算 P&L |

---

## 6. fill (Py → C++)

成交回報。對應 `OrderState.FuturesDeal` callback（`FuturesDealEvent`）。

```json
{
  "msg_type": "fill",
  "seq_num": 4,
  "ts_ns": 1715769600400000000,
  "data": {
    "client_order_id": "tickengine-0001",
    "broker_order_id": "de616839",
    "exchange_seq": "987654",
    "code": "TXFE5",
    "action": "Buy",
    "price": 21500.0,
    "quantity": 1,
    "exchange_ts_ns": 1715769600350000000,
    "security_type": "FUT",
    "market_type": "Day"
  }
}
```

| 欄位 | 型別 | 對應 Shioaji | 說明 |
|------|------|--------------|------|
| `client_order_id` | string | (adapter 由 `ordno` 反查) | C++ 用這個對到自己的 order |
| `broker_order_id` | string | (adapter 由 `ordno` 反查到 `id`) | 永豐 10 碼 id；log 用 |
| `exchange_seq` | string | `deal["exchange_seq"]` | 交易所成交序號，對帳 / 重複偵測用 |
| `code` | string | `deal["code"]` | 合約代碼 |
| `action` | enum | `deal["action"]` | `"Buy"` / `"Sell"` |
| `price` | float | `deal["price"]` | 這筆成交價 |
| `quantity` | int | `deal["quantity"]` | 這筆成交量（部分成交時 < 委託量） |
| `exchange_ts_ns` | uint64 | `deal["ts"]` × 1e9 | `deal.ts` 是 float seconds（**底層精度約 ms**，見設計決策 3） |
| `security_type` | enum | `deal["security_type"]` | `"FUT"` / `"OPT"` |
| `market_type` | enum | `deal["market_type"]` | `"Day"` / `"Night"`，分開計算 P&L |

V1 沒帶的 FuturesDealEvent 欄位（之後需要再加）：
- `combo: bool`：是否為組合單腳成交，V1 不支援組合單可省
- `subaccount: str`：子帳號，V1 單帳戶可省
- `delivery_month` / `strike_price` / `option_right`：選擇權合約細節，可由 `code` 解析

注意：永豐文件提到「成交回報可能比委託回報早到」。C++ 端要能處理 `fill` 比 `order_ack` 先到的情況，不要 assert 順序。

---

## 7. event (Py → C++)

連線狀態通知，給 C++ 端 log 用。

```json
{
  "msg_type": "event",
  "seq_num": 5,
  "ts_ns": 1715769600500000000,
  "data": {
    "source": "quote",
    "event_code": 2,
    "info": "Disconnected"
  }
}
```

| `event_code` | 意義 |
|--------------|------|
| 0 | Heartbeat |
| 1 | Connected |
| 2 | Disconnected |
| 3 | Reconnecting |
| 4 | Reconnected |
| 16 | Subscribe success |
| 17 | Unsubscribe success |

V1 收到 `Disconnected` C++ 端直接結束（fail-fast，符合 DESIGN.md）。

---

## 設計決策 (V1)

1. **JSON 不是 binary**：簡單、好 debug，效能之後再說。
2. **Price 用 float64**：TXF/MXF 是整數點，TXO 最小跳動 0.1，float64 精度遠夠。如果之後接股票零股 / 美股小數價發現精度問題再改 string。
3. **時間有兩個，欄位單位是 ns 但底層精度只有 µs/ms**：
   - `ts_ns`：送端本機時間 epoch ns，由 Python adapter 用 `time.time_ns()` 打，每筆都有
   - `exchange_ts_ns`：交易所/永豐給的時間 epoch ns，行情/成交才有
   - **行情封包（`tick.datetime` / `bidask.datetime`）底層精度 µs**（datetime 物件 6 位小數）
   - **成交封包（`deal.ts`）底層精度約 ms**（float seconds，sub-second 通常 3 位小數）
   - 欄位用 ns 是為了統一單位、避開 float 表示大時間戳的精度問題，**不是 ns 級延遲量測能力**
   - 策略要算延遲就 `ts_ns - exchange_ts_ns`，但 µs/ms 以下的數字無意義
4. **enum 用字串不用整數**：JSON 看起來直觀，C++ 端做一次 string → enum 對照就好。所有 enum 字串值**對齊 Shioaji 在 OrderEvent / DealEvent 序列化出來的字串**（例如 `octype` 用 `"New"` 不是 Python 常數名 `"NewPosition"`），減少 adapter 轉譯錯誤。
5. **`seq_num` V1 只 log 不檢查**：之後要做斷線補單時，C++ 端記住最後一筆 seq_num，重連後跟 Python 對表。
6. **JSON number 都在 int64 範圍內**：`seq_num` / `ts_ns` 是 uint64，理論上可超過 2^53。但實務上 epoch ns 到 2255 年前都不會超過 2^63；C++ 用 nlohmann/json、Python 用 stdlib `json`，兩端原生 int64 支援沒問題。**若之後要接 JS / 瀏覽器視覺化工具，再把超大整數欄位改成字串**。

## V1 不做的事

- 訊息壓縮
- Binary protocol (FlatBuffers / Protobuf)
- 訊息重送 / gap 補單
- `update_order` (改價/改量) 對應的 wire 訊息 — 先不支援改單，要改就 cancel + new
- 股票 tick / bidask（`TickSTKv1` / `BidAskSTKv1`）— 之後加
- 組合單（combo orders）
- BidAskFOPv1 的 `diff_*_vol` / `first_derived_*`（一檔隱含委託）
- FuturesDealEvent 的 `combo` / `subaccount` / 選擇權合約解析欄位
