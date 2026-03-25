/// @file FillModel.h
/// @brief FillModel concept and a probabilistic fill implementation.
///
/// The FillModel determines whether and when a simulated order gets filled.
/// It is consulted by the Engine on each event to check pending sim orders
/// against the current book state.
///
/// ## Upgrade path
///
/// The initial ProbabilisticFillModel uses a simple price-crossing heuristic.
/// To upgrade to queue-position tracking, implement a new type satisfying
/// the FillModel concept — no changes to Engine or Strategy needed.

#pragma once
#include "lobe/Book/LOB.h"
#include "lobe/Strategy/Strategy.h"
#include <concepts>
#include <optional>

namespace lobe {

// ============================================================
// FillModel concept
// ============================================================

/// @brief Concept constraining types that decide simulated order fills.
///
/// A valid FillModel must provide:
/// - `check_fill(order, book, now)`: inspect a pending simulated order
///   against the current book state and return a Fill if the order should
///   be (partially) filled, or std::nullopt otherwise.
///
/// @par Example:
/// @code
/// struct MyFillModel {
///     std::optional<Fill> check_fill(const SimOrder& order,
///                                    const LOB& book,
///                                    Timestamp now) const;
/// };
/// static_assert(FillModel<MyFillModel>);
/// @endcode
template <typename F>
concept FillModel = requires(const F f, const SimOrder& order,
                             const LOB& book, Timestamp now) {
    { f.check_fill(order, book, now) } -> std::same_as<std::optional<Fill>>;
};

// ============================================================
// ProbabilisticFillModel
// ============================================================

/// @brief A simple fill model based on price crossing.
///
/// ## Fill logic
///
/// A simulated limit order is filled when the market price crosses
/// through the order's limit price:
/// - **Bid order**: filled when `best_ask <= order.price`
///   (someone is willing to sell at or below your bid).
/// - **Ask order**: filled when `best_bid >= order.price`
///   (someone is willing to buy at or above your ask).
///
/// All fills are treated as full fills at the order's limit price.
/// This is a simplification — real fills may be partial and at varying
/// prices. The FillModel concept allows swapping in more realistic
/// implementations later.
///
/// ## Limitations
///
/// - No partial fills.
/// - No queue position tracking (your order fills immediately on cross).
/// - Ignores market impact (your order doesn't affect the book).
struct ProbabilisticFillModel {
    /// @brief Check if a simulated order should be filled.
    /// @param order The pending simulated order.
    /// @param book  The current state of the LOB for this symbol.
    /// @param now   The current simulation timestamp.
    /// @return A Fill if the order crosses the market, std::nullopt otherwise.
    [[nodiscard]] std::optional<Fill> check_fill(
            const SimOrder& order,
            const LOB& book,
            Timestamp now) const {
        if (order.type == SimOrderType::cancel) return std::nullopt;

        if (order.side == Side::bid) {
            Price ba = book.best_ask();
            if (ba <= order.price) {
                return Fill{
                    .sim_order_id = order.sim_order_id,
                    .symbol       = order.symbol,
                    .side         = order.side,
                    .fill_price   = order.price,
                    .fill_qty     = order.qty,
                    .fill_ts      = now,
                };
            }
        } else {
            Price bb = book.best_bid();
            if (bb >= order.price) {
                return Fill{
                    .sim_order_id = order.sim_order_id,
                    .symbol       = order.symbol,
                    .side         = order.side,
                    .fill_price   = order.price,
                    .fill_qty     = order.qty,
                    .fill_ts      = now,
                };
            }
        }
        return std::nullopt;
    }
};

static_assert(FillModel<ProbabilisticFillModel>,
              "ProbabilisticFillModel must satisfy FillModel");

} // namespace lobe
