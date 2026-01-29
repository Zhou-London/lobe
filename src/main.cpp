
#include "LOB.h"
#include <cassert>
#include <iostream>

#define VERSION 0.1

int main(int argc, char **argv) {
  mose::LOB lob;
  auto id1 = mose::insert_order(lob, mose::Side::bid, 100.123, 10, 0);
  auto id2 = mose::insert_order(lob, mose::Side::bid, 100, 100, 0);
  auto index2 = lob.order_table.find_index[id2];

  assert(lob.order_table.ids.size() == 2 && lob.pl_table.prices.size() == 2);

  mose::remove_order(lob, id1);

  assert(lob.pl_table.order_counts[0] == 0 && lob.pl_table.prices.size() == 2);
  assert(lob.pl_table.prices[lob.order_table.levels[index2]] == float(100));

  return 0;
}
