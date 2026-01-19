
#include <iostream>
#include <string_view>

#define VERSION 0.1

int main(int argc, char **argv) {
  if (argc > 1 && std::string_view(argv[1]) == "--version")
    std::cout << "mose version: " << VERSION << std::endl;

  return 0;
}