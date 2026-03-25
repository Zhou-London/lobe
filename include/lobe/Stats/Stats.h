/// @file Stats.h
/// @brief Performance statistics tracking and output.
///
/// Stats collects fill events during a backtest run and produces:
/// - A stdout summary (PnL, trade count, fill rate, etc.)
/// - A CSV file with per-fill detail for external analysis.
///
/// ## PnL calculation
///
/// Position and PnL are tracked per-symbol. Each fill updates the
/// running position and realized PnL. Mark-to-market (unrealized)
/// PnL requires a final mid-price snapshot.

#pragma once
#include "lobe/Strategy/Strategy.h"
#include <cstdio>
#include <fstream>
#include <string>
#include <unordered_map>
#include <vector>

namespace lobe {

/// @brief Per-symbol position and PnL tracker.
struct SymbolStats {
    int64_t  position = 0;       ///< Net position (positive = long, negative = short).
    double   realized_pnl = 0.0; ///< Cumulative realized PnL.
    double   avg_cost = 0.0;     ///< Volume-weighted average cost of current position.
    uint64_t buy_count = 0;      ///< Number of buy fills.
    uint64_t sell_count = 0;     ///< Number of sell fills.
    Quantity buy_volume = 0;     ///< Total shares bought.
    Quantity sell_volume = 0;    ///< Total shares sold.
};

/// @brief Collects and reports backtest performance statistics.
///
/// @par Usage:
/// @code
/// Stats stats;
/// // ... during backtest:
/// stats.record_fill(fill);
/// // ... after backtest:
/// stats.print_summary();
/// stats.write_csv("results.csv");
/// @endcode
class Stats {
public:
    /// @brief Record a simulated fill.
    ///
    /// Updates per-symbol position and realized PnL. The PnL model
    /// uses average-cost: when a fill reduces the position (or flips
    /// it), the difference between fill price and average cost is
    /// realized.
    ///
    /// @param fill The fill event from the simulation layer.
    void record_fill(const Fill& fill) {
        fills_.push_back(fill);
        auto& s = symbol_stats_[fill.symbol];

        int64_t signed_qty = (fill.side == Side::bid)
            ? static_cast<int64_t>(fill.fill_qty)
            : -static_cast<int64_t>(fill.fill_qty);

        if (fill.side == Side::bid) {
            s.buy_count++;
            s.buy_volume += fill.fill_qty;
        } else {
            s.sell_count++;
            s.sell_volume += fill.fill_qty;
        }

        int64_t old_pos = s.position;
        s.position += signed_qty;

        // Realized PnL when reducing position.
        if ((old_pos > 0 && signed_qty < 0) || (old_pos < 0 && signed_qty > 0)) {
            int64_t closed = std::min(std::abs(old_pos), std::abs(signed_qty));
            double pnl_per_share = (old_pos > 0)
                ? (fill.fill_price - s.avg_cost)
                : (s.avg_cost - fill.fill_price);
            s.realized_pnl += pnl_per_share * static_cast<double>(closed);
        }

        // Update average cost when adding to position.
        if (s.position != 0 &&
            ((old_pos >= 0 && signed_qty > 0) || (old_pos <= 0 && signed_qty < 0))) {
            double total_cost = s.avg_cost * std::abs(static_cast<double>(old_pos))
                              + fill.fill_price * static_cast<double>(fill.fill_qty);
            s.avg_cost = total_cost / std::abs(static_cast<double>(s.position));
        } else if (s.position == 0) {
            s.avg_cost = 0.0;
        }
    }

    /// @brief Print a human-readable summary to stdout.
    void print_summary() const {
        std::printf("\n=== LOBE Backtest Summary ===\n");
        std::printf("Total fills: %zu\n", fills_.size());

        double total_pnl = 0.0;
        for (const auto& [sym, s] : symbol_stats_) {
            std::printf("\n  Symbol %u:\n", sym);
            std::printf("    Buys:  %llu fills, %llu shares\n", s.buy_count, s.buy_volume);
            std::printf("    Sells: %llu fills, %llu shares\n", s.sell_count, s.sell_volume);
            std::printf("    Net position: %lld\n", s.position);
            std::printf("    Realized PnL: %.2f\n", s.realized_pnl);
            total_pnl += s.realized_pnl;
        }

        std::printf("\n  Total realized PnL: %.2f\n", total_pnl);
        std::printf("============================\n\n");
    }

    /// @brief Write per-fill detail to a CSV file.
    /// @param path Output file path.
    /// @return True if the file was written successfully.
    bool write_csv(const std::string& path) const {
        std::ofstream out(path);
        if (!out.is_open()) return false;

        out << "sim_order_id,symbol,side,fill_price,fill_qty,fill_ts\n";
        for (const auto& f : fills_) {
            out << f.sim_order_id << ","
                << f.symbol << ","
                << (f.side == Side::bid ? "bid" : "ask") << ","
                << f.fill_price << ","
                << f.fill_qty << ","
                << f.fill_ts << "\n";
        }
        return true;
    }

    /// @brief Get per-symbol stats (const).
    [[nodiscard]] const auto& symbol_stats() const { return symbol_stats_; }

    /// @brief Get all recorded fills (const).
    [[nodiscard]] const auto& fills() const { return fills_; }

private:
    std::vector<Fill> fills_;                              ///< All recorded fills.
    std::unordered_map<SymbolId, SymbolStats> symbol_stats_; ///< Per-symbol stats.
};

} // namespace lobe
