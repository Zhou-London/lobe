#pragma once
#include <climits>
#include <cstdint>
#include <vector>
#define ORDERS_NUM 10000

namespace mose {

namespace container {
template <typename T> using vector = std::vector<T>;
}

enum class Side : bool { bid = true, ask = false };
enum class OrderType : uint8_t { limit = 0, market = 1, other = 2 };

using OrderId = uint64_t;
using Quantity = uint64_t;
using Timestamp = uint64_t;
using LevelIdx = uint64_t;
using OrderIdx = uint64_t;

using Price = float;

struct OrderTable {
  container::vector<OrderId> ids;
  container::vector<Quantity> quantities;
  container::vector<Timestamp> timestamps;
  container::vector<LevelIdx> levels;
  container::vector<OrderIdx> prev;
  container::vector<OrderIdx> next;

  OrderIdx insert() {
    OrderIdx index = static_cast<OrderIdx>(ids.size());
    ids.push_back({});
    quantities.push_back(0);
    timestamps.push_back(0);
    levels.push_back(INT_MAX);
    prev.push_back(INT_MAX);
    next.push_back(INT_MAX);
    return index;
  }
};

struct PriceLevelTable {
  container::vector<Price> prices;
  container::vector<Side> sides;
  container::vector<Quantity> quantities;
  container::vector<uint32_t> order_counts;
  container::vector<OrderIdx> heads;
  container::vector<OrderIdx> tails;

  LevelIdx insert() {
    LevelIdx index = static_cast<LevelIdx>(prices.size());
    prices.push_back(0);
    sides.push_back(Side::ask);
    quantities.push_back(0);
    order_counts.push_back(0);
    heads.push_back(INT_MAX);
    tails.push_back(INT_MAX);
    return index;
  }
};

struct LOB {
  OrderTable order_table;
  PriceLevelTable pl_table;
};



} // namespace mose
