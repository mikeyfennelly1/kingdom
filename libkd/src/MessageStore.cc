#include "kd/MessageStore.hpp"

namespace kd {

void MessageStore::add(Message message) {
    messages_.push_back(std::move(message));
}

const std::vector<Message>& MessageStore::getAll() const {
    return messages_;
}

std::vector<Message> MessageStore::findBySender(uint64_t senderId) const {
    std::vector<Message> result;
    std::copy_if(messages_.begin(), messages_.end(), std::back_inserter(result),
                 [senderId](const Message& m) { return m.senderId == senderId; });
    return result;
}

void MessageStore::clear() {
    messages_.clear();
}

void MessageStore::cachePublicKey(uint64_t userId, const std::string& publicKey) {
    publicKeyCache_[userId] = publicKey;
}

std::optional<std::string> MessageStore::getCachedPublicKey(uint64_t userId) const {
    auto it = publicKeyCache_.find(userId);
    if (it == publicKeyCache_.end()) {
        return std::nullopt;
    }
    return it->second;
}

}  // namespace kd
