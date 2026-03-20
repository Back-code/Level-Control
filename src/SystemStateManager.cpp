#include "SystemStateManager.h"
#include "DebugLogger.h"

SystemStateManager& SystemStateManager::getInstance() {
    static SystemStateManager instance;
    return instance;
}

SystemStateManager::SystemStateManager() : currentState_(SystemState::SETUP_MODE) {
    ConfigStore::getInstance().load(config_);
}

SystemState SystemStateManager::determineState() {
    if (config_.wifi.ssid.empty()) {
        currentState_ = SystemState::SETUP_MODE;
    } else {
        // Assume NORMAL_MODE, but could check WiFi connection later
        currentState_ = SystemState::NORMAL_MODE;
    }
    DebugLogger::getInstance().log(LogLevel::INFO, "System state determined: " + std::to_string((int)currentState_));
    return currentState_;
}

void SystemStateManager::setState(SystemState state) {
    currentState_ = state;
    DebugLogger::getInstance().log(LogLevel::INFO, "System state set to: " + std::to_string((int)state));
}