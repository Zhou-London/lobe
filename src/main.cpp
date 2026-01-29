
#include "LOB.h"
#include <cassert>
#include <cstdint>
#include <iostream>
#include <ranges>

#define VERSION 0.1

int main(int argc, char **argv) {
  mose::LOB lob;
  for (auto i : std::ranges::views::iota(uint64_t{1000}, uint64_t{2000})) {
    mose::insert_order(lob, mose::Side::bid, float(i), (i + 500 - 1) / 500, i);
  }

  assert(lob.order_table.ids.size() == 1000);
  assert(lob.pl_table.prices[0] == float(1000));

  return 0;
}
