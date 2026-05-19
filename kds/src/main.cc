#include <spdlog/spdlog.h>

#include <cstdlib>

#include "controller/Controller.hh"

const int defaultPortNumber = 8080;

auto main() -> int {
  try {
    kd::Controller server = kd::configure();
    server.start();

  } catch (const std::exception& e) {
    spdlog::critical("Unhandled exception: {}", e.what());
    return 1;
  }

  return 0;
}
