/// @file FeedParser.h
/// @brief FeedParser concept and a synthetic feed implementation for testing.
///
/// A FeedParser is the abstraction over exchange data feeds. It converts raw
/// binary data (ITCH, PITCH, etc.) into normalized Event objects. The Engine
/// is templated on FeedParser, so any conforming type can be plugged in with
/// zero virtual-dispatch overhead.
///
/// This file also provides SyntheticFeed, a deterministic event generator
/// useful for testing and demos without real market data.

#pragma once
#include "lobe/Feed/Event.h"
#include <concepts>
#include <cstdint>
#include <vector>

namespace lobe {

// ============================================================
// FeedParser concept
// ============================================================

/// @brief Concept constraining types that can serve as market data feeds.
///
/// A valid FeedParser must provide:
/// - `has_next()`: returns true if more events are available.
/// - `next()`: returns the next Event in chronological order.
///
/// @par Example implementation:
/// @code
/// struct MyITCHParser {
///     bool  has_next() const;
///     Event next();
/// };
/// static_assert(FeedParser<MyITCHParser>);
/// @endcode
template <typename F>
concept FeedParser = requires(F f) {
    { f.has_next() } -> std::convertible_to<bool>;
    { f.next() }     -> std::same_as<Event>;
};

// ============================================================
// SyntheticFeed — deterministic test feed
// ============================================================

/// @brief A deterministic synthetic market data feed for testing.
///
/// Generates a pre-built sequence of events. Useful for:
/// - Unit tests with known expected behavior.
/// - Demos when real ITCH data is not available.
/// - Verifying Engine/Strategy wiring before using real feeds.
///
/// Events are consumed in the order they were added.
class SyntheticFeed {
public:
    /// @brief Add an event to the end of the feed.
    /// @param event The event to enqueue.
    void push(Event event) {
        events_.push_back(std::move(event));
    }

    /// @brief Check if there are remaining events.
    /// @return True if at least one event is available.
    [[nodiscard]] bool has_next() const {
        return cursor_ < events_.size();
    }

    /// @brief Consume and return the next event.
    /// @return The next Event in insertion order.
    /// @pre has_next() must be true.
    Event next() {
        return events_[cursor_++];
    }

    /// @brief Reset the cursor to replay all events from the beginning.
    void reset() { cursor_ = 0; }

    /// @brief Return the total number of events in the feed.
    [[nodiscard]] size_t size() const { return events_.size(); }

private:
    std::vector<Event> events_;  ///< The stored event sequence.
    size_t cursor_ = 0;         ///< Current read position.
};

static_assert(FeedParser<SyntheticFeed>, "SyntheticFeed must satisfy FeedParser");

} // namespace lobe
