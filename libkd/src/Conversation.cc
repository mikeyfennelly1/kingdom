#include "kd/Conversation.hpp"

#include <algorithm>

namespace kd {

bool Conversation::hasParticipant(uint64_t userId) const {
    return std::find(participantIds.begin(), participantIds.end(), userId) != participantIds.end();
}

}
