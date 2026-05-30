#include "kd/Message.hpp"

#include <sstream>

namespace kd {

std::string Message::formatted() const {
  std::ostringstream oss;
  oss << "[" << timestamp << "] User " << senderId << ": " << payload;
  return oss.str();
}

}  // namespace kd
