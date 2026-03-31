#ifndef SENSOR_MANAGER_H
#define SENSOR_MANAGER_H

#include "EventBus.h"

class SensorManager {
public:
    enum class SensorType {
        Ultraschall = 0,
        Laser       = 1
    };

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
    void setSampleIntervalSeconds(unsigned long seconds);
    void setSensorType(SensorType type) { sensorType_ = type; }
    float getBehaelterhoehe() const { return behaelterhoehe_; }
    float getOffset() const { return offset_; }
    SensorType getSensorType() const { return sensorType_; }
    unsigned long getSampleIntervalSeconds() const { return sampleIntervalMs_ / 1000UL; }
    unsigned long getSampleIntervalMs() const { return sampleIntervalMs_; }

    // HC-SR04 pins
    static constexpr int US_TRIGGER_PIN = 4;
    static constexpr int US_ECHO_PIN    = 5;

    // VL53L1X I2C + control pins
    static constexpr int VL53_SDA_PIN   = 6;
    static constexpr int VL53_SCL_PIN   = 7;
    static constexpr int VL53_XSHUT_PIN = 2;
    static constexpr int VL53_GPIO1_PIN = 3;

private:
    SensorManager();
    unsigned int ping();

    float rawDistance_    = 0.0f;
    float distanceCm_     = 0.0f;
    float distancePercent_= 0.0f;
    unsigned int lastPingUs_ = 0;
    bool lastValid_       = false;
    float behaelterhoehe_ = 95.0f;
    float offset_         = 0.0f;
    unsigned long sampleIntervalMs_ = 5000UL;
    SensorType sensorType_          = SensorType::Ultraschall;
    bool vl53l1xInitialized_        = false;
    const int maxDistance_          = 400;
};

#endif // SENSOR_MANAGER_H