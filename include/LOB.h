#pragma once

#include <cstdint>
#include <map>
#include <unordered_map>
#include <vector>
namespace lob {

struct Order {
  uint64_t order_id;
  uint64_t price;
  uint32_t quantity;
  bool is_buy;

  Order() = default;
  Order(uint64_t id, uint64_t p, uint32_t qty, bool buy)
      : order_id(id), price(p), quantity(qty), is_buy(buy) {}
};

struct PriceLevel {
  uint64_t price;
  uint32_t total_quantity;
  std::vector<uint64_t> order_ids; // fifo orders

  PriceLevel() : price(0), total_quantity(0) {}
  explicit PriceLevel(uint64_t p) : price(p), total_quantity(0) {}
};

class OrderBook {
public:
  OrderBook() { orders_.reserve(10000); }

  /*
  ! S
  */
  bool add_order(uint64_t order_id, uint64_t price, uint32_t quantity,
                 bool is_buy) {

    if (orders_.contains(order_id)) [[unlikely]] {
      return false; // check duplicate
    }

    orders_.emplace(order_id, Order{order_id, price, quantity, is_buy});

    auto &levels = is_buy ? bids_ : asks_;
    auto [it, inserted] = levels.try_emplace(price, price);

    it->second.order_ids.push_back(order_id);
    it->second.total_quantity += quantity;

    return true;
  }

  /*
  ! S
  */
  bool remove_order(uint64_t order_id) {

    auto order_it = orders_.find(order_id);
    if (order_it == orders_.end()) [[unlikely]] {
      return false;
    }
    const auto &order = order_it->second;

    auto &levels = order.is_buy ? bids_ : asks_;
    auto level_it = levels.find(order.price);
    if (level_it == levels.end()) [[unlikely]] {
      orders_.erase(order_it); // system inconsistency, extremely unlikely
      return false;
    }
    auto &level = level_it->second;

    auto &ids = level.order_ids;
    for (auto it = ids.begin(); it != ids.end(); ++it) {
      if (*it == order_id) {
        ids.erase(it);
        break;
      }
    }

    level.total_quantity -= order.quantity;
    if (ids.empty()) {
      levels.erase(level_it);
    }

    orders_.erase(order_it);
    return true;
  }

private:
  std::unordered_map<uint64_t, Order> orders_;

  std::map<uint64_t, PriceLevel> bids_;
  std::map<uint64_t, PriceLevel> asks_;
};

} // namespace lob