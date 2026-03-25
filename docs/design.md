# LOBE — System Design Document

**Light Order Backtesting Engine**
A Level-2, order-by-order backtesting engine for HFT and market-making strategy research.

---

## 1. Goals and Non-Goals

### Goals
- Reconstruct full-depth limit order books from exchange message feeds.
- Support multi-asset backtesting with a unified event timeline.
- Provide a zero-overhead Strategy API using C++20 Concepts.
- Enable incremental sophistication: start simple, swap in better models later.

### Non-Goals (for now)
- Production-grade latency (nanosecond-precise real-time replay).
- Direct exchange connectivity or live trading.
- GUI or interactive visualization.

---

## 2. Architecture Overview

LOBE is built as a set of loosely-coupled modules connected through C++ Concepts.
The Engine is a class template that assembles all modules at compile time —
no virtual dispatch, no runtime overhead.

```
┌──────────────────────────────────────────────────────────────┐
│                     Engine<Feed, Strategy, FM, LM>           │
│                                                              │
│  ┌────────────┐    ┌────────────┐    ┌────────────────────┐  │
│  │ FeedParser  │───>│ BookManager│───>│ Strategy           │  │
│  │ (concept)   │    │ (LOBs)     │    │ (concept)          │  │
│  └────────────┘    └────────────┘    └─────────┬──────────┘  │
│                                                │ SimOrders   │
│                                                v             │
│                    ┌────────────┐    ┌──────────────────┐    │
│                    │ Stats      │<───│ FillModel        │    │
│                    │ (PnL, CSV) │    │ (concept)        │    │
│                    └────────────┘    │ + LatencyModel   │    │
│                                     │   (concept)      │    │
│                                     └──────────────────┘    │
└──────────────────────────────────────────────────────────────┘
```

### Data Flow (single-threaded event loop)

```
while feed.has_next():
    event = feed.next()                // 1. Read next market event
    books.apply(event)                 // 2. Update the LOB
    check_fills(pending_orders, now)   // 3. Check simulated orders for fills
    strategy.on_book_update(book)      // 4. Strategy reacts, may submit orders
    collect_orders(submitter)          // 5. Enqueue new sim orders
```

---

## 3. Module Descriptions

### 3.1 Feed Layer (`include/lobe/Feed/`)

**Purpose:** Convert raw exchange data into normalized internal events.

| File | Description |
|------|-------------|
| `Event.h` | Defines `AddOrder`, `CancelOrder`, `DeleteOrder`, `ExecuteOrder`, `ReplaceOrder`, `TradeMessage` as plain structs. All are collected in `Event` (a `std::variant`). |
| `FeedParser.h` | Defines the `FeedParser` concept (`has_next()` + `next()`). Also provides `SyntheticFeed` for testing. |

**Concept:**
```cpp
template <typename F>
concept FeedParser = requires(F f) {
    { f.has_next() } -> std::convertible_to<bool>;
    { f.next() }     -> std::same_as<Event>;
};
```

**Extension point:** To support a new exchange protocol (e.g., Nasdaq ITCH 5.0),
implement a struct that reads the binary file and emits `Event` objects.

### 3.2 Book Layer (`include/lobe/Book/`)

**Purpose:** Maintain per-instrument full-depth order books.

| File | Description |
|------|-------------|
| `LOB.h` | Single-instrument limit order book. Struct-of-arrays layout with free-list memory reuse. Supports `apply(Event)` for event-driven updates and `best_bid()`/`best_ask()`/`mid_price()` queries. |
| `BookManager.h` | Maps `SymbolId → LOB`. Lazily creates books on first event. |

**Memory layout:** The `OrderTable` uses parallel vectors (SoA) for cache-friendly
sequential access. Deleted orders are recycled via a free-list to avoid allocator
pressure.

**FIFO ordering:** Orders at the same price level are maintained in arrival order
via a doubly-linked list threaded through the `OrderTable`.

### 3.3 Strategy Layer (`include/lobe/Strategy/`)

**Purpose:** Define the user-facing trading API.

| File | Description |
|------|-------------|
| `Strategy.h` | Defines the `Strategy` concept, `SimOrder`/`Fill` types, and `OrderSubmitter`. |

**Concept:**
```cpp
template <typename S>
concept Strategy = requires(S s, SymbolId sym, const LOB& book,
                            const Event& event, OrderSubmitter& sub,
                            const Fill& fill) {
    { s.on_book_update(sym, book, event, sub) };
    { s.on_fill(fill) };
};
```

**How strategies submit orders:** The `on_book_update` callback receives an
`OrderSubmitter&`. The strategy calls `submitter.submit(SimOrder{...})` to
enqueue orders. The Engine drains them after the callback returns.

### 3.4 Simulation Layer (`include/lobe/Sim/`)

**Purpose:** Model the gap between strategy intent and market reality.

| File | Description |
|------|-------------|
| `FillModel.h` | Defines the `FillModel` concept and `ProbabilisticFillModel` (fills on price crossing). |
| `LatencyModel.h` | Defines the `LatencyModel` concept and `NoLatency` (zero delay). |

**FillModel concept:**
```cpp
template <typename F>
concept FillModel = requires(const F f, const SimOrder& order,
                             const LOB& book, Timestamp now) {
    { f.check_fill(order, book, now) } -> std::same_as<std::optional<Fill>>;
};
```

**Upgrade path:**
- **Phase 1 (current):** `ProbabilisticFillModel` — fills when the market
  crosses through the limit price. Simple, gets the pipeline working.
- **Phase 2 (future):** `QueuePositionFillModel` — tracks the simulated order's
  position in the queue; fills only after sufficient volume passes ahead of it.

**LatencyModel concept:**
```cpp
template <typename L>
concept LatencyModel = requires(const L l, Timestamp send_time) {
    { l.delivery_time(send_time) } -> std::same_as<Timestamp>;
};
```

### 3.5 Engine (`include/lobe/Engine/`)

**Purpose:** Orchestrate the event loop.

| File | Description |
|------|-------------|
| `Engine.h` | Class template `Engine<Feed, Strat, FM, LM>`. Owns all components. Runs the single-threaded event loop. |

The Engine is the only component that knows about all other modules. It is
parameterized on four concepts, allowing any conforming implementation to
be plugged in at compile time.

```cpp
template <FeedParser Feed, Strategy Strat,
          FillModel FM = ProbabilisticFillModel,
          LatencyModel LM = NoLatency>
class Engine { ... };
```

### 3.6 Stats (`include/lobe/Stats/`)

**Purpose:** Collect and report performance metrics.

| File | Description |
|------|-------------|
| `Stats.h` | Records fills, computes per-symbol PnL (average-cost method), prints summary to stdout, writes per-fill CSV. |

---

## 4. Project Layout (LLVM Style)

```
lobe/
├── CMakeLists.txt                  # Top-level: project(), add_subdirectory()
├── docs/
│   ├── design.md                   # This document
│   └── Doxyfile                    # Doxygen configuration
├── include/lobe/                   # Public headers (namespaced)
│   ├── Feed/
│   │   ├── Event.h
│   │   └── FeedParser.h
│   ├── Book/
│   │   ├── LOB.h
│   │   └── BookManager.h
│   ├── Sim/
│   │   ├── FillModel.h
│   │   └── LatencyModel.h
│   ├── Engine/
│   │   └── Engine.h
│   ├── Strategy/
│   │   └── Strategy.h
│   └── Stats/
│       └── Stats.h
├── lib/
│   └── CMakeLists.txt              # LOBECore INTERFACE library
├── tools/
│   └── lobe-backtester/
│       ├── CMakeLists.txt
│       └── main.cpp                # Demo: synthetic data + market maker
└── unittests/
    └── CMakeLists.txt              # Placeholder for future tests
```

### Build system

The project uses CMake with per-directory `CMakeLists.txt` files:

- **LOBECore** is an `INTERFACE` library (header-only). If `.cpp` files are
  added later, change to `STATIC`.
- **lobe-backtester** is the demo executable, linking against LOBECore.
- C++23 is required (`CMAKE_CXX_STANDARD 23`).

---

## 5. Concepts as Extension Points

Every major module boundary is defined by a C++ Concept. This means:

1. **Zero runtime overhead** — all dispatch is resolved at compile time.
2. **Type safety** — constraint violations produce clear error messages.
3. **Easy extension** — implement a new struct, satisfy the concept, done.

| Concept | Where | Purpose |
|---------|-------|---------|
| `FeedParser` | Feed → Engine | Data source abstraction |
| `Strategy` | Engine → User code | Trading logic interface |
| `FillModel` | Engine → Sim | Fill determination |
| `LatencyModel` | Engine → Sim | Order delivery delay |

---

## 6. Key Design Decisions

| Decision | Choice | Rationale |
|----------|--------|-----------|
| Concurrency model | Single-threaded event queue | Deterministic replay, no synchronization overhead, ITCH data is naturally sequential. |
| Polymorphism | C++20 Concepts (compile-time) | Zero-cost abstraction; critical for HFT where every nanosecond matters. |
| Memory layout | Struct-of-arrays + free list | Cache-friendly for sequential scans; O(1) slot reuse avoids allocator fragmentation. |
| Fill simulation | Probabilistic (price crossing) | Gets the pipeline working fast. Interface designed for drop-in queue-position upgrade. |
| Latency model | No-op placeholder | Simplifies initial development. `LatencyModel` concept ready for real implementations. |
| Multi-asset | `BookManager` (hash map of LOBs) | Lazily created per symbol, no pre-configuration needed. |
| Price type | `double` | Sufficient precision for backtesting. Production systems may use fixed-point. |

---

## 7. Future Work

- [ ] **ITCH 5.0 Parser** — Binary protocol reader satisfying `FeedParser`.
- [ ] **Queue Position Fill Model** — Track simulated order's queue position for realistic fill simulation.
- [ ] **Latency Distribution Model** — Sample from measured RTT distributions.
- [ ] **Unit Tests** — Per-module tests using a framework like Catch2 or Google Test.
- [ ] **Performance Benchmarks** — Events/second throughput on real ITCH data.
- [ ] **Order matching engine** — Full matching for crossed sim orders.

---

## 8. Building and Running

```bash
# Configure
cmake -B build -G Ninja

# Build
cmake --build build

# Run the demo
./build/tools/lobe-backtester/lobe-backtester

# Generate documentation (requires Doxygen)
cd docs && doxygen Doxyfile
```
