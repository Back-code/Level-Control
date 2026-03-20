#ifndef SENSOR_MANAGER_H
#define SENSOR_MANAGER_H

#include "EventBus.h"

class SensorManager {
public:
    static SensorManager& getInstance();

    void init();
    void measure();

    float getRawDistance() const { return rawDistance_; }
    float getDistanceCm() const { return distanceCm_; }
    float getDistancePercent() const { return distancePercent_; }
    unsigned int getLastPingUs() const { return lastPingUs_; }
    bool hasValidReading() const { return lastValid_; }

    void setBehaelterhoehe(float h) { behaelterhoehe_ = h; }
    void setOffset(float o) { offset_ = o; }
    float getBehaelterhoehe() const { return behaelterhoehe_; }
    float getOffset() const { return offset_; }

private:
    SensorManager();
    unsigned int ping();
    float rawDistance_ = 0.0;
    float distanceCm_ = 0.0;
    float distancePercent_ = 0.0;
    unsigned int lastPingUs_ = 0;
    bool lastValid_ = false;
    float behaelterhoehe_ = 95.0;
    float offset_ = 0.0;
    const int triggerPin_ = 4;
    const int echoPin_ = 5;
    const int maxDistance_ = 400;
};

#endif // SENSOR_MANAGER_H