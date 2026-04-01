#include "EventBus.h"

EventBus& EventBus::getInstance() {
    static EventBus instance;
    return instance;
}

void EventBus::subscribe(EventType type, EventHandler handler) {
    handlers_[type].push_back(handler);
}

void EventBus::publish(const Event& event) {
    auto it = handlers_.find(event.type);
    if (it != handlers_.end()) {
        for (auto& handler : it->second) {
            handler(event);
        }
    }
}