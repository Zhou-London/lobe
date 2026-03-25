/// @file LatencyModel.h
/// @brief LatencyModel concept and a no-op default implementation.
///
/// The LatencyModel simulates the delay between a strategy submitting
/// an order and that order becoming effective in the market. This is
/// critical for realistic HFT backtesting, where round-trip latencies
/// of microseconds determine profitability.
///
/// ## Current state
///
/// Only `NoLatency` is provided — orders take effect immediately.
/// This is a placeholder; future implementations might sample from
/// measured latency distributions.
///
/// ## Upgrade path
///
/// Implement a new struct satisfying the LatencyModel concept:
/// @code
/// struct GaussianLatency {
///     double mean_ns;
///     double stddev_ns;
///     Timestamp delivery_time(Timestamp send_time) const {
///         return send_time + sample_gaussian(mean_ns, stddev_ns);
///     }
/// };
/// @endcode

#pragma once
#include "lobe/Feed/Event.h"
#include <concepts>

namespace lobe {

// ============================================================
// LatencyModel concept
// ============================================================

/// @brief Concept constraining types that model order delivery latency.
///
/// A valid LatencyModel must provide:
/// - `delivery_time(send_time)`: given the timestamp when the strategy
///   submits an order, return the timestamp when it arrives at the
///   exchange (or takes effect in the simulation).
template <typename L>
concept LatencyModel = requires(const L l, Timestamp send_time) {
    { l.delivery_time(send_time) } -> std::same_as<Timestamp>;
};

// ============================================================
// NoLatency — zero-delay default
// ============================================================

/// @brief A no-op latency model: orders take effect immediately.
///
/// This is the default LatencyModel used when latency simulation is
/// not needed. Orders arrive at the same timestamp they are submitted.
struct NoLatency {
    /// @brief Return the delivery time (same as send time — zero delay).
    /// @param send_time The submission timestamp.
    /// @return send_time unchanged.
    [[nodiscard]] Timestamp delivery_time(Timestamp send_time) const {
        return send_time;
    }
};

static_assert(LatencyModel<NoLatency>, "NoLatency must satisfy LatencyModel");

} // namespace lobe
