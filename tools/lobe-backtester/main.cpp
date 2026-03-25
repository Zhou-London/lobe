/// @file main.cpp
/// @brief Demo backtest: a simple market-making strategy on synthetic data.
///
/// This program demonstrates the LOBE framework by:
/// 1. Generating synthetic L2 market data events (a SyntheticFeed).
/// 2. Running a naive market-making strategy that quotes around mid-price.
/// 3. Printing backtest statistics and writing fills to CSV.

#include "lobe/Engine/Engine.h"
#include <cstdio>

// ============================================================
// A simple market-making strategy for demonstration.
// ============================================================

/// @brief A naive market-making strategy.
///
/// On each book update, the strategy places a bid and an ask around
/// the current mid-price with a configurable spread. It maintains
/// at most one bid and one ask at a time.
///
/// This is intentionally simplistic — it demonstrates the Strategy
/// concept wiring, not a profitable trading algorithm.
struct SimpleMarketMaker {
    double   half_spread = 0.50;  ///< Half the bid-ask spread to quote.
    uint64_t quote_qty   = 10;    ///< Quantity to quote on each side.
    uint64_t next_id     = 1;     ///< Next simulated order ID to assign.

    /// @brief Called after each book update.
    ///
    /// If the book has both a bid and an ask, compute mid-price and
    /// submit a bid below and an ask above.
    void on_book_update(lobe::SymbolId sym, const lobe::LOB& book,
                        const lobe::Event& /*event*/,
                        lobe::OrderSubmitter& submitter) {
        double mid = book.mid_price();
        if (mid == 0.0) return;  // Book is one-sided; skip.

        // Quote a bid below mid and an ask above mid.
        submitter.submit(lobe::SimOrder{
            .sim_order_id = next_id++,
            .symbol       = sym,
            .type         = lobe::SimOrderType::limit,
            .side         = lobe::Side::bid,
            .price        = mid - half_spread,
            .qty          = quote_qty,
            .submit_ts    = 0,  // Engine fills this in.
        });

        submitter.submit(lobe::SimOrder{
            .sim_order_id = next_id++,
            .symbol       = sym,
            .type         = lobe::SimOrderType::limit,
            .side         = lobe::Side::ask,
            .price        = mid + half_spread,
            .qty          = quote_qty,
            .submit_ts    = 0,
        });
    }

    /// @brief Called when a simulated order is filled.
    void on_fill(const lobe::Fill& fill) {
        std::printf("  [FILL] order=%llu sym=%u side=%s price=%.2f qty=%llu ts=%llu\n",
                    fill.sim_order_id, fill.symbol,
                    fill.side == lobe::Side::bid ? "BID" : "ASK",
                    fill.fill_price, fill.fill_qty, fill.fill_ts);
    }
};

// Compile-time check: SimpleMarketMaker satisfies the Strategy concept.
static_assert(lobe::Strategy<SimpleMarketMaker>);

// ============================================================
// Build a synthetic feed simulating a two-asset market.
// ============================================================

/// @brief Generate synthetic market data events.
///
/// Creates a realistic-looking order book for two symbols:
/// - Symbol 1: price around 100.0
/// - Symbol 2: price around 50.0
///
/// The sequence includes adds, executions, and cancels to exercise
/// all event types.
lobe::SyntheticFeed build_demo_feed() {
    lobe::SyntheticFeed feed;
    lobe::Timestamp ts = 1000000000;  // Start at 1 second in nanos.

    // --- Symbol 1: Build initial book around 100.0 ---
    // Bid side
    for (uint64_t i = 0; i < 5; ++i) {
        feed.push(lobe::AddOrder{
            .symbol   = 1,
            .order_id = 100 + i,
            .side     = lobe::Side::bid,
            .price    = 99.50 - static_cast<double>(i) * 0.10,
            .qty      = 100 + i * 50,
            .ts       = ts++,
        });
    }
    // Ask side
    for (uint64_t i = 0; i < 5; ++i) {
        feed.push(lobe::AddOrder{
            .symbol   = 1,
            .order_id = 200 + i,
            .side     = lobe::Side::ask,
            .price    = 100.50 + static_cast<double>(i) * 0.10,
            .qty      = 80 + i * 30,
            .ts       = ts++,
        });
    }

    // --- Symbol 2: Build initial book around 50.0 ---
    for (uint64_t i = 0; i < 3; ++i) {
        feed.push(lobe::AddOrder{
            .symbol   = 2,
            .order_id = 300 + i,
            .side     = lobe::Side::bid,
            .price    = 49.80 - static_cast<double>(i) * 0.05,
            .qty      = 200,
            .ts       = ts++,
        });
    }
    for (uint64_t i = 0; i < 3; ++i) {
        feed.push(lobe::AddOrder{
            .symbol   = 2,
            .order_id = 400 + i,
            .side     = lobe::Side::ask,
            .price    = 50.20 + static_cast<double>(i) * 0.05,
            .qty      = 150,
            .ts       = ts++,
        });
    }

    // --- Phase 2: Market activity ---
    // Delete all original asks on symbol 1, then add aggressive asks
    // below the strategy's bid quotes to trigger fills.

    // Wipe original asks.
    for (uint64_t i = 0; i < 5; ++i) {
        feed.push(lobe::DeleteOrder{
            .symbol = 1, .order_id = 200 + i, .ts = ts++,
        });
    }

    // Aggressive ask arrives at 99.60 — below the strategy's bid of ~99.70.
    feed.push(lobe::AddOrder{
        .symbol   = 1,
        .order_id = 500,
        .side     = lobe::Side::ask,
        .price    = 99.60,
        .qty      = 50,
        .ts       = ts++,
    });

    // Similarly for symbol 2: delete asks, add aggressive ask.
    for (uint64_t i = 0; i < 3; ++i) {
        feed.push(lobe::DeleteOrder{
            .symbol = 2, .order_id = 400 + i, .ts = ts++,
        });
    }
    feed.push(lobe::AddOrder{
        .symbol   = 2,
        .order_id = 501,
        .side     = lobe::Side::ask,
        .price    = 49.60,
        .qty      = 80,
        .ts       = ts++,
    });

    // Now add aggressive bids above the strategy's ask quotes.
    // Delete original bids on symbol 1 first.
    for (uint64_t i = 1; i < 5; ++i) {
        feed.push(lobe::DeleteOrder{
            .symbol = 1, .order_id = 100 + i, .ts = ts++,
        });
    }
    feed.push(lobe::AddOrder{
        .symbol   = 1,
        .order_id = 600,
        .side     = lobe::Side::bid,
        .price    = 100.40,
        .qty      = 60,
        .ts       = ts++,
    });

    // More regular activity to generate further book updates.
    for (uint64_t i = 0; i < 10; ++i) {
        lobe::SymbolId sym = (i % 2 == 0) ? 1 : 2;
        lobe::Side side = (i % 2 == 0) ? lobe::Side::bid : lobe::Side::ask;
        double base = (sym == 1) ? 100.0 : 50.0;

        feed.push(lobe::AddOrder{
            .symbol   = sym,
            .order_id = 700 + i,
            .side     = side,
            .price    = base + (side == lobe::Side::bid ? -0.10 : 0.10),
            .qty      = 100,
            .ts       = ts++,
        });
    }

    // Execute the aggressive ask on symbol 1 to move price.
    feed.push(lobe::ExecuteOrder{
        .symbol   = 1,
        .order_id = 500,
        .qty      = 50,
        .price    = 99.60,
        .ts       = ts++,
    });

    return feed;
}

// ============================================================
// Main
// ============================================================

int main() {
    std::printf("LOBE — Light Order Backtesting Engine\n");
    std::printf("Running demo backtest with synthetic data...\n\n");

    auto feed = build_demo_feed();
    SimpleMarketMaker strategy{.half_spread = 0.30, .quote_qty = 10};

    // Engine<Feed, Strategy, FillModel, LatencyModel>
    // Using defaults: ProbabilisticFillModel, NoLatency.
    lobe::Engine engine(std::move(feed), std::move(strategy));

    engine.run();

    std::printf("\nProcessed %llu events.\n", engine.event_count());
    engine.stats().print_summary();

    if (engine.stats().write_csv("backtest_fills.csv")) {
        std::printf("Fill details written to backtest_fills.csv\n");
    }

    return 0;
}
