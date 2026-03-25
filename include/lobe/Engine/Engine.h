/// @file Engine.h
/// @brief The core backtesting event loop.
///
/// Engine is a class template parameterized on four concepts:
/// - `FeedParser`: supplies normalized market data events.
/// - `Strategy`:   user trading logic.
/// - `FillModel`:  determines when simulated orders fill.
/// - `LatencyModel`: delays order delivery (default: NoLatency).
///
/// All four are compile-time type parameters — no virtual dispatch.
/// The Engine runs a single-threaded event loop that processes events
/// in strict timestamp order.
///
/// ## Event loop pseudocode
///
/// @code
/// while (feed.has_next()):
///     event = feed.next()
///     books.apply(event)                   // update LOB
///     check_pending_fills(event.ts)        // check sim orders
///     strategy.on_book_update(sym, book, event, submitter)
///     collect_new_orders(submitter, event.ts)
/// print_summary()
/// @endcode

#pragma once
#include "lobe/Book/BookManager.h"
#include "lobe/Feed/FeedParser.h"
#include "lobe/Sim/FillModel.h"
#include "lobe/Sim/LatencyModel.h"
#include "lobe/Stats/Stats.h"
#include "lobe/Strategy/Strategy.h"
#include <vector>

namespace lobe {

/// @brief The backtesting engine orchestrating feed, book, strategy, and sim.
///
/// @tparam Feed  A type satisfying FeedParser.
/// @tparam Strat A type satisfying Strategy.
/// @tparam FM    A type satisfying FillModel (default: ProbabilisticFillModel).
/// @tparam LM    A type satisfying LatencyModel (default: NoLatency).
///
/// ## Ownership
///
/// Engine takes ownership of the Feed and Strategy by value. The FillModel
/// and LatencyModel are also owned by value (they are typically small,
/// stateless objects).
///
/// ## Thread safety
///
/// Engine is single-threaded by design. All processing happens in `run()`.
template <FeedParser Feed, Strategy Strat,
          FillModel FM = ProbabilisticFillModel,
          LatencyModel LM = NoLatency>
class Engine {
public:
    /// @brief Construct the engine with all components.
    /// @param feed      Market data source.
    /// @param strategy  Trading strategy.
    /// @param fill_model Fill simulation model.
    /// @param latency_model Latency simulation model.
    Engine(Feed feed, Strat strategy, FM fill_model = {}, LM latency_model = {})
        : feed_(std::move(feed))
        , strategy_(std::move(strategy))
        , fill_model_(std::move(fill_model))
        , latency_model_(std::move(latency_model))
    {}

    /// @brief Run the backtest to completion.
    ///
    /// Processes all events from the feed in order, updating the book,
    /// checking fills, and invoking strategy callbacks. After the feed
    /// is exhausted, remaining pending orders are checked one final time.
    void run() {
        while (feed_.has_next()) {
            Event event = feed_.next();
            Timestamp now = event_timestamp(event);
            SymbolId sym = event_symbol(event);

            // 1. Apply market data to the LOB.
            books_.apply(event);

            // 2. Check pending simulated orders for fills.
            check_fills(now);

            // 3. Notify strategy of the book update.
            const LOB& book = books_.get(sym);
            strategy_.on_book_update(sym, book, event, submitter_);

            // 4. Collect any orders the strategy submitted.
            collect_orders(now);

            event_count_++;
        }

        // Final fill check for any remaining pending orders.
        if (!pending_orders_.empty()) {
            // Use the last known timestamp.
            Timestamp final_ts = event_count_ > 0 ? last_ts_ : 0;
            check_fills(final_ts);
        }
    }

    /// @brief Get the performance statistics collected during the run.
    /// @return Const reference to the Stats object.
    [[nodiscard]] const Stats& stats() const { return stats_; }

    /// @brief Get mutable reference to stats (for custom post-processing).
    [[nodiscard]] Stats& stats() { return stats_; }

    /// @brief Get the BookManager (all LOBs).
    [[nodiscard]] const BookManager& books() const { return books_; }

    /// @brief Get the number of events processed.
    [[nodiscard]] uint64_t event_count() const { return event_count_; }

private:
    Feed  feed_;                            ///< Market data source.
    Strat strategy_;                        ///< Trading strategy.
    FM    fill_model_;                      ///< Fill simulation.
    LM    latency_model_;                   ///< Latency simulation.

    BookManager    books_;                  ///< Per-symbol LOBs.
    Stats          stats_;                  ///< Performance tracker.
    OrderSubmitter submitter_;              ///< Collects strategy orders.
    std::vector<SimOrder> pending_orders_;  ///< Orders waiting for fill.

    uint64_t  event_count_ = 0;            ///< Total events processed.
    Timestamp last_ts_ = 0;                ///< Most recent event timestamp.

    /// @brief Check all pending simulated orders for fills.
    /// @param now Current simulation time.
    void check_fills(Timestamp now) {
        last_ts_ = now;
        auto it = pending_orders_.begin();
        while (it != pending_orders_.end()) {
            // Only check orders that have "arrived" per the latency model.
            Timestamp arrive_ts = latency_model_.delivery_time(it->submit_ts);
            if (arrive_ts > now) {
                ++it;
                continue;
            }

            const LOB& book = books_.get(it->symbol);
            auto fill = fill_model_.check_fill(*it, book, now);
            if (fill.has_value()) {
                stats_.record_fill(*fill);
                strategy_.on_fill(*fill);
                it = pending_orders_.erase(it);
            } else {
                ++it;
            }
        }
    }

    /// @brief Drain orders from the submitter and add to pending list.
    /// @param now Current simulation time (used as submit timestamp).
    void collect_orders(Timestamp now) {
        auto orders = submitter_.drain();
        for (auto& order : orders) {
            order.submit_ts = now;
            if (order.type == SimOrderType::cancel) {
                // Remove the corresponding pending order.
                auto it = std::find_if(pending_orders_.begin(), pending_orders_.end(),
                    [&](const SimOrder& o) { return o.sim_order_id == order.sim_order_id; });
                if (it != pending_orders_.end()) {
                    pending_orders_.erase(it);
                }
            } else {
                pending_orders_.push_back(std::move(order));
            }
        }
    }
};

} // namespace lobe
