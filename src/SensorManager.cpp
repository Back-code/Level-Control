#include <Arduino.h>
#include "SensorManager.h"
#include "DebugLogger.h"
#include "ConfigStore.h"
#include "EventBus.h"
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
    Config cfg;
    if (ConfigStore::getInstance().load(cfg)) {
        behaelterhoehe_ = cfg.behaelterhoehe;
        offset_ = cfg.offset;
        setSampleIntervalSeconds(cfg.sampleIntervalSeconds);
        // SensorType aus Config übernehmen
        if (cfg.sensorType == 1) {
            sensorType_ = SensorType::Laser;
        } else {
            sensorType_ = SensorType::Ultraschall;
        }
    }

    if (sensorType_ == SensorType::Ultraschall) {
        pinMode(US_TRIGGER_PIN, OUTPUT);
        pinMode(US_ECHO_PIN, INPUT);
    } else if (sensorType_ == SensorType::Laser) {
        pinMode(VL53_SCL_PIN, INPUT_PULLUP); // I2C Pins werden meist vom Wire-Objekt initialisiert
        pinMode(VL53_SDA_PIN, INPUT_PULLUP);
        pinMode(VL53_XSHUT_PIN, OUTPUT);
        pinMode(VL53_GPIO1_PIN, INPUT);

        digitalWrite(VL53_XSHUT_PIN, HIGH); // Sensor aktivieren
        delay(10); // Wartezeit für Stabilisierung
        vl53l1x.setIntPolarity(true); // GPIO1 auf High-Pegel konfigurieren

        if (!vl53l1x.begin()) {
            DebugLogger::getInstance().log(LogLevel::ERROR, "VL53L1X: Initialisierung fehlgeschlagen");
            return;
        }
        vl53l1x.setTimingBudget(50); // Timing-Budget auf 50 ms setzen
        vl53l1x.startRanging();
        DebugLogger::getInstance().log(LogLevel::INFO, "VL53L1X: Initialisierung erfolgreich");
    }

    DebugLogger::getInstance().log(LogLevel::INFO,
        "SensorManager init (hoehe=" + std::to_string(behaelterhoehe_) +
        "cm, offset=" + std::to_string(offset_) +
        "cm, intervall=" + std::to_string(getSampleIntervalSeconds()) + "s, Typ=" +
        (sensorType_ == SensorType::Ultraschall ? "Ultraschall" : "Laser") + ")");
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
    } else if (sensorType_ == SensorType::Laser) {
        if (!vl53l1x.dataReady()) {
            DebugLogger::getInstance().log(LogLevel::WARN, "VL53L1X: Daten nicht bereit");
            return 0;
        }
        int16_t distance = vl53l1x.distance();
        if (distance == -1) {
            DebugLogger::getInstance().log(LogLevel::WARN, "VL53L1X: Keine gültige Messung");
            return 0;
        }
        vl53l1x.clearInterrupt();
        return static_cast<unsigned int>(distance);
    }
    return 0;
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
    } else if (sensorType_ == SensorType::Laser) {
        // TODO: VL53L1X Messung (Platzhalter)
        // Hier als Dummy: keine gültige Messung
        lastPingUs_ = 0;
        lastValid_ = false;
        rawDistance_ = 0.0f;
        distanceCm_ = 0.0f;
        distancePercent_ = 0.0f;
        DebugLogger::getInstance().log(LogLevel::WARN, "VL53L1X: Messung nicht implementiert");
    }
}