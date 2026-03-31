#include <Arduino.h>
#include "SensorManager.h"
#include "DebugLogger.h"
#include "ConfigStore.h"
#include "EventBus.h"
#include <Wire.h>
#include <Adafruit_VL53L1X.h>

namespace {
constexpr unsigned long kMinSampleIntervalSeconds = 5UL;
Adafruit_VL53L1X vl53l1x = Adafruit_VL53L1X();
}

using SensorType = SensorManager::SensorType;

SensorManager& SensorManager::getInstance() {
    static SensorManager instance;
    return instance;
}

SensorManager::SensorManager() {}

void SensorManager::setSampleIntervalSeconds(unsigned long seconds) {
    if (seconds < kMinSampleIntervalSeconds) {
        seconds = kMinSampleIntervalSeconds;
    }
    sampleIntervalMs_ = seconds * 1000UL;
}

void SensorManager::init() {
    // Load calibration and sensor type from config
    Config cfg;
    if (ConfigStore::getInstance().load(cfg)) {
        behaelterhoehe_ = cfg.behaelterhoehe;
        offset_ = cfg.offset;
        setSampleIntervalSeconds(cfg.sampleIntervalSeconds);
        sensorType_ = (cfg.sensorType == 1) ? SensorType::Laser : SensorType::Ultraschall;
    }

    if (sensorType_ == SensorType::Ultraschall) {
        pinMode(US_TRIGGER_PIN, OUTPUT);
        pinMode(US_ECHO_PIN, INPUT);
        DebugLogger::getInstance().log(LogLevel::INFO,
            "SensorManager init HC-SR04 (hoehe=" + std::to_string(behaelterhoehe_) +
            "cm, offset=" + std::to_string(offset_) +
            "cm, intervall=" + std::to_string(getSampleIntervalSeconds()) + "s)");
    } else {
        // VL53L1X: hard-reset via XSHUT, then init I2C
        pinMode(VL53_XSHUT_PIN, OUTPUT);
        pinMode(VL53_GPIO1_PIN, INPUT);
        digitalWrite(VL53_XSHUT_PIN, LOW);
        delay(10);
        digitalWrite(VL53_XSHUT_PIN, HIGH);
        delay(10);

        Wire.begin(VL53_SDA_PIN, VL53_SCL_PIN);

        if (!vl53l1x.begin(0x29, &Wire)) {
            DebugLogger::getInstance().log(LogLevel::ERROR,
                "VL53L1X: Initialisierung fehlgeschlagen (SDA=" + std::to_string(VL53_SDA_PIN) +
                ", SCL=" + std::to_string(VL53_SCL_PIN) + ")");
            vl53l1xInitialized_ = false;
        } else {
            vl53l1x.setTimingBudget(50);
            vl53l1x.startRanging();
            vl53l1xInitialized_ = true;
            DebugLogger::getInstance().log(LogLevel::INFO,
                "SensorManager init VL53L1X OK (hoehe=" + std::to_string(behaelterhoehe_) +
                "cm, offset=" + std::to_string(offset_) +
                "cm, intervall=" + std::to_string(getSampleIntervalSeconds()) + "s)");
        }
    }
}

unsigned int SensorManager::ping() {
    if (sensorType_ == SensorType::Ultraschall) {
        // Trigger pulse
        digitalWrite(US_TRIGGER_PIN, LOW);
        delayMicroseconds(4);
        digitalWrite(US_TRIGGER_PIN, HIGH);
        delayMicroseconds(10);
        digitalWrite(US_TRIGGER_PIN, LOW);

        // pulseIn ist auf ESP32-C3 meist stabiler als manuelles Busy-Waiting.
        unsigned long duration = pulseIn(US_ECHO_PIN, HIGH, 30000UL);
        return static_cast<unsigned int>(duration);
    } else {
        // VL53L1X: single non-blocking sample (used internally only when data is ready)
        if (!vl53l1xInitialized_ || !vl53l1x.dataReady()) {
            return 0;
        }
        int16_t distance = vl53l1x.distance();
        vl53l1x.clearInterrupt();
        if (distance == -1) {
            return 0;
        }
        return static_cast<unsigned int>(distance);
    }
}

void SensorManager::measure() {
    if (sensorType_ == SensorType::Ultraschall) {
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

        float d_cm = rawDistance_ * 100.0f;
        distanceCm_ = (behaelterhoehe_ + offset_) - d_cm;
        if (distanceCm_ < 0) distanceCm_ = 0;
        if (distanceCm_ > behaelterhoehe_) distanceCm_ = behaelterhoehe_;
        distancePercent_ = (distanceCm_ / behaelterhoehe_) * 100.0f;
        if (distancePercent_ > 100) distancePercent_ = 100;

        DebugLogger::getInstance().log(LogLevel::DEBUG,
            "raw=" + std::to_string(rawDistance_) + "m cm=" +
            std::to_string(distanceCm_) + " %=" + std::to_string(distancePercent_));

    } else {
        // VL53L1X (Laser ToF)
        if (!vl53l1xInitialized_) {
            lastPingUs_ = 0;
            lastValid_ = false;
            DebugLogger::getInstance().log(LogLevel::WARN, "VL53L1X: Sensor nicht initialisiert");
            EventBus::getInstance().publish({EventType::SENSOR_TIMEOUT, ""});
            return;
        }

        // Non-blocking: skip if no fresh measurement is available yet
        if (!vl53l1x.dataReady()) {
            return;
        }

        int16_t distanceMm = vl53l1x.distance();
        vl53l1x.clearInterrupt();

        if (distanceMm == -1) {
            lastPingUs_ = 0;
            lastValid_ = false;
            DebugLogger::getInstance().log(LogLevel::WARN, "VL53L1X: Keine gültige Messung");
            EventBus::getInstance().publish({EventType::SENSOR_TIMEOUT, ""});
            return;
        }

        // Store raw mm value in lastPingUs_ so the dashboard can display it
        lastPingUs_ = static_cast<unsigned int>(distanceMm);
        lastValid_ = true;
        rawDistance_ = distanceMm / 1000.0f; // mm -> m

        float d_cm = rawDistance_ * 100.0f;
        distanceCm_ = (behaelterhoehe_ + offset_) - d_cm;
        if (distanceCm_ < 0) distanceCm_ = 0;
        if (distanceCm_ > behaelterhoehe_) distanceCm_ = behaelterhoehe_;
        distancePercent_ = (distanceCm_ / behaelterhoehe_) * 100.0f;
        if (distancePercent_ > 100) distancePercent_ = 100;

        DebugLogger::getInstance().log(LogLevel::DEBUG,
            "VL53L1X raw=" + std::to_string(rawDistance_) + "m cm=" +
            std::to_string(distanceCm_) + " %=" + std::to_string(distancePercent_) +
            " mm=" + std::to_string(distanceMm));
    }
}