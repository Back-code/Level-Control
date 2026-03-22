#ifndef SYSTEM_STATE_MANAGER_H
#define SYSTEM_STATE_MANAGER_H

#include "ConfigStore.h"

enum class SystemState {
    SETUP_MODE,
    NORMAL_MODE
};

class SystemStateManager {
public:
    static SystemStateManager& getInstance();

    SystemState determineState();
    SystemState getCurrentState() const { return currentState_; }
    void setState(SystemState state);

private:
    SystemStateManager();
    SystemState currentState_;
    Config config_;
};

#endif // SYSTEM_STATE_MANAGER_H