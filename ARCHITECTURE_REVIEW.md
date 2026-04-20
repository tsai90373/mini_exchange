# Architecture Review

目前專案已經有 matching engine、Server/Session/Exchange 分離、TCP client 骨架，基本流程走得通。
下面是用「真實世界 trading system 標準」去看，應該改進的地方。

依照影響程度分成三層：**架構層（必改）**、**實作層（重要）**、**細節層（優化）**。

---

## 1. 架構層問題（必改）

### 1.1 Domain types 和 Wire types 沒有分離
**現況：** Session 用 `reinterpret_cast<Order*>(buf_.data())` 直接把網路封包當成 `Order` 用。
但 `Order` 同時也是 Exchange 內部的 domain object（有 `leaveQty_`、`filledQty_` 這種狀態）。

**問題：**
- 網路端不該關心 `leaveQty_`，但它出現在 wire format 裡
- `Order` 有建構子，不是純 POD，`reinterpret_cast` 是 undefined behavior（嚴格來說）
- 欄位順序、padding、endianness 沒有明確定義，換 compiler 可能就壞

**真實系統做法：** 分兩層
```cpp
// Wire layer — 只給網路用
#pragma pack(push, 1)
struct OrderNewMsg {
    uint32_t size;
    uint32_t ordId;
    char     symbId[6];
    char     side;     // 'B' / 'S'
    uint64_t price;
    uint32_t qty;
};
#pragma pack(pop)

// Domain layer — 只給 Exchange 用（你現在的 Order）
class Order { ... };
```

Session 負責 `OrderNewMsg → Order` 的轉換。兩層之間有明確的邊界。

**影響範圍：** 中等。要新增 wire 結構，Session 改解析邏輯。

---

### 1.2 Server 跟 Exchange / Session 耦合太緊
**現況：**
```cpp
Server(uint32_t port, Exchange& exchange)
// Server 內部：new Session(fd, exchange_)
```
Server 必須知道 Exchange 的存在，也必須知道怎麼建立 Session。

**問題：** 之後想加 MarketDataSession、DropCopySession，Server 要改；換掉 Exchange 的型別，Server 也要改。

**真實系統做法：** 用 factory 讓 Server 跟業務邏輯解耦
```cpp
using SessionFactory = std::function<std::unique_ptr<Session>(int fd)>;

class Server {
    SessionFactory factory_;
public:
    Server(uint32_t port, SessionFactory factory);
};

// main
Server server(8080, [&exchange](int fd) {
    return std::make_unique<OrderSession>(fd, exchange);
});
```

Server 現在完全不知道 Exchange 的存在，也不知道 Session 的具體型別。

**影響範圍：** 小。只改 Server 的 constructor 和一行 `new Session`。

---

### 1.3 沒有 thread safety / concurrency 設計
**現況：** Exchange 是 shared 的，但完全沒有保護。現在 single-threaded epoll 剛好沒事，但你 CLAUDE.md 提到 LMAX Disruptor、lock-free，代表你想做高效能。

**真實系統做法（單執行緒撮合，最常見）：**
```
Network Threads (多個)
    ↓ 把 Order 丟進 queue
Order Queue (SPSC / MPSC ring buffer)
    ↓
Matching Thread (單執行緒)
    ↓ 撮合結果
Report Queue
    ↓
Network Threads 送回 client
```

撮合 thread 是單執行緒的——不需要 lock，延遲低，邏輯也比較好推理。

**影響範圍：** 大。但現在不一定要做，先把核心流程通了再來設計。

---

### 1.4 Error handling 完全沒有
**現況：** `socket`、`bind`、`listen`、`accept`、`read`、`write`、`connect`、`epoll_*` 全部沒有檢查 return value。

**問題：** production 系統任何 syscall 都可能失敗（port 被佔、連線被對方 reset、buffer 滿等等）。現在出錯你什麼都不知道。

**真實系統做法：** 至少每個 syscall 檢查 return value，log error：
```cpp
int listen_fd = socket(AF_INET, SOCK_STREAM, 0);
if (listen_fd < 0) {
    perror("socket");
    return false;
}
```

**影響範圍：** 小但範圍廣。每個地方加 check，不改架構。

---

## 2. 實作層問題（重要）

### 2.1 `OrderBook` 的 zombie order 問題
**現況：** `std::queue<Order*>` 沒辦法從中間刪除，你用 `leaveQty_ = 0` 當 tombstone。

**真實做法：** 用 intrusive linked list，每個 `Order` 自己帶 `prev`/`next` pointer，刪除任何一個是 O(1)。
libfon9 就是用這個模式。

```cpp
class Order {
    Order* prev_ = nullptr;
    Order* next_ = nullptr;
    // ...
};
```

OrderBook 只存 head/tail，不是 queue。

**影響範圍：** 中。需要重寫 OrderBook 的資料結構，`match` function 也要改。

---

### 2.2 Partial read / write 沒處理
**現況：**
- Server 的 `read` 預設會一次讀完整封包
- Client 的 `write` 預設會一次送完
- Session 的 header parsing 預設 4 bytes 一定在第一次 read 裡

**真實做法：** 每次 read/write 都要 loop 直到湊齊或 buffer 滿。
- Session 用一個 stream-style 的 parser：把所有 bytes 塞進 buffer，每次檢查「我有夠多 bytes 可以 parse 下一筆嗎」
- Server/Client 用 write buffer，`write` 不一定一次送完，要存著下次 `EPOLLOUT` 再送

**影響範圍：** 中。Session 的 `OnRecvData` 邏輯要重寫。

---

### 2.3 Session 的 state machine 太簡單
**現況：** 只有 `WaitingHeader` / `WaitingBody` 兩個狀態，而且假設 header 一定完整到達。

**建議：** 改成 buffer-based，不用 state machine：
```cpp
void Session::OnRecvData(char* buf, int n) {
    buf_.insert(buf_.end(), buf, buf + n);
    while (hasCompleteMessage()) {
        processOneMessage();
        removeProcessedBytes();
    }
}
```

這樣不管資料怎麼切分、幾筆訊息打包在一起、header 有沒有分段，都處理得了。

**影響範圍：** 中。Session 內部大改，但外部介面不變。

---

### 2.4 裸 `new` 在 main.cpp
**現況：**
```cpp
Server* svr = new Server{8080, exchange};
svr->run();
```
永遠沒 delete，leak。雖然 main 結束 OS 會回收，但這是 bad practice。

**真實做法：**
```cpp
Exchange exchange;
Server server(8080, exchange);  // stack 上
server.run();
```
Server 本來就跟 main 同生命週期，stack 最合適。

**影響範圍：** 很小。

---

### 2.5 沒有 `SO_REUSEADDR`
**現況：** server crash 後重啟，port 還在 TIME_WAIT，要等幾十秒才能 bind。

**改法：**
```cpp
int opt = 1;
setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
```

基本上是 TCP server 標配。

---

### 2.6 Exchange 的 `SendNew` 太長
**現況：** `SendNew` 有 80+ 行，買單賣單邏輯是對稱的但分開寫。

**真實做法：** 抽出 helper function，用統一邏輯：
```cpp
bool Exchange::SendNew(Order& ord) {
    Symbol* symb = findSymbol(ord.symbId_);
    if (!symb) return false;
    OrderBook* book = findBook(*symb);
    if (!book) return false;

    if (ord.side_ == Side::Buy)
        processNew<Asks, Bids>(ord, book->asks_, book->bids_, book->ask_1, book->bid_1);
    else
        processNew<Bids, Asks>(ord, book->bids_, book->asks_, book->bid_1, book->ask_1);
    return true;
}
```

這是後續優化，現在先能動比較重要。

---

## 3. 細節層問題（優化）

### 3.1 命名風格不一致
| 現況 | 建議 |
|---|---|
| `SendNew`, `OnRecvData` (PascalCase) | 統一用 `sendNew`, `onRecvData` (camelCase) |
| `port_`, `Ip_` (大小寫混用) | 統一 `ip_`, `port_` (小寫開頭) |
| `RevState` | `RecvState` (typo) |
| `sesList_` | `sessions_` |

C++ 沒有官方規範，但同一個專案要一致。Google C++ Style 是很多公司的標準。

### 3.2 Header dependency
Server.hpp include Session.hpp 而 Session.hpp include Exchange.hpp——Server 間接依賴 Exchange。
用 forward declaration 能減少編譯時間：
```cpp
// Server.hpp
class Session;  // forward declare
class Exchange; // forward declare
```
.cpp 才 `#include`。

### 3.3 Magic numbers
- `buf[1024]`
- `epoll_event events[64]`
- `SOMAXCONN` 已經用了，好

改成 constexpr:
```cpp
constexpr int MAX_EVENTS = 64;
constexpr int READ_BUFFER_SIZE = 1024;
```

### 3.4 `Order` 應該是 value type，但現在混用 `Order*` 和 `unique_ptr<Order>`
你的 `orderPool_` 用 `unique_ptr<Order>` 管理生命週期，但 `OrderBook` 的 queue 用 raw pointer。
這個二元管理現在還 OK，但之後改成 intrusive list 就會更乾淨。

### 3.5 缺少的功能（CLAUDE.md 列的）
- **Logging** — 目前只有 `printf`，該有正經的 async logger（spdlog 或自己寫）
- **Unit test** — main.cpp 裡有一些 `assert`，但沒有 test framework。GoogleTest 或 Catch2
- **Benchmark** — 沒有。Google Benchmark 是標配
- **Memory allocator** — 進階項目，OrderPool 可以用 object pool 避免 heap allocation

---

## 4. 建議的下一步順序

按優先級：

1. **先把 end-to-end 流程跑通** — Client 送單 → Server 收到 → Session 解析 → Exchange 撮合 → 回傳 report（這個做完再開始重構）
2. **加 error handling** — 每個 syscall 檢查 return value + log
3. **設計 wire protocol** — 分離 Wire 和 Domain type（1.1）
4. **加 unit test framework** — GoogleTest
5. **加 async logger**
6. **重構 OrderBook 用 intrusive list**
7. **SendNew 合併買賣邏輯**
8. **Server 用 factory pattern**（1.2）
9. **單執行緒撮合 + MPSC queue**（1.3）
10. **Benchmark + 優化**

---

## 5. 一個原則性建議

你在 CLAUDE.md 說想要「達到真正實戰等級的系統的標準」——這是對的方向，但要注意：

**先讓系統能動、再來追求架構完美。**

現在你已經快要跑通了，先集中把 client → server → exchange 的閉環做通，能送一筆 order 然後收到 confirmation，這個里程碑很重要。

架構重構可以之後一次做，不用邊寫 feature 邊重構。你已經知道哪裡不好了（都寫在 Q: 裡），有意識就夠。
