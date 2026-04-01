// ...existing code...

#include <Arduino.h>
#include <Wire.h>
#include "ConfigStore.h"
#include "DebugLogger.h"
#include "SystemStateManager.h"
// Falls noch nicht vorhanden, Konstante für Mindestintervall deklarieren
#ifndef KMIN_SAMPLE_INTERVAL_SECONDS
#define KMIN_SAMPLE_INTERVAL_SECONDS 1UL
#endif
#include <string>

#include "SensorManager.h"
#include <Wire.h>
#include <string>

std::string SensorManager::getLaserVersion() {
    const uint8_t ADDR = 0x29;

    // Gleiche Startsequenz wie im verifizierten TEST-Sketch.
    pinMode(kXshutPin, OUTPUT);
    digitalWrite(kXshutPin, LOW);
    delay(10);
    digitalWrite(kXshutPin, HIGH);
    delay(10);
    pinMode(kGpio1Pin, INPUT_PULLUP);

    Wire.begin(kSdaPin, kSclPin);
    Wire.setClock(100000);
    delay(20);

    // Erst pruefen, ob ueberhaupt ein Geraet auf 0x29 antwortet.
    Wire.beginTransmission(ADDR);
    if (Wire.endTransmission() != 0) {
        return "Unbekannt";
    }

    auto readReg8 = [&](uint16_t reg, uint8_t &out) -> bool {
        Wire.beginTransmission(ADDR);
        Wire.write((uint8_t)(reg >> 8));
        Wire.write((uint8_t)(reg & 0xFF));
        if (Wire.endTransmission(false) != 0) {
            return false;
        }
        if (Wire.requestFrom((uint8_t)ADDR, (uint8_t)1) != 1) {
            return false;
        }
        out = Wire.read();
        return true;
    };

    // VL53L1X: Model-ID Register 0x010F == 0xEA
    uint8_t id_l1 = 0;
    if (readReg8(0x010F, id_l1) && id_l1 == 0xEA) {
        return "VL53L1X";
    }

    // VL53L0X: IDENTIFICATION_MODEL_ID Register 0x00C0 == 0xEE
    uint8_t id_l0 = 0;
    if (readReg8(0x00C0, id_l0) && id_l0 == 0xEE) {
        return "VL53L0X";
    }

    return "Unbekannt";
}

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

void SensorManager::setSensorType(const std::string& type) {
    sensorType_ = (type == "vl53l1x") ? "vl53l1x" : "rcwl1670";
}

void SensorManager::init() {
    // Kalibrierungswerte aus gespeicherter Config laden
    Config cfg;
    if (ConfigStore::getInstance().load(cfg)) {
        behaelterhoehe_ = cfg.behaelterhoehe;
        offset_ = cfg.offset;
        setSampleIntervalSeconds(cfg.sampleIntervalSeconds);
        setSensorType(cfg.sensorType);
    }

    DebugLogger::getInstance().log(LogLevel::INFO,
        "SensorManager init (hoehe=" + std::to_string(behaelterhoehe_) +
        "cm, offset=" + std::to_string(offset_) +
        "cm, intervall=" + std::to_string(getSampleIntervalSeconds()) +
        "s, sensor=" + sensorType_ + ")");

    if (sensorType_ == "vl53l1x") {
        initVl53l1x();
    } else {
        initRcwl1670();
    }
}

void SensorManager::initRcwl1670() {
    pinMode(triggerPin_, OUTPUT);
    pinMode(echoPin_, INPUT);
    DebugLogger::getInstance().log(LogLevel::INFO, "RCWL-1670 initialisiert (trigger=" +
        std::to_string(triggerPin_) + ", echo=" + std::to_string(echoPin_) + ")");
}

void SensorManager::initVl53l1x() {
    vl53l1xInitialized_ = false;

    // XSHUT pin steuern: sensor erst deaktivieren, dann aktivieren
    pinMode(kXshutPin, OUTPUT);
    digitalWrite(kXshutPin, LOW);
    delay(10);
    digitalWrite(kXshutPin, HIGH);
    delay(10);

    Wire.begin(kSdaPin, kSclPin);
    vl53l1x_.setBus(&Wire);
    vl53l1x_.setTimeout(500);

    if (!vl53l1x_.init()) {
        DebugLogger::getInstance().log(LogLevel::ERROR, "VL53L1X init fehlgeschlagen!");
        return;
    }

    vl53l1x_.setDistanceMode(VL53L1X::Short);
    vl53l1x_.setMeasurementTimingBudget(50000);
    vl53l1x_.startContinuous(50);
    vl53l1xInitialized_ = true;

    DebugLogger::getInstance().log(LogLevel::INFO, "VL53L1X initialisiert (SDA=" +
        std::to_string(kSdaPin) + ", SCL=" + std::to_string(kSclPin) +
        ", XSHUT=" + std::to_string(kXshutPin) + ")");
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
    if (sensorType_ == "vl53l1x") {
        measureVl53l1x();
    } else {
        measureRcwl1670();
    }
}

void SensorManager::measureRcwl1670() {
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

void SensorManager::measureVl53l1x() {
    if (!vl53l1xInitialized_) {
        lastPingUs_ = 0;
        lastValid_ = false;
        DebugLogger::getInstance().log(LogLevel::WARN, "VL53L1X nicht initialisiert");
        EventBus::getInstance().publish({EventType::SENSOR_TIMEOUT, ""});
        return;
    }

    // Take median of 5 measurements
    float readings_mm[5];
    int validCount = 0;

    for (int i = 0; i < 5; i++) {
        uint16_t dist_mm = vl53l1x_.read();
        // range_status == 0 means valid measurement
        if (vl53l1x_.ranging_data.range_status == 0 && dist_mm > 0 && dist_mm < 4000) {
            readings_mm[validCount++] = static_cast<float>(dist_mm);
        }
    }

    if (validCount == 0) {
        lastPingUs_ = 0;
        lastValid_ = false;
        DebugLogger::getInstance().log(LogLevel::WARN, "VL53L1X timeout - no valid measurements");
        EventBus::getInstance().publish({EventType::SENSOR_TIMEOUT, ""});
        return;
    }

    // Simple median (sort and pick middle)
    for (int i = 0; i < validCount - 1; i++) {
        for (int j = i + 1; j < validCount; j++) {
            if (readings_mm[i] > readings_mm[j]) {
                float temp = readings_mm[i];
                readings_mm[i] = readings_mm[j];
                readings_mm[j] = temp;
            }
        }
    }
    float dist_mm = readings_mm[validCount / 2];

    // Simulate ping_us for dashboard consistency: dist_mm / 1000 [m] / 340 [m/s] * 2 [round-trip] * 1e6 [µs/s]
    lastPingUs_ = static_cast<unsigned int>(dist_mm / 1000.0f / 340.0f * 2.0f * 1000000.0f);
    lastValid_ = true;
    rawDistance_ = dist_mm / 1000.0f; // mm -> m

    float d_cm = dist_mm / 10.0f; // mm -> cm
    distanceCm_ = (behaelterhoehe_ + offset_) - d_cm;
    if (distanceCm_ < 0) distanceCm_ = 0;
    if (distanceCm_ > behaelterhoehe_) distanceCm_ = behaelterhoehe_;
    distancePercent_ = (distanceCm_ / behaelterhoehe_) * 100.0f;
    if (distancePercent_ > 100) distancePercent_ = 100;

    DebugLogger::getInstance().log(LogLevel::DEBUG,
        "VL53L1X raw=" + std::to_string(rawDistance_) + "m cm=" +
        std::to_string(distanceCm_) + " %=" + std::to_string(distancePercent_));
}
