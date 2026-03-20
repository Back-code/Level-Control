#include "SensorManager.h"
#include "DebugLogger.h"
#include "ConfigStore.h"
#include "EventBus.h"

SensorManager& SensorManager::getInstance() {
    static SensorManager instance;
    return instance;
}

SensorManager::SensorManager() {}

void SensorManager::init() {
    pinMode(triggerPin_, OUTPUT);
    pinMode(echoPin_, INPUT);

    // Kalibrierungswerte aus gespeicherter Config laden
    Config cfg;
    if (ConfigStore::getInstance().load(cfg)) {
        behaelterhoehe_ = cfg.behaelterhoehe;
        offset_ = cfg.offset;
    }

    DebugLogger::getInstance().log(LogLevel::INFO,
        "SensorManager init (hoehe=" + std::to_string(behaelterhoehe_) +
        "cm, offset=" + std::to_string(offset_) + "cm)");
}

unsigned int SensorManager::ping() {
    // Trigger pulse
    digitalWrite(triggerPin_, LOW);
    delayMicroseconds(4);
    digitalWrite(triggerPin_, HIGH);
    delayMicroseconds(10);
    digitalWrite(triggerPin_, LOW);

    // pulseIn ist auf ESP32-C3 meist stabiler als manuelles Busy-Waiting.
    unsigned long duration = pulseIn(echoPin_, HIGH, 30000UL);
    return static_cast<unsigned int>(duration);
}

void SensorManager::measure() {
    // Take median of 10 measurements
    unsigned int measurements[10];
    int validCount = 0;
    for (int i = 0; i < 10; i++) {
        unsigned int uS = ping();
        if (uS > 0 && uS < 25000) { // Valid range
            measurements[validCount++] = uS;
        }
        delay(30); // Wait between measurements
    }

    if (validCount == 0) {
        lastPingUs_ = 0;
        lastValid_ = false;
        DebugLogger::getInstance().log(LogLevel::WARN, "Sensor timeout - no valid measurements");
        EventBus::getInstance().publish({EventType::SENSOR_TIMEOUT, ""});
        return;
    }

    // Simple median (sort and pick middle)
    for (int i = 0; i < validCount - 1; i++) {
        for (int j = i + 1; j < validCount; j++) {
            if (measurements[i] > measurements[j]) {
                unsigned int temp = measurements[i];
                measurements[i] = measurements[j];
                measurements[j] = temp;
            }
        }
    }
    unsigned int uS = measurements[validCount / 2];

    lastPingUs_ = uS;
    lastValid_ = true;
    rawDistance_ = uS / 1000000.0f * 340.0f / 2.0f; // in m

    // offset_ = Abstand Sensor -> Behälteroberkante (cm)
    // Füllstand = (behaelterhoehe_ + offset_) - gemessene_Distanz_cm
    float d_cm = rawDistance_ * 100.0f;
    distanceCm_ = (behaelterhoehe_ + offset_) - d_cm;
    if (distanceCm_ < 0) distanceCm_ = 0;
    if (distanceCm_ > behaelterhoehe_) distanceCm_ = behaelterhoehe_;
    distancePercent_ = (distanceCm_ / behaelterhoehe_) * 100.0f;
    if (distancePercent_ > 100) distancePercent_ = 100;

    DebugLogger::getInstance().log(LogLevel::DEBUG,
        "raw=" + std::to_string(rawDistance_) + "m cm=" +
        std::to_string(distanceCm_) + " %=" + std::to_string(distancePercent_));
}