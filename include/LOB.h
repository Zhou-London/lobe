#pragma once
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
  container::vector<OrderIdx> free_next;
  container::vector<OrderIdx> find_index;
  OrderIdx free_head = UINT64_MAX;

  OrderIdx insert() {
    OrderIdx index;
    find_index.push_back(UINT64_MAX);

    // Option 1: Reuse free space
    if (free_head != UINT64_MAX) {
      index = free_head;
      free_head = free_next[index];
    } else {
      // Option 2: Insert new row
      index = static_cast<OrderIdx>(ids.size());
      ids.push_back({});
      quantities.push_back(0);
      timestamps.push_back(0);
      levels.push_back(UINT64_MAX);
      prev.push_back(UINT64_MAX);
      next.push_back(UINT64_MAX);
      free_next.push_back(UINT64_MAX);
    }
    return index;
  }

  void remove(OrderId id) {
    auto index = find_index[id];
    find_index[id] = UINT64_MAX;

    ids[index] = {};
    quantities[index] = 0;
    timestamps[index] = 0;
    levels[index] = UINT64_MAX;
    prev[index] = UINT64_MAX;
    next[index] = UINT64_MAX;
    free_next[index] = free_head;
    free_head = index;
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
    heads.push_back(UINT64_MAX);
    tails.push_back(UINT64_MAX);
    return index;
  }
};

struct LOB {
  OrderTable order_table;
  PriceLevelTable pl_table;
  OrderId next_order_id = 1;
};

inline OrderId insert_order(LOB &lob, Side side, Price price, Quantity qty,
                            Timestamp ts) {
  LevelIdx level_idx = UINT64_MAX;
  for (LevelIdx i = 0; i < lob.pl_table.prices.size(); ++i) {
    if (lob.pl_table.prices[i] == price && lob.pl_table.sides[i] == side) {
      level_idx = i;
      break;
    }
  }

  if (level_idx == UINT64_MAX) {
    level_idx = lob.pl_table.insert();
    lob.pl_table.prices[level_idx] = price;
    lob.pl_table.sides[level_idx] = side;
  }

  auto order_id = lob.next_order_id++;
  auto order_index = lob.order_table.insert();
  lob.order_table.ids[order_index] = order_id;
  lob.order_table.find_index[order_id] = order_index;
  lob.order_table.quantities[order_index] = qty;
  lob.order_table.timestamps[order_index] = ts;
  lob.order_table.levels[order_index] = level_idx;

  if (lob.pl_table.heads[level_idx] == UINT64_MAX) {
    lob.pl_table.heads[level_idx] = order_index;
    lob.pl_table.tails[level_idx] = order_index;
  } else {
    OrderIdx old_tail = lob.pl_table.tails[level_idx];
    lob.order_table.next[old_tail] = order_index;
    lob.order_table.prev[order_index] = old_tail;
    lob.pl_table.tails[level_idx] = order_index;
  }

  lob.pl_table.quantities[level_idx] += qty;
  lob.pl_table.order_counts[level_idx]++;
  return order_id;
}

inline void remove_order(LOB &lob, OrderId order_id) {
  auto order_index = lob.order_table.find_index[order_id];
  auto level_idx = lob.order_table.levels[order_index];
  auto qty = lob.order_table.quantities[order_index];

  auto prev_idx = lob.order_table.prev[order_index];
  auto next_idx = lob.order_table.next[order_index];

  if (prev_idx != UINT64_MAX)
    lob.order_table.next[prev_idx] = next_idx;
  else
    lob.pl_table.heads[level_idx] = next_idx;

  if (next_idx != UINT64_MAX)
    lob.order_table.prev[next_idx] = prev_idx;
  else
    lob.pl_table.tails[level_idx] = prev_idx;

  lob.pl_table.quantities[level_idx] -= qty;
  lob.pl_table.order_counts[level_idx]--;

  lob.order_table.remove(order_id);
}

} // namespace mose
