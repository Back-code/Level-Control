#include "App.h"

#include <Arduino.h>
#include <esp_ota_ops.h>

#include "SystemState.h"
#include "WifiManager.h"
#include "WebServerSetup.h"
#include "WebServerDashboard.h"
#include "SensorManager.h"
#include "MqttManager.h"
#include "PushNotificationManager.h"
#include "DebugLogger.h"
#include "EventBus.h"
#include "ConfigStore.h"
#include "HistoryManager.h"

App& App::getInstance() {
    static App instance;
    return instance;
}

void App::init() {
    Serial.begin(115200);
    delay(100);
    Serial.println("[Level-Control] Starting...");
    DebugLogger::getInstance().log(LogLevel::INFO, "Starting Level-Control Sensor");

    // OTA-Rollback-Validierung so frueh wie moeglich ausfuehren.
    // Darf nicht vom WLAN-Status abhaengen, sonst kann ein frischer OTA-Boot
    // bei fehlendem WLAN unbestaetigt bleiben.
    esp_err_t otaMarkRes = esp_ota_mark_app_valid_cancel_rollback();
    if (otaMarkRes == ESP_OK) {
        DebugLogger::getInstance().log(LogLevel::INFO, "Aktuelles Image als gueltig markiert");
    } else if (otaMarkRes == ESP_ERR_NOT_FOUND || otaMarkRes == ESP_ERR_INVALID_STATE) {
        DebugLogger::getInstance().log(LogLevel::INFO, std::string("OTA-Validierung nicht noetig: ") + esp_err_to_name(otaMarkRes));
    } else {
        DebugLogger::getInstance().log(LogLevel::WARN, std::string("esp_ota_mark_app_valid_cancel_rollback(): ") + esp_err_to_name(otaMarkRes));
    }

    SystemStateManager& ssm = SystemStateManager::getInstance();
    SystemState state = ssm.determineState();

    WifiManager::getInstance().init();

    {
        // #region agent log boot decision + config existence
        Config bootConfig;
        const bool bootCfgLoaded = ConfigStore::getInstance().load(bootConfig);
        const bool cfgFallbackApplied = ConfigStore::getInstance().wasFallbackApplied();
        const std::string cfgFallbackMessage = ConfigStore::getInstance().getFallbackMessage();
        DebugLogger::getInstance().log(
            LogLevel::INFO,
            std::string("DEBUG_BOOT H1/H2: determineState=") + (state == SystemState::SETUP_MODE ? "SETUP" : "NORMAL")
                + ", ConfigStore.load=" + (bootCfgLoaded ? "true" : "false")
                + ", ssid_len=" + std::to_string(bootConfig.wifi.ssid.size())
        );
        if (cfgFallbackApplied) {
            DebugLogger::getInstance().log(
                LogLevel::WARN,
                std::string("Config fallback aktiv: ") + cfgFallbackMessage);
        }
        // #endregion
    }

    if (state == SystemState::SETUP_MODE) {
        // #region agent log boot setup-mode wifi config
        const WifiConfig wifiCfg = WifiManager::getInstance().getConfig();
        DebugLogger::getInstance().log(
            LogLevel::WARN,
            std::string("DEBUG_BOOT H1/H3: entering SETUP_MODE, wifiCfg.ssid_len=") + std::to_string(wifiCfg.ssid.size())
        );
        // #endregion
        WifiManager::getInstance().startAP("Level-Control-Setup", "");
        WebServerSetup::getInstance().init();
        WebServerSetup::getInstance().start();
    } else {
        const bool connected = WifiManager::getInstance().connect();
        // #region agent log boot wifi connect result
        DebugLogger::getInstance().log(
            LogLevel::INFO,
            std::string("DEBUG_BOOT H2: WiFi connect returned=") + (connected ? "true" : "false")
                + ", wifiCfg.ssid_len=" + std::to_string(WifiManager::getInstance().getConfig().ssid.size())
        );
        // #endregion

        if (connected) {
            WebServerDashboard::getInstance().init();
            WebServerDashboard::getInstance().start();
            // Initialize MQTT with config
            Config config;
            if (ConfigStore::getInstance().load(config) && !config.mqtt.server.empty()) {
                MqttManager::getInstance().init(config.mqtt.server.c_str(), config.mqtt.port);
                MqttManager::getInstance().connect();
            }
        } else {
            // Nach 20 fehlgeschlagenen Versuchen -> Setup-AP starten
            DebugLogger::getInstance().log(LogLevel::WARN,
                "WiFi connect failed, switching to SETUP_MODE");
            ssm.setState(SystemState::SETUP_MODE);
            WifiManager::getInstance().startAP("Level-Control-Setup", "");
            WebServerSetup::getInstance().init();
            WebServerSetup::getInstance().start();
        }
    }

    SensorManager::getInstance().init();
    PushNotificationManager::getInstance().init();
    // HistoryManager nach dem LittleFS-Mount (WebServerDashboard::init) initialisieren
    HistoryManager::getInstance().init();

    // Sensor Test beim Start
    Serial.println("[Level-Control] Starting sensor test...");
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
    Serial.println("[Level-Control] Sensor test complete");
}

void App::loop() {
    WifiManager::getInstance().process();
    MqttManager::getInstance().loop();

    const unsigned long now = millis();
    const bool updateInProgress = WebServerDashboard::getInstance().isUpdateInProgress();

    if (updateInProgress) {
        if (now - lastSerial_ > 10000) {
            lastSerial_ = now;
            Serial.println("[Level-Control] Update aktiv, nicht-kritische Laufzeitjobs pausiert");
        }

        delay(10);
        return;
    }

    if (lastMeasurement_ == 0 || now - lastMeasurement_ >= SensorManager::getInstance().getSampleIntervalMs()) {
        lastMeasurement_ = now;
        SensorManager::getInstance().measure();
        // Sofort nach jeder Messung Sensordaten übertragen, damit die UI ohne Verzögerung aktualisiert wird.
        WebServerDashboard::getInstance().broadcastSensorData();
    }

    PushNotificationManager::getInstance().process(
        SensorManager::getInstance().getDistancePercent(),
        SensorManager::getInstance().getDistanceCm(),
        SensorManager::getInstance().getRawDistance(),
        SensorManager::getInstance().hasValidReading()
    );

    // MQTT Reconnect alle 5 s prüfen
    if (now - lastMqttReconnect_ > 5000) {
        lastMqttReconnect_ = now;
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
    if (now - lastMqttPublish_ > 30000) {
        lastMqttPublish_ = now;
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

    // Level-Control-Verlauf alle 60 s prüfen; HistoryManager entscheidet intern ob 6h vergangen
    if (lastHistoryCheck_ == 0 || now - lastHistoryCheck_ >= 60000UL) {
        lastHistoryCheck_ = now;
        if (SensorManager::getInstance().hasValidReading()) {
            HistoryManager::getInstance().addSample(
                SensorManager::getInstance().getDistancePercent());
        }
    }

    // WebSocket Broadcast (WiFi, Uptime, MQTT-Status) alle 5 s
    // Sensor-Daten werden direkt nach jeder Messung gesendet (siehe oben).
    if (now - lastBroadcast_ > 5000) {
        lastBroadcast_ = now;
        WebServerDashboard::getInstance().broadcastWifiData();
        WebServerDashboard::getInstance().broadcastUptime();
        WebServerDashboard::getInstance().broadcastMqttState();
    }

    // Serielle Diagnose alle 10 s
    if (now - lastSerial_ > 10000) {
        lastSerial_ = now;
        SensorManager& s = SensorManager::getInstance();
        Serial.printf("[Level-Control] raw=%.3fm cm=%.1f pct=%.1f valid=%d ping=%uus\n",
            s.getRawDistance(), s.getDistanceCm(), s.getDistancePercent(),
            s.hasValidReading() ? 1 : 0, s.getLastPingUs());
    }

    delay(100);
}

