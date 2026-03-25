/// @file BookManager.h
/// @brief Multi-asset order book registry.
///
/// BookManager maps SymbolId -> LOB, providing the Engine with a single
/// entry point for applying events across multiple instruments.

#pragma once
#include "lobe/Book/LOB.h"
#include <unordered_map>

namespace lobe {

/// @brief Registry of per-instrument Limit Order Books.
///
/// Lazily creates a new LOB the first time a SymbolId is seen.
/// This allows the Engine to handle an arbitrary number of instruments
/// without pre-configuration.
///
/// @par Example usage:
/// @code
/// BookManager books;
/// books.apply(some_event);  // auto-creates LOB for the event's symbol
/// const LOB& book = books.get(symbol_id);
/// @endcode
class BookManager {
public:
    /// @brief Apply a normalized event to the appropriate book.
    ///
    /// Extracts the SymbolId from the event, looks up (or creates) the
    /// corresponding LOB, and forwards the event.
    ///
    /// @param event The market data event to apply.
    void apply(const Event& event) {
        SymbolId sym = event_symbol(event);
        books_[sym].apply(event);
    }

    /// @brief Get a const reference to the LOB for a given symbol.
    /// @param symbol The instrument to look up.
    /// @return Reference to the LOB. Creates an empty one if not found.
    [[nodiscard]] const LOB& get(SymbolId symbol) const {
        auto it = books_.find(symbol);
        if (it != books_.end()) return it->second;
        return empty_book_;
    }

    /// @brief Get a mutable reference to the LOB for a given symbol.
    /// @param symbol The instrument to look up.
    /// @return Reference to the LOB (created if not present).
    LOB& get_mut(SymbolId symbol) {
        return books_[symbol];
    }

    /// @brief Check if a book exists for the given symbol.
    [[nodiscard]] bool has(SymbolId symbol) const {
        return books_.contains(symbol);
    }

    /// @brief Return the number of tracked instruments.
    [[nodiscard]] size_t size() const { return books_.size(); }

    /// @brief Provide iteration over all (symbol, LOB) pairs.
    [[nodiscard]] auto begin() const { return books_.begin(); }
    [[nodiscard]] auto end()   const { return books_.end(); }

private:
    std::unordered_map<SymbolId, LOB> books_;  ///< Symbol -> LOB mapping.
    static inline const LOB empty_book_{};     ///< Returned for unknown symbols.
};

} // namespace lobe
