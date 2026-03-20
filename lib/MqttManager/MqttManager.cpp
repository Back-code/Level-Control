#include "MqttManager.h"
#include "DebugLogger.h"
#include "ConfigStore.h"
#include <WiFi.h>
#include <ArduinoJson.h>

#ifdef MQTT_CONNECTED
#undef MQTT_CONNECTED
#endif

#ifdef MQTT_DISCONNECTED
#undef MQTT_DISCONNECTED
#endif

MqttManager& MqttManager::getInstance() {
    static MqttManager instance;
    return instance;
}

MqttManager::MqttManager() : mqttClient_(netClient_) {}

void MqttManager::init(const char* server, uint16_t port) {
    server_ = server ? server : "";
    port_ = port;
    mqttClient_.setServer(server_.c_str(), port_);
    mqttClient_.setKeepAlive(30);
    state_ = MqttConnectionState::DISCONNECTED;

    mqttClientId_ = std::string("salzstand-") + WiFi.macAddress().c_str();
    for (char& c : mqttClientId_) {
        if (c == ':') {
            c = '-';
        }
    }

    Config config;
    if (ConfigStore::getInstance().load(config)) {
        username_ = config.mqtt.user;
        password_ = config.mqtt.password;
        if (password_ == "***") {
            password_.clear();
        }

        if (!username_.empty()) {
            DebugLogger::getInstance().log(LogLevel::INFO, "MQTT auth enabled for user: " + username_);
        } else {
            DebugLogger::getInstance().log(LogLevel::INFO, "MQTT auth disabled");
        }
    }

    DebugLogger::getInstance().log(LogLevel::INFO, "MQTT init " + server_ + ":" + std::to_string(port_));
}

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

    bool ok = false;
    if (!username_.empty()) {
        ok = mqttClient_.connect(mqttClientId_.c_str(), username_.c_str(), password_.c_str());
    } else {
        ok = mqttClient_.connect(mqttClientId_.c_str());
    }

    connecting_ = false;
    if (ok) {
        state_ = MqttConnectionState::CONNECTED;
        consecutiveFailures_ = 0;
        DebugLogger::getInstance().log(LogLevel::INFO, "MQTT connected");
        EventBus::getInstance().publish({EventType::MQTT_CONNECTED, ""});
    } else {
        state_ = MqttConnectionState::DISCONNECTED;
        consecutiveFailures_++;
        if (consecutiveFailures_ == 1 || (consecutiveFailures_ % 6) == 0) {
            DebugLogger::getInstance().log(LogLevel::WARN, "MQTT connect failed, state=" + std::to_string(mqttClient_.state()));
        }
        EventBus::getInstance().publish({EventType::MQTT_DISCONNECTED, ""});
    }
}

void MqttManager::loop() {
    if (mqttClient_.connected()) {
        mqttClient_.loop();
        state_ = MqttConnectionState::CONNECTED;
    }
}

void MqttManager::disconnect() {
    connecting_ = false;
    mqttClient_.disconnect();
    state_ = MqttConnectionState::DISCONNECTED;
    EventBus::getInstance().publish({EventType::MQTT_DISCONNECTED, ""});
}

void MqttManager::publish(const char* topic, const char* payload) {
    if (isConnected()) {
        mqttClient_.publish(topic, payload, true);
    }
}

void MqttManager::setWill(const char* topic, const char* payload) {
    willTopic_ = topic ? topic : "";
    willPayload_ = payload ? payload : "";
}

void MqttManager::publishDiscovery() {
    // Home Assistant MQTT Discovery for sensor
    const char* deviceName = "salzstand";
    const char* baseTopic = "homeassistant/sensor/salzstand";
    const char* stateTopic = "salzstand/sensor/state";

    auto makeDevice = [&]() {
        DynamicJsonDocument d(256);
        d["identifiers"][0] = deviceName;
        d["name"] = "Salzstand";
        d["manufacturer"] = "DIY";
        d["model"] = "ESP32 Salzstand";
        return d;
    };

    // Main fill level sensor
    {
        DynamicJsonDocument doc(512);
        doc["name"] = "Salzstand Füllstand";
        doc["state_topic"] = stateTopic;
        doc["unit_of_measurement"] = "%";
        doc["value_template"] = "{{ value_json.fill_level }}";
        doc["device"] = makeDevice();
        std::string payload;
        serializeJson(doc, payload);
        std::string topic = std::string(baseTopic) + "/fill/config";
        publish(topic.c_str(), payload.c_str());
    }

    // Raw distance sensor (meters)
    {
        DynamicJsonDocument doc(512);
        doc["name"] = "Salzstand Aktuelle Distanz";
        doc["state_topic"] = stateTopic;
        doc["unit_of_measurement"] = "m";
        doc["value_template"] = "{{ value_json.raw_distance_m }}";
        doc["device"] = makeDevice();
        std::string payload;
        serializeJson(doc, payload);
        std::string topic = std::string(baseTopic) + "/raw/config";
        publish(topic.c_str(), payload.c_str());
    }

    // Distance in cm sensor
    {
        DynamicJsonDocument doc(512);
        doc["name"] = "Salzstand Abstand";
        doc["state_topic"] = stateTopic;
        doc["unit_of_measurement"] = "cm";
        doc["value_template"] = "{{ value_json.distance_cm }}";
        doc["device"] = makeDevice();
        std::string payload;
        serializeJson(doc, payload);
        std::string topic = std::string(baseTopic) + "/cm/config";
        publish(topic.c_str(), payload.c_str());
    }
}