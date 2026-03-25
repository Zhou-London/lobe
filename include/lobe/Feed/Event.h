/// @file Event.h
/// @brief Normalized market data event types for the LOBE backtesting engine.
///
/// These types form the universal internal representation of market data,
/// decoupled from any specific exchange protocol (ITCH, PITCH, etc.).
/// A FeedParser converts raw protocol messages into these normalized events.

#pragma once
#include <cstdint>
#include <variant>

namespace lobe {

// ============================================================
// Type aliases
// ============================================================

using OrderId   = uint64_t;  ///< Unique order identifier.
using SymbolId  = uint32_t;  ///< Instrument identifier.
using Quantity  = uint64_t;  ///< Order quantity in shares.
using Timestamp = uint64_t;  ///< Nanosecond-resolution timestamp.
using Price     = double;    ///< Price in floating point.

/// @brief Order side: bid (buy) or ask (sell).
enum class Side : uint8_t { bid = 0, ask = 1 };

// ============================================================
// Market data event types
// ============================================================

/// @brief A new order is added to the book.
///
/// Corresponds to ITCH "Add Order" (type 'A'/'F') messages.
struct AddOrder {
    SymbolId  symbol;    ///< Which instrument this order belongs to.
    OrderId   order_id;  ///< Exchange-assigned order ID.
    Side      side;      ///< Bid or ask.
    Price     price;     ///< Limit price.
    Quantity  qty;       ///< Number of shares.
    Timestamp ts;        ///< Exchange timestamp in nanoseconds.
};

/// @brief An existing order is partially or fully cancelled.
///
/// Corresponds to ITCH "Order Cancel" (type 'X') messages.
/// The cancelled quantity is removed from the order.
struct CancelOrder {
    SymbolId  symbol;
    OrderId   order_id;  ///< The order being cancelled.
    Quantity  qty;       ///< Number of shares cancelled.
    Timestamp ts;
};

/// @brief An existing order is fully deleted from the book.
///
/// Corresponds to ITCH "Order Delete" (type 'D') messages.
struct DeleteOrder {
    SymbolId  symbol;
    OrderId   order_id;  ///< The order being removed.
    Timestamp ts;
};

/// @brief An existing order is executed (filled) against an incoming order.
///
/// Corresponds to ITCH "Order Executed" (type 'E'/'C') messages.
struct ExecuteOrder {
    SymbolId  symbol;
    OrderId   order_id;  ///< The resting order that was filled.
    Quantity  qty;       ///< Number of shares executed.
    Price     price;     ///< Execution price (may differ from limit for 'C' type).
    Timestamp ts;
};

/// @brief An existing order is replaced with new price/quantity.
///
/// Corresponds to ITCH "Order Replace" (type 'U') messages.
/// The old order is removed and a new one is created.
struct ReplaceOrder {
    SymbolId  symbol;
    OrderId   old_order_id;  ///< The order being replaced.
    OrderId   new_order_id;  ///< The new order ID.
    Price     price;         ///< New limit price.
    Quantity  qty;           ///< New quantity.
    Timestamp ts;
};

/// @brief A trade message (informational, not tied to a specific resting order).
///
/// Corresponds to ITCH "Trade" (type 'P') messages.
/// Used for statistics; does not directly modify the book.
struct TradeMessage {
    SymbolId  symbol;
    Price     price;
    Quantity  qty;
    Side      aggressor_side;  ///< Side of the aggressive (taker) order.
    Timestamp ts;
};

/// @brief Variant holding any normalized market data event.
///
/// The Engine processes events polymorphically through std::visit.
/// This is the single currency of data flow between Feed and Book layers.
using Event = std::variant<
    AddOrder,
    CancelOrder,
    DeleteOrder,
    ExecuteOrder,
    ReplaceOrder,
    TradeMessage
>;

/// @brief Extract the timestamp from any event type.
/// @param event The event variant.
/// @return The nanosecond timestamp of the event.
inline Timestamp event_timestamp(const Event& event) {
    return std::visit([](const auto& e) { return e.ts; }, event);
}

/// @brief Extract the symbol from any event type.
/// @param event The event variant.
/// @return The symbol identifier.
inline SymbolId event_symbol(const Event& event) {
    return std::visit([](const auto& e) { return e.symbol; }, event);
}

} // namespace lobe
