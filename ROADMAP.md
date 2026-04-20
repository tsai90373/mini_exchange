# Roadmap — 未來要加的功能與技術

優先順序原則：**每個項目應該為下一個打基礎**。前面做好，後面才有意義。
例：沒有 latency histogram 就沒有 benchmark 可言、沒有 risk check 的 exchange 不像 exchange。

---

## Phase 1：先做完目前的架構問題（ARCHITECTURE_REVIEW.md）
不在這份 roadmap 裡，但要先做完才開始 Phase 2。

---

## Phase 2：工程品質基線
> 花 2–3 天，值得先做的，之後每個 feature 都會受益。

### 2.1 CMake 取代 Makefile
- 業界標準，cross-platform、能找 library、能分 subdirectory
- 為之後加 GoogleTest、Google Benchmark 鋪路

### 2.2 Unit Test Framework
- **GoogleTest** 或 **Catch2** 二選一（GoogleTest 比較主流）
- 先把 Exchange 的撮合邏輯寫 test（main.cpp 裡那些 commented 的 assert 改成正式 test）
- 每個 bug 修完都要有 regression test

### 2.3 Sanitizers
- 編譯時加：`-fsanitize=address,undefined`、`-fsanitize=thread`（兩個要分開編）
- ASan 抓記憶體問題、UBSan 抓 UB、TSan 抓 race condition
- Debug build 預設開啟

### 2.4 CI/CD（GitHub Actions）
- push 自動 build + test + 跑 sanitizer build
- PR 檢查必過才能 merge
- 面試官看 GitHub 會注意這個

### 2.5 Async Logger（spdlog 或自幹）
- 先用 spdlog 讓系統有正經 logging
- 之後可以自己實作一個 lock-free async logger 作為進階練習
- 所有 `printf` 都換掉

---

## Phase 3：核心交易系統功能
> 讓它看起來「像」真實 exchange。這些是面試官會問的。

### 3.1 Risk Check 模組
- Position limit / credit limit
- Self-trade prevention（同一帳戶的 buy 跟 sell 不能撮合）
- Fat finger check（價格偏離 reference price 過大拒絕）
- 放在 Exchange 接到 order 之後、撮合之前

### 3.2 Time-in-Force
- IOC (Immediate or Cancel)：不能立刻成交的部分刪除
- FOK (Fill or Kill)：不能全部成交就全刪
- GTD (Good Till Date) / GTC (Good Till Cancel)
- 牽涉 timer wheel 的設計

### 3.3 Market Data Publisher
- 成交後廣播 trade tick + L2 order book delta
- snapshot + incremental update 的標準模式
- Sequence number + gap recovery 機制

### 3.4 Persistence / Write-Ahead Log (WAL)
- 每筆 order 進 Exchange 前先寫 log
- Exchange crash restart 後能 replay log 恢復狀態
- 涉及 mmap、fsync、log replay——系統程式設計的好題材

---

## Phase 4：Latency 觀測 + 基本效能
> 沒有觀測就沒有優化。

### 4.1 Latency Histogram
- 用 **HdrHistogram** 記錄各階段 latency
- 測量點：網路進來→parse→到 matching queue→撮合完→報告送出
- p50 / p99 / p999 三個數字是黃金指標

### 4.2 Benchmark Framework
- **Google Benchmark** 寫 micro-benchmark
- 針對 matching engine、order book operations 量化效能
- 跟 baseline 比較，看每次改動的影響

### 4.3 單執行緒撮合 + MPSC Queue
- 多個 network thread 把 order 丟進 queue
- 單一 matching thread 從 queue 取出撮合
- 先用簡單版（`std::queue` + mutex）跑通，再換 lock-free

### 4.4 Object Pool Allocator
- Order 用 pool allocation，避免 heap allocation 的不確定性
- Free list based，搭配 placement new
- Exchange 的 hot path 不該呼叫 `new`/`make_unique`

---

## Phase 5：低延遲進階
> 這些才是真正的 low-latency 技術。前面沒做好做這個沒意義。

### 5.1 Lock-free Queues (LMAX Disruptor)
- SPSC ring buffer → MPSC → MPMC 循序實作
- 避免 false sharing（cache line padding）
- Sequence-based 的同步

### 5.2 io_uring（取代 epoll）
- Linux 5.1+ 的新一代 async I/O
- 比 epoll 省 context switch
- 2024+ low-latency 圈的趨勢

### 5.3 C++20 Coroutines
- 把 callback 模式改寫成 coroutine
- Code 大幅變短、更線性
- 了解 `co_await`、`co_return`、promise type、awaitable

### 5.4 CPU Affinity + NUMA Awareness
- `pthread_setaffinity_np` 把 matching thread 綁到特定 CPU
- 隔離 CPU（isolcpus），確保 kernel 不會切走
- NUMA-aware memory allocation

### 5.5 HugePages
- 減少 TLB miss
- 需要 kernel 支援 + mmap 指定 flag

### 5.6 SIMD Optimization
- 批次處理 order 時可以 vectorize
- 了解 AVX2 / AVX512 intrinsics

---

## Phase 6：協議 + 業務完整性
> 讓它不只是 demo，而是能對外服務。

### 6.1 FIX Gateway
- 支援 FIX 4.4 / 5.0 協議（對外介面）
- 可以用 QuickFIX library，或自己寫 parser
- 理解 tag=value 格式、session management、heartbeat

### 6.2 進階 Matching Algorithm
- Pro-rata matching（期貨常用）
- 開盤 / 收盤集合競價（auction）
- Matching algorithm 可配置

### 6.3 Admin / Query API
- REST or gRPC 介面查詢 order book 狀態、trade history
- 給監控系統用

### 6.4 Websocket Gateway
- 給瀏覽器 client 用
- 可以串一個簡單前端顯示 order book

---

## Phase 7：生產等級
> 真的上線要的東西。選做。

### 7.1 Config 系統 + Hot Reload
- YAML/JSON config
- 不用重啟就能改 risk limit、logging level

### 7.2 Fuzz Testing
- `libFuzzer` 餵亂七八糟的封包給 Session
- 找 edge case、security issue

### 7.3 Kernel Bypass（超進階）
- DPDK、Solarflare OpenOnload、AF_XDP
- 真的 HFT 才需要這層
- 了解概念就好，實作成本極高

### 7.4 Time Synchronization
- PTP (Precision Time Protocol) 達到 nanosecond 級同步
- Order timestamp 要一致

### 7.5 改寫 STL
- 自己實作 `vector`、`unordered_map` 等
- 更多是學習性質，知道 STL 內部運作

---

## 建議的學習順序總結

如果你想在 GaTech 開學前把這個專案做到「面試加分」等級，建議優先順序：

```
Phase 1 (架構問題)
  ↓
Phase 2.1 CMake + 2.2 Unit Test + 2.3 Sanitizers + 2.5 Logger   ← 工程品質
  ↓
Phase 4.1 Latency Histogram   ← 最先做，之後所有優化都需要
  ↓
Phase 3.1 Risk Check          ← 最能展現懂業務
  ↓
Phase 4.3 單執行緒撮合 + Queue  ← 真實 exchange 的 threading model
  ↓
Phase 4.4 Object Pool         ← 開始優化 latency
  ↓
Phase 5.1 Lock-free Queue     ← LMAX Disruptor
  ↓
Phase 3.2 TIF + 3.3 Market Data + 3.4 WAL  ← 讓它像真的 exchange
  ↓
(Phase 5 其他 / Phase 6 / Phase 7 看興趣)
```

**關鍵判斷：** 如果你的時間只夠做 3 件大事，選：
1. **Latency Histogram + Benchmark**（量化所有優化）
2. **Risk Check**（展現懂業務）
3. **Lock-free Matching Queue**（展現懂 low-latency）

其他都是加分項。
