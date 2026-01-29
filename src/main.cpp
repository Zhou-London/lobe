
#include "LOB.h"
#include <iostream>
#include <string_view>

#define VERSION 0.1

int main(int argc, char **argv) {
  mose::LOB lob;
  mose::insert_order(lob, mose::Side::bid, 100.123, 10, 0);

  std::cout << lob.pl_table.prices[0] << std::endl;

  return 0;
}
