#include <Arduino.h>
#include "SystemStateManager.h"
#include "WifiManager.h"
#include "WebServerSetup.h"
#include "WebServerDashboard.h"
#include "SensorManager.h"
#include "MqttManager.h"
#include "PushNotificationManager.h"
#include "DebugLogger.h"
#include "EventBus.h"
#include "ConfigStore.h"

void setup() {
    Serial.begin(115200);
    delay(100);
    Serial.println("[Salzstand] Starting...");
    DebugLogger::getInstance().log(LogLevel::INFO, "Starting Salzstand Sensor");

    SystemStateManager& ssm = SystemStateManager::getInstance();
    SystemState state = ssm.determineState();

    WifiManager::getInstance().init();

    if (state == SystemState::SETUP_MODE) {
        WifiManager::getInstance().startAP("Salzstand-Setup", "");
        WebServerSetup::getInstance().init();
        WebServerSetup::getInstance().start();
    } else {
        if (WifiManager::getInstance().connect()) {
            WebServerDashboard::getInstance().init();
            WebServerDashboard::getInstance().start();
            // Initialize MQTT with config
            Config config;
            if (ConfigStore::getInstance().load(config) && !config.mqtt.server.empty()) {
                MqttManager::getInstance().init(config.mqtt.server.c_str(), config.mqtt.port);
                MqttManager::getInstance().connect();
            }
        } else {
            ssm.setState(SystemState::FALLBACK_AP);
            WifiManager::getInstance().startAP("Salzstand-Fallback", "password");
            WebServerSetup::getInstance().init();
            WebServerSetup::getInstance().start();
        }
    }

    SensorManager::getInstance().init();
    PushNotificationManager::getInstance().init();

    // Sensor Test beim Start
    Serial.println("[Salzstand] Starting sensor test...");
    for (int i = 0; i < 5; i++) {
        SensorManager::getInstance().measure();
        delay(500);
        float raw = SensorManager::getInstance().getRawDistance();
        float cm = SensorManager::getInstance().getDistanceCm();
        float pct = SensorManager::getInstance().getDistancePercent();
        unsigned int ping = SensorManager::getInstance().getLastPingUs();
        bool valid = SensorManager::getInstance().hasValidReading();
        Serial.printf("[Test %d] raw=%.3fm cm=%.1f pct=%.1f valid=%d ping=%uus\n", i+1, raw, cm, pct, valid ? 1 : 0, ping);
    }
    Serial.println("[Salzstand] Sensor test complete");
}

void loop() {
    static unsigned long lastBroadcast       = 0;
    static unsigned long lastSerial          = 0;
    static unsigned long lastMeasurement     = 0;
    static unsigned long lastMqttReconnect   = 0;
    static unsigned long lastMqttPublish     = 0;

    WifiManager::getInstance().process();
    MqttManager::getInstance().loop();

    const unsigned long now = millis();
    if (lastMeasurement == 0 || now - lastMeasurement >= SensorManager::getInstance().getSampleIntervalMs()) {
        lastMeasurement = now;
        SensorManager::getInstance().measure();
    }

    PushNotificationManager::getInstance().process(
        SensorManager::getInstance().getDistancePercent(),
        SensorManager::getInstance().getDistanceCm(),
        SensorManager::getInstance().getRawDistance(),
        SensorManager::getInstance().hasValidReading()
    );

    // MQTT Reconnect alle 5 s prüfen
    if (now - lastMqttReconnect > 5000) {
        lastMqttReconnect = now;
        if (WifiManager::getInstance().isConnected() &&
            !MqttManager::getInstance().isConnected()) {
            Config config;
            if (ConfigStore::getInstance().load(config) && !config.mqtt.server.empty()) {
                MqttManager::getInstance().init(config.mqtt.server.c_str(), config.mqtt.port);
                MqttManager::getInstance().connect();
            }
        }
    }

    // MQTT Sensor-Daten alle 30 s senden
    if (now - lastMqttPublish > 30000) {
        lastMqttPublish = now;
        SensorManager& s = SensorManager::getInstance();
        const char* sensorStatus = s.hasValidReading() ? "ok" :
                                   (s.getLastPingUs() == 0 ? "timeout" : "out_of_range");
        MqttManager::getInstance().publishSensorState(
            s.getDistancePercent(),
            s.getDistanceCm(),
            s.getRawDistance(),
            s.getLastPingUs(),
            s.hasValidReading(),
            sensorStatus
        );
        MqttManager::getInstance().publishSystemState();
        MqttManager::getInstance().publishUpdateState();
    }

    // WebSocket Broadcast alle 5 s
    if (now - lastBroadcast > 5000) {
        lastBroadcast = now;
        WebServerDashboard::getInstance().broadcastSensorData();
        WebServerDashboard::getInstance().broadcastWifiData();
        WebServerDashboard::getInstance().broadcastUptime();
        WebServerDashboard::getInstance().broadcastMqttState();
    }

    // Serielle Diagnose alle 10 s
    if (now - lastSerial > 10000) {
        lastSerial = now;
        SensorManager& s = SensorManager::getInstance();
        Serial.printf("[Salzstand] raw=%.3fm cm=%.1f pct=%.1f valid=%d ping=%uus\n",
            s.getRawDistance(), s.getDistanceCm(), s.getDistancePercent(),
            s.hasValidReading() ? 1 : 0, s.getLastPingUs());
    }

    delay(100);
}