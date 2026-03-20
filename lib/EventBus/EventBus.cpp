#include "EventBus.h"
#include <algorithm>

EventBus& EventBus::getInstance() {
    static EventBus instance;
    return instance;
}

void EventBus::subscribe(EventType type, EventHandler handler) {
    handlers_[type].push_back(handler);
}

void EventBus::unsubscribe(EventType type, EventHandler handler) {
    auto& vec = handlers_[type];
    vec.erase(std::remove(vec.begin(), vec.end(), handler), vec.end());
}

void EventBus::publish(const Event& event) {
    auto it = handlers_.find(event.type);
    if (it != handlers_.end()) {
        for (auto& handler : it->second) {
            handler(event);
        }
    }
}