/// @file LOB.h
/// @brief Limit Order Book implementation using struct-of-arrays layout.
///
/// The LOB maintains a full depth-of-book for a single instrument.
/// It is rebuilt from a stream of normalized Event messages (AddOrder,
/// CancelOrder, DeleteOrder, ExecuteOrder, ReplaceOrder).
///
/// ## Memory layout
///
/// The book uses a struct-of-arrays (SoA) pattern for cache efficiency:
/// each field (price, quantity, etc.) is stored in a contiguous vector.
/// A free-list enables O(1) slot reuse without compaction.
///
/// ## FIFO ordering
///
/// Orders at the same price level are maintained in FIFO order via
/// a doubly-linked list threaded through the OrderTable.

#pragma once
#include "lobe/Feed/Event.h"
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <limits>
#include <vector>

namespace lobe {

/// @brief Sentinel value indicating an invalid or empty index.
constexpr uint64_t INVALID = std::numeric_limits<uint64_t>::max();

/// @brief Internal index into the OrderTable arrays.
using OrderIdx = uint64_t;

/// @brief Internal index into the PriceLevelTable arrays.
using LevelIdx = uint64_t;

// ============================================================
// OrderTable — SoA storage for individual orders
// ============================================================

/// @brief Struct-of-arrays table storing all orders in the book.
///
/// Each order occupies a row across parallel vectors. Removed orders
/// are recycled via a singly-linked free list (`free_head` / `free_next`)
/// to avoid vector growth and fragmentation.
///
/// Orders at the same price level are doubly-linked (`prev` / `next`)
/// to maintain FIFO priority.
struct OrderTable {
    std::vector<OrderId>  ids;         ///< Exchange order ID for each slot.
    std::vector<Quantity> quantities;  ///< Remaining quantity.
    std::vector<Price>    prices;      ///< Limit price.
    std::vector<Side>     sides;       ///< Bid or ask.
    std::vector<Timestamp> timestamps; ///< Order arrival time.
    std::vector<LevelIdx> levels;      ///< Index of the price level this order belongs to.
    std::vector<OrderIdx> prev;        ///< Previous order at same price level (FIFO chain).
    std::vector<OrderIdx> next;        ///< Next order at same price level (FIFO chain).
    std::vector<OrderIdx> free_next;   ///< Next free slot (free-list chain).

    /// @brief Maps OrderId -> OrderIdx for O(1) lookup.
    ///
    /// Indexed by OrderId. Entries set to INVALID when the order is removed.
    /// Grows automatically as new OrderIds are seen.
    std::vector<OrderIdx> find_index;

    /// @brief Head of the free-list. INVALID if no free slots.
    OrderIdx free_head = INVALID;

    /// @brief Allocate a slot for a new order.
    ///
    /// Reuses a free slot if available; otherwise appends a new row.
    /// @return The OrderIdx of the allocated slot.
    OrderIdx insert() {
        OrderIdx index;
        if (free_head != INVALID) {
            index = free_head;
            free_head = free_next[index];
        } else {
            index = static_cast<OrderIdx>(ids.size());
            ids.push_back({});
            quantities.push_back(0);
            prices.push_back(0.0);
            sides.push_back(Side::bid);
            timestamps.push_back(0);
            levels.push_back(INVALID);
            prev.push_back(INVALID);
            next.push_back(INVALID);
            free_next.push_back(INVALID);
        }
        return index;
    }

    /// @brief Ensure find_index is large enough for the given OrderId.
    /// @param order_id The OrderId that needs to be addressable.
    void ensure_find_index(OrderId order_id) {
        if (order_id >= find_index.size()) {
            find_index.resize(order_id + 1, INVALID);
        }
    }

    /// @brief Release a slot back to the free list.
    /// @param index The OrderIdx to free.
    void free_slot(OrderIdx index) {
        ids[index] = {};
        quantities[index] = 0;
        levels[index] = INVALID;
        prev[index] = INVALID;
        next[index] = INVALID;
        free_next[index] = free_head;
        free_head = index;
    }
};

// ============================================================
// PriceLevelTable — SoA storage for aggregated price levels
// ============================================================

/// @brief Struct-of-arrays table storing aggregated price levels.
///
/// Each row represents a unique (price, side) combination. Orders at
/// the same level are linked into a FIFO chain via `heads` / `tails`.
struct PriceLevelTable {
    std::vector<Price>    prices;       ///< Price value of each level.
    std::vector<Side>     sides;        ///< Bid or ask.
    std::vector<Quantity> quantities;   ///< Total remaining quantity at this level.
    std::vector<uint32_t> order_counts; ///< Number of live orders at this level.
    std::vector<OrderIdx> heads;        ///< First order in the FIFO chain.
    std::vector<OrderIdx> tails;        ///< Last order in the FIFO chain.

    /// @brief Append a new price level.
    /// @return The LevelIdx of the new level.
    LevelIdx insert() {
        LevelIdx index = static_cast<LevelIdx>(prices.size());
        prices.push_back(0.0);
        sides.push_back(Side::ask);
        quantities.push_back(0);
        order_counts.push_back(0);
        heads.push_back(INVALID);
        tails.push_back(INVALID);
        return index;
    }

    /// @brief Find the level index for a given (price, side) pair.
    /// @return The LevelIdx, or INVALID if not found.
    LevelIdx find(Price price, Side side) const {
        for (LevelIdx i = 0; i < prices.size(); ++i) {
            if (prices[i] == price && sides[i] == side) {
                return i;
            }
        }
        return INVALID;
    }
};

// ============================================================
// LOB — the Limit Order Book
// ============================================================

/// @brief A full-depth Limit Order Book for a single instrument.
///
/// Maintains order-level detail reconstructed from exchange messages.
/// Use `apply()` to feed normalized events; use `best_bid()` / `best_ask()`
/// to query the top of book.
struct LOB {
    OrderTable      order_table;  ///< All individual orders.
    PriceLevelTable pl_table;     ///< Aggregated price levels.

    // --------------------------------------------------------
    // Core operations
    // --------------------------------------------------------

    /// @brief Add a new order to the book.
    /// @param order_id Exchange-assigned order ID.
    /// @param side     Bid or ask.
    /// @param price    Limit price.
    /// @param qty      Order quantity.
    /// @param ts       Timestamp.
    void add_order(OrderId order_id, Side side, Price price, Quantity qty, Timestamp ts) {
        LevelIdx level_idx = pl_table.find(price, side);
        if (level_idx == INVALID) {
            level_idx = pl_table.insert();
            pl_table.prices[level_idx] = price;
            pl_table.sides[level_idx] = side;
        }

        auto oidx = order_table.insert();
        order_table.ensure_find_index(order_id);
        order_table.ids[oidx] = order_id;
        order_table.find_index[order_id] = oidx;
        order_table.quantities[oidx] = qty;
        order_table.prices[oidx] = price;
        order_table.sides[oidx] = side;
        order_table.timestamps[oidx] = ts;
        order_table.levels[oidx] = level_idx;

        // Append to FIFO chain at this price level.
        if (pl_table.heads[level_idx] == INVALID) {
            pl_table.heads[level_idx] = oidx;
            pl_table.tails[level_idx] = oidx;
        } else {
            OrderIdx old_tail = pl_table.tails[level_idx];
            order_table.next[old_tail] = oidx;
            order_table.prev[oidx] = old_tail;
            pl_table.tails[level_idx] = oidx;
        }

        pl_table.quantities[level_idx] += qty;
        pl_table.order_counts[level_idx]++;
    }

    /// @brief Remove an order entirely from the book.
    /// @param order_id The order to remove.
    void delete_order(OrderId order_id) {
        if (order_id >= order_table.find_index.size()) return;
        auto oidx = order_table.find_index[order_id];
        if (oidx == INVALID) return;

        auto level_idx = order_table.levels[oidx];
        auto qty = order_table.quantities[oidx];
        unlink_order(oidx, level_idx);

        pl_table.quantities[level_idx] -= qty;
        pl_table.order_counts[level_idx]--;

        order_table.find_index[order_id] = INVALID;
        order_table.free_slot(oidx);
    }

    /// @brief Reduce an order's quantity (partial cancel).
    /// @param order_id The order to reduce.
    /// @param cancel_qty The number of shares to cancel.
    void cancel_order(OrderId order_id, Quantity cancel_qty) {
        if (order_id >= order_table.find_index.size()) return;
        auto oidx = order_table.find_index[order_id];
        if (oidx == INVALID) return;

        auto level_idx = order_table.levels[oidx];
        auto& remaining = order_table.quantities[oidx];
        Quantity actual = std::min(cancel_qty, remaining);
        remaining -= actual;
        pl_table.quantities[level_idx] -= actual;

        if (remaining == 0) {
            unlink_order(oidx, level_idx);
            pl_table.order_counts[level_idx]--;
            order_table.find_index[order_id] = INVALID;
            order_table.free_slot(oidx);
        }
    }

    /// @brief Execute (fill) shares from a resting order.
    /// @param order_id The resting order being filled.
    /// @param exec_qty Number of shares executed.
    void execute_order(OrderId order_id, Quantity exec_qty) {
        // Same mechanics as cancel: reduce qty, remove if fully filled.
        cancel_order(order_id, exec_qty);
    }

    /// @brief Replace an existing order with a new one.
    /// @param old_id The order to remove.
    /// @param new_id The new order ID.
    /// @param price  New price.
    /// @param qty    New quantity.
    /// @param ts     Timestamp.
    void replace_order(OrderId old_id, OrderId new_id, Price price, Quantity qty, Timestamp ts) {
        Side side = Side::bid;
        if (old_id < order_table.find_index.size()) {
            auto oidx = order_table.find_index[old_id];
            if (oidx != INVALID) {
                side = order_table.sides[oidx];
            }
        }
        delete_order(old_id);
        add_order(new_id, side, price, qty, ts);
    }

    // --------------------------------------------------------
    // Event dispatch
    // --------------------------------------------------------

    /// @brief Apply a normalized market data event to update the book.
    ///
    /// This is the primary interface between the Feed layer and the Book.
    /// The Engine calls this for every event to keep the LOB in sync with
    /// the exchange's state.
    ///
    /// @param event Any Event variant (AddOrder, CancelOrder, etc.).
    void apply(const Event& event) {
        std::visit([this](const auto& e) { apply_impl(e); }, event);
    }

    // --------------------------------------------------------
    // Queries
    // --------------------------------------------------------

    /// @brief Get the best (highest) bid price.
    /// @return The best bid price, or 0.0 if no bids exist.
    [[nodiscard]] Price best_bid() const {
        Price best = 0.0;
        for (LevelIdx i = 0; i < pl_table.prices.size(); ++i) {
            if (pl_table.sides[i] == Side::bid &&
                pl_table.order_counts[i] > 0 &&
                pl_table.prices[i] > best) {
                best = pl_table.prices[i];
            }
        }
        return best;
    }

    /// @brief Get the best (lowest) ask price.
    /// @return The best ask price, or a very large value if no asks exist.
    [[nodiscard]] Price best_ask() const {
        Price best = std::numeric_limits<Price>::max();
        for (LevelIdx i = 0; i < pl_table.prices.size(); ++i) {
            if (pl_table.sides[i] == Side::ask &&
                pl_table.order_counts[i] > 0 &&
                pl_table.prices[i] < best) {
                best = pl_table.prices[i];
            }
        }
        return best;
    }

    /// @brief Get the total quantity at the best bid.
    [[nodiscard]] Quantity best_bid_qty() const {
        Price bb = best_bid();
        for (LevelIdx i = 0; i < pl_table.prices.size(); ++i) {
            if (pl_table.sides[i] == Side::bid && pl_table.prices[i] == bb) {
                return pl_table.quantities[i];
            }
        }
        return 0;
    }

    /// @brief Get the total quantity at the best ask.
    [[nodiscard]] Quantity best_ask_qty() const {
        Price ba = best_ask();
        for (LevelIdx i = 0; i < pl_table.prices.size(); ++i) {
            if (pl_table.sides[i] == Side::ask && pl_table.prices[i] == ba) {
                return pl_table.quantities[i];
            }
        }
        return 0;
    }

    /// @brief Get the mid-price (average of best bid and best ask).
    /// @return The mid-price, or 0.0 if either side is empty.
    [[nodiscard]] Price mid_price() const {
        Price bb = best_bid();
        Price ba = best_ask();
        if (bb == 0.0 || ba == std::numeric_limits<Price>::max()) return 0.0;
        return (bb + ba) / 2.0;
    }

private:
    /// @brief Unlink an order from its price level's FIFO chain.
    void unlink_order(OrderIdx oidx, LevelIdx level_idx) {
        auto prev_idx = order_table.prev[oidx];
        auto next_idx = order_table.next[oidx];

        if (prev_idx != INVALID)
            order_table.next[prev_idx] = next_idx;
        else
            pl_table.heads[level_idx] = next_idx;

        if (next_idx != INVALID)
            order_table.prev[next_idx] = prev_idx;
        else
            pl_table.tails[level_idx] = prev_idx;
    }

    // -- apply_impl overloads for each event type --

    void apply_impl(const AddOrder& e) {
        add_order(e.order_id, e.side, e.price, e.qty, e.ts);
    }

    void apply_impl(const CancelOrder& e) {
        cancel_order(e.order_id, e.qty);
    }

    void apply_impl(const DeleteOrder& e) {
        delete_order(e.order_id);
    }

    void apply_impl(const ExecuteOrder& e) {
        execute_order(e.order_id, e.qty);
    }

    void apply_impl(const ReplaceOrder& e) {
        replace_order(e.old_order_id, e.new_order_id, e.price, e.qty, e.ts);
    }

    void apply_impl(const TradeMessage&) {
        // Trade messages are informational; they don't modify the book.
    }
};

} // namespace lobe
