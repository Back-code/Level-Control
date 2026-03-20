#include "MqttManager.h"
#include "DebugLogger.h"
#include "ConfigStore.h"
#include "SensorManager.h"
#include <WiFi.h>
#include <ArduinoJson.h>

// PubSubClient defines MQTT_CONNECTED / MQTT_DISCONNECTED as int macros —
// undefine to avoid conflict with our EventType enum class values.
#ifdef MQTT_CONNECTED
#undef MQTT_CONNECTED
#endif
#ifdef MQTT_DISCONNECTED
#undef MQTT_DISCONNECTED
#endif

// ------------------------------------------------------------------ Topics
static const char* TOPIC_STATUS          = "salzstand/status";
static const char* TOPIC_SENSOR_STATE    = "salzstand/sensor/state";
static const char* TOPIC_CFG_HOEHE_STATE = "salzstand/config/behaelterhoehe/state";
static const char* TOPIC_CFG_HOEHE_SET   = "salzstand/config/behaelterhoehe/set";
static const char* TOPIC_CFG_OFFSET_STATE = "salzstand/config/offset/state";
static const char* TOPIC_CFG_OFFSET_SET  = "salzstand/config/offset/set";
static const char* TOPIC_SYSTEM_STATE    = "salzstand/system/state";

// ------------------------------------------------------------------ Singleton
MqttManager& MqttManager::getInstance() {
    static MqttManager instance;
    return instance;
}

MqttManager::MqttManager() : mqttClient_(netClient_) {
    mqttClient_.setCallback(mqttCallbackStatic);
}

// ------------------------------------------------------------------ Static callback shim
void MqttManager::mqttCallbackStatic(char* topic, byte* payload, unsigned int length) {
    std::string pl((char*)payload, length);
    MqttManager::getInstance().handleMessage(topic, pl);
}

// ------------------------------------------------------------------ Incoming message handler
void MqttManager::handleMessage(const char* topic, const std::string& payload) {
    DebugLogger::getInstance().log(LogLevel::DEBUG,
        std::string("MQTT rx [") + topic + "] " + payload);

    Config config;
    if (!ConfigStore::getInstance().load(config)) return;

    bool changed = false;

    if (std::string(topic) == TOPIC_CFG_HOEHE_SET) {
        try {
            float val = std::stof(payload);
            if (val > 0 && val <= 1000.0f) {
                config.behaelterhoehe = val;
                SensorManager::getInstance().setBehaelterhoehe(val);
                changed = true;
                DebugLogger::getInstance().log(LogLevel::INFO,
                    "MQTT: behaelterhoehe=" + payload + "cm");
            }
        } catch (...) {
            DebugLogger::getInstance().log(LogLevel::WARN,
                "MQTT: ungültiger Wert für behaelterhoehe: " + payload);
        }
    } else if (std::string(topic) == TOPIC_CFG_OFFSET_SET) {
        try {
            float val = std::stof(payload);
            if (val >= -500.0f && val <= 500.0f) {
                config.offset = val;
                SensorManager::getInstance().setOffset(val);
                changed = true;
                DebugLogger::getInstance().log(LogLevel::INFO,
                    "MQTT: offset=" + payload + "cm");
            }
        } catch (...) {
            DebugLogger::getInstance().log(LogLevel::WARN,
                "MQTT: ungültiger Wert für offset: " + payload);
        }
    }

    if (changed) {
        ConfigStore::getInstance().save(config);
        publishConfig();
    }
}

// ------------------------------------------------------------------ Subscribe
void MqttManager::subscribeToTopics() {
    mqttClient_.subscribe(TOPIC_CFG_HOEHE_SET);
    mqttClient_.subscribe(TOPIC_CFG_OFFSET_SET);
    DebugLogger::getInstance().log(LogLevel::INFO, "MQTT: config topics subscribed");
}

// ------------------------------------------------------------------ Init
void MqttManager::init(const char* server, uint16_t port) {
    server_ = server ? server : "";
    port_   = port;

    // Größeren Buffer für HA-Discovery Payloads (~400 Bytes)
    mqttClient_.setBufferSize(1024);
    mqttClient_.setServer(server_.c_str(), port_);
    mqttClient_.setKeepAlive(30);
    mqttClient_.setCallback(mqttCallbackStatic);
    state_ = MqttConnectionState::DISCONNECTED;

    // Eindeutige Geräte-ID aus MAC-Adresse
    std::string mac = WiFi.macAddress().c_str();
    deviceId_ = "salzstand_";
    for (char c : mac) {
        if (c != ':') deviceId_ += c;
    }
    mqttClientId_ = deviceId_;

    Config config;
    if (ConfigStore::getInstance().load(config)) {
        username_ = config.mqtt.user;
        password_ = config.mqtt.password;
        if (password_ == "***") password_.clear();
        if (!username_.empty()) {
            DebugLogger::getInstance().log(LogLevel::INFO, "MQTT auth: " + username_);
        }
    }

    DebugLogger::getInstance().log(LogLevel::INFO,
        "MQTT init " + server_ + ":" + std::to_string(port_) +
        " id=" + deviceId_);
}

// ------------------------------------------------------------------ Connect
void MqttManager::connect() {
    unsigned long now = millis();
    if (lastConnectAttemptMs_ != 0 && now - lastConnectAttemptMs_ < connectRetryIntervalMs_) {
        state_ = MqttConnectionState::BACKOFF;
        return;
    }
    if (mqttClient_.connected()) {
        state_ = MqttConnectionState::CONNECTED;
        return;
    }
    if (connecting_) {
        state_ = MqttConnectionState::CONNECTING;
        return;
    }
    if (server_.empty()) {
        state_ = MqttConnectionState::UNINITIALIZED;
        return;
    }

    connecting_ = true;
    state_ = MqttConnectionState::CONNECTING;
    lastConnectAttemptMs_ = now;

    // Verbinden mit LWT "offline"
    const char* user = username_.empty() ? nullptr : username_.c_str();
    const char* pass = password_.empty() ? nullptr : password_.c_str();
    bool ok = mqttClient_.connect(mqttClientId_.c_str(), user, pass,
                                  TOPIC_STATUS, 0, true, "offline");

    connecting_ = false;
    if (ok) {
        state_ = MqttConnectionState::CONNECTED;
        consecutiveFailures_ = 0;

        // Verfügbarkeit publizieren
        publish(TOPIC_STATUS, "online");

        // Config-Topics abonnieren
        subscribeToTopics();

        // Discovery + akt. Konfig nur wenn aktiviert
        Config config;
        if (ConfigStore::getInstance().load(config)) {
            if (config.mqtt.discovery) {
                publishDiscovery();
            }
            publishConfig();
        }

        DebugLogger::getInstance().log(LogLevel::INFO, "MQTT connected");
        EventBus::getInstance().publish({EventType::SYSTEM_MQTT_CONNECTED, ""});
    } else {
        state_ = MqttConnectionState::DISCONNECTED;
        consecutiveFailures_++;
        if (consecutiveFailures_ == 1 || (consecutiveFailures_ % 6) == 0) {
            DebugLogger::getInstance().log(LogLevel::WARN,
                "MQTT connect failed rc=" + std::to_string(mqttClient_.state()));
        }
        EventBus::getInstance().publish({EventType::SYSTEM_MQTT_DISCONNECTED, ""});
    }
}

// ------------------------------------------------------------------ Loop
void MqttManager::loop() {
    if (mqttClient_.connected()) {
        mqttClient_.loop();
        state_ = MqttConnectionState::CONNECTED;
    }
}

// ------------------------------------------------------------------ Disconnect
void MqttManager::disconnect() {
    if (isConnected()) {
        publish(TOPIC_STATUS, "offline");
    }
    connecting_ = false;
    mqttClient_.disconnect();
    state_ = MqttConnectionState::DISCONNECTED;
    EventBus::getInstance().publish({EventType::SYSTEM_MQTT_DISCONNECTED, ""});
}

// ------------------------------------------------------------------ Publish generic
void MqttManager::publish(const char* topic, const char* payload, bool retain) {
    if (isConnected()) {
        mqttClient_.publish(topic, payload, retain);
    }
}

// ------------------------------------------------------------------ Publish sensor state
void MqttManager::publishSensorState(float fillLevel, float distanceCm,
                                      float rawDistanceM, unsigned int pingUs,
                                      bool valid, const char* status) {
    if (!isConnected()) return;
    char buf[192];
    snprintf(buf, sizeof(buf),
        "{\"fill_level\":%.1f,\"distance_cm\":%.1f,"
        "\"raw_distance_m\":%.4f,\"ping_us\":%u,\"valid\":%s,\"status\":\"%s\"}",
        fillLevel, distanceCm, rawDistanceM, pingUs,
        valid ? "true" : "false",
        status ? status : "ok");
    publish(TOPIC_SENSOR_STATE, buf);
}

// ------------------------------------------------------------------ Publish config state
void MqttManager::publishConfig() {
    if (!isConnected()) return;
    Config config;
    if (!ConfigStore::getInstance().load(config)) return;

    char buf[16];
    snprintf(buf, sizeof(buf), "%.1f", config.behaelterhoehe);
    publish(TOPIC_CFG_HOEHE_STATE, buf);
    snprintf(buf, sizeof(buf), "%.1f", config.offset);
    publish(TOPIC_CFG_OFFSET_STATE, buf);
}

// ------------------------------------------------------------------ setWill (legacy)
// ------------------------------------------------------------------ Publish system state
void MqttManager::publishSystemState() {
    if (!isConnected()) return;
    char buf[384];
    snprintf(buf, sizeof(buf),
        "{"
        "\"ip\":\"%s\","
        "\"ssid\":\"%s\","
        "\"rssi\":%d,"
        "\"uptime_s\":%lu,"
        "\"free_heap\":%u,"
        "\"min_free_heap\":%u,"
        "\"cpu_freq_mhz\":%u,"
        "\"flash_size_kb\":%u,"
        "\"sketch_size_kb\":%u,"
        "\"chip_rev\":%u"
        "}",
        WiFi.localIP().toString().c_str(),
        WiFi.SSID().c_str(),
        (int)WiFi.RSSI(),
        millis() / 1000UL,
        (unsigned)ESP.getFreeHeap(),
        (unsigned)ESP.getMinFreeHeap(),
        (unsigned)ESP.getCpuFreqMHz(),
        (unsigned)(ESP.getFlashChipSize() / 1024),
        (unsigned)(ESP.getSketchSize() / 1024),
        (unsigned)ESP.getChipRevision()
    );
    publish(TOPIC_SYSTEM_STATE, buf);
}

// ------------------------------------------------------------------ setWill (legacy)
void MqttManager::setWill(const char* topic, const char* payload) {
    willTopic_   = topic   ? topic   : "";
    willPayload_ = payload ? payload : "";
}

// ------------------------------------------------------------------ Home Assistant Discovery
void MqttManager::publishDiscovery() {
    if (!isConnected() || deviceId_.empty()) return;

    // Device-Block wird in jede Entity eingebettet
    auto addDevice = [&](JsonDocument& d) {
        JsonObject dev = d.createNestedObject("device");
        dev["identifiers"][0] = deviceId_;
        dev["name"]           = "Salzstand";
        dev["manufacturer"]   = "DIY";
        dev["model"]          = "ESP32-C3";
    };

    // Hilfsfunktion: Topic erzeugen + publishen
    auto send = [&](const char* domain, const char* objId, JsonDocument& doc) {
        std::string topic = std::string("homeassistant/") + domain +
                            "/" + deviceId_ + "/" + objId + "/config";
        std::string payload;
        serializeJson(doc, payload);
        publish(topic.c_str(), payload.c_str());
    };

    // --- 1. Füllstand % ---
    {
        DynamicJsonDocument doc(512);
        doc["unique_id"]          = deviceId_ + "_fill_level";
        doc["name"]               = "Füllstand";
        doc["state_topic"]        = TOPIC_SENSOR_STATE;
        doc["value_template"]     = "{{ value_json.fill_level | round(1) }}";
        doc["unit_of_measurement"]= "%";
        doc["icon"]               = "mdi:cup-water";
        doc["availability_topic"] = TOPIC_STATUS;
        addDevice(doc);
        send("sensor", "fill_level", doc);
    }

    // --- 2. Salzstand in cm ---
    {
        DynamicJsonDocument doc(512);
        doc["unique_id"]          = deviceId_ + "_distance_cm";
        doc["name"]               = "Salzstand";
        doc["state_topic"]        = TOPIC_SENSOR_STATE;
        doc["value_template"]     = "{{ value_json.distance_cm | round(1) }}";
        doc["unit_of_measurement"]= "cm";
        doc["icon"]               = "mdi:ruler";
        doc["availability_topic"] = TOPIC_STATUS;
        addDevice(doc);
        send("sensor", "distance_cm", doc);
    }

    // --- 3. Rohdistanz in m ---
    {
        DynamicJsonDocument doc(512);
        doc["unique_id"]          = deviceId_ + "_raw_distance";
        doc["name"]               = "Rohdistanz";
        doc["state_topic"]        = TOPIC_SENSOR_STATE;
        doc["value_template"]     = "{{ value_json.raw_distance_m | round(3) }}";
        doc["unit_of_measurement"]= "m";
        doc["icon"]               = "mdi:tape-measure";
        doc["availability_topic"] = TOPIC_STATUS;
        doc["entity_category"]    = "diagnostic";
        addDevice(doc);
        send("sensor", "raw_distance", doc);
    }

    // --- 4. Ping-Zeit (diagnostisch) ---
    {
        DynamicJsonDocument doc(512);
        doc["unique_id"]          = deviceId_ + "_ping_us";
        doc["name"]               = "Ultraschall Pingzeit";
        doc["state_topic"]        = TOPIC_SENSOR_STATE;
        doc["value_template"]     = "{{ value_json.ping_us }}";
        doc["unit_of_measurement"]= "µs";
        doc["icon"]               = "mdi:timer-outline";
        doc["availability_topic"] = TOPIC_STATUS;
        doc["entity_category"]    = "diagnostic";
        addDevice(doc);
        send("sensor", "ping_us", doc);
    }

    // --- 5. Behälterhöhe (einstellbar via MQTT) ---
    {
        DynamicJsonDocument doc(512);
        doc["unique_id"]          = deviceId_ + "_behaelterhoehe";
        doc["name"]               = "Behälterhöhe";
        doc["state_topic"]        = TOPIC_CFG_HOEHE_STATE;
        doc["command_topic"]      = TOPIC_CFG_HOEHE_SET;
        doc["unit_of_measurement"]= "cm";
        doc["min"]                = 1;
        doc["max"]                = 500;
        doc["step"]               = 0.5;
        doc["icon"]               = "mdi:arrow-expand-vertical";
        doc["availability_topic"] = TOPIC_STATUS;
        doc["entity_category"]    = "config";
        addDevice(doc);
        send("number", "behaelterhoehe", doc);
    }

    // --- 6. Sensor-Offset (einstellbar via MQTT) ---
    {
        DynamicJsonDocument doc(512);
        doc["unique_id"]          = deviceId_ + "_offset";
        doc["name"]               = "Sensor Offset";
        doc["state_topic"]        = TOPIC_CFG_OFFSET_STATE;
        doc["command_topic"]      = TOPIC_CFG_OFFSET_SET;
        doc["unit_of_measurement"]= "cm";
        doc["min"]                = -100;
        doc["max"]                = 100;
        doc["step"]               = 0.5;
        doc["icon"]               = "mdi:tune-vertical";
        doc["availability_topic"] = TOPIC_STATUS;
        doc["entity_category"]    = "config";
        addDevice(doc);
        send("number", "offset", doc);
    }

    // --- 7. WiFi-Signal ---
    {
        DynamicJsonDocument doc(512);
        doc["unique_id"]          = deviceId_ + "_rssi";
        doc["name"]               = "WiFi Signal";
        doc["state_topic"]        = TOPIC_SYSTEM_STATE;
        doc["value_template"]     = "{{ value_json.rssi }}";
        doc["unit_of_measurement"]= "dBm";
        doc["device_class"]       = "signal_strength";
        doc["icon"]               = "mdi:wifi";
        doc["availability_topic"] = TOPIC_STATUS;
        doc["entity_category"]    = "diagnostic";
        addDevice(doc);
        send("sensor", "rssi", doc);
    }

    // --- 8. IP-Adresse ---
    {
        DynamicJsonDocument doc(512);
        doc["unique_id"]          = deviceId_ + "_ip";
        doc["name"]               = "IP-Adresse";
        doc["state_topic"]        = TOPIC_SYSTEM_STATE;
        doc["value_template"]     = "{{ value_json.ip }}";
        doc["icon"]               = "mdi:ip-network";
        doc["availability_topic"] = TOPIC_STATUS;
        doc["entity_category"]    = "diagnostic";
        addDevice(doc);
        send("sensor", "ip_address", doc);
    }

    // --- 9. SSID ---
    {
        DynamicJsonDocument doc(512);
        doc["unique_id"]          = deviceId_ + "_ssid";
        doc["name"]               = "SSID";
        doc["state_topic"]        = TOPIC_SYSTEM_STATE;
        doc["value_template"]     = "{{ value_json.ssid }}";
        doc["icon"]               = "mdi:access-point";
        doc["availability_topic"] = TOPIC_STATUS;
        doc["entity_category"]    = "diagnostic";
        addDevice(doc);
        send("sensor", "ssid", doc);
    }

    // --- 10. Uptime ---
    {
        DynamicJsonDocument doc(512);
        doc["unique_id"]          = deviceId_ + "_uptime";
        doc["name"]               = "Betriebszeit";
        doc["state_topic"]        = TOPIC_SYSTEM_STATE;
        doc["value_template"]     = "{{ value_json.uptime_s }}";
        doc["unit_of_measurement"]= "s";
        doc["device_class"]       = "duration";
        doc["icon"]               = "mdi:clock-outline";
        doc["availability_topic"] = TOPIC_STATUS;
        doc["entity_category"]    = "diagnostic";
        addDevice(doc);
        send("sensor", "uptime", doc);
    }

    // --- 11. Freier Heap ---
    {
        DynamicJsonDocument doc(512);
        doc["unique_id"]          = deviceId_ + "_free_heap";
        doc["name"]               = "Freier Heap";
        doc["state_topic"]        = TOPIC_SYSTEM_STATE;
        doc["value_template"]     = "{{ value_json.free_heap }}";
        doc["unit_of_measurement"]= "B";
        doc["icon"]               = "mdi:memory";
        doc["availability_topic"] = TOPIC_STATUS;
        doc["entity_category"]    = "diagnostic";
        addDevice(doc);
        send("sensor", "free_heap", doc);
    }

    // --- 12. CPU-Frequenz ---
    {
        DynamicJsonDocument doc(512);
        doc["unique_id"]          = deviceId_ + "_cpu_freq";
        doc["name"]               = "CPU-Frequenz";
        doc["state_topic"]        = TOPIC_SYSTEM_STATE;
        doc["value_template"]     = "{{ value_json.cpu_freq_mhz }}";
        doc["unit_of_measurement"]= "MHz";
        doc["icon"]               = "mdi:cpu-32-bit";
        doc["availability_topic"] = TOPIC_STATUS;
        doc["entity_category"]    = "diagnostic";
        addDevice(doc);
        send("sensor", "cpu_freq", doc);
    }

    DebugLogger::getInstance().log(LogLevel::INFO,
        "MQTT HA Discovery published (" + deviceId_ + ")");
}
    // ------------------------------------------------------------------ (discovery entities for system/state appended above)
