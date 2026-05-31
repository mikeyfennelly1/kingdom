#include "kd/Conversation.hpp"

#include <algorithm>

namespace kd {

bool Conversation::hasParticipant(uint64_t userId) const {
  return std::ranges::find(participantIds, userId) != participantIds.end();
}

}  // namespace kd
