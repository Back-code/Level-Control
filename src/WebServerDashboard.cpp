#include "WebServerDashboard.h"
#include <ArduinoJson.h>
#include <LittleFS.h>
#include <WiFi.h>
#include "ConfigStore.h"
#include "MqttManager.h"
#include "SensorManager.h"

WebServerDashboard& WebServerDashboard::getInstance() {
    static WebServerDashboard instance;
    return instance;
}

WebServerDashboard::WebServerDashboard() : server_(80), ws_("/ws") {
    if (!LittleFS.begin()) {
        DebugLogger::getInstance().log(LogLevel::ERROR, "LittleFS mount failed");
    }
    ws_.onEvent([this](AsyncWebSocket *server, AsyncWebSocketClient *client, AwsEventType type, void *arg, uint8_t *data, size_t len) {
        onWsEvent(server, client, type, arg, data, len);
    });
    server_.addHandler(&ws_);
}

void WebServerDashboard::init() {
    setupRoutes();
    DebugLogger::getInstance().setWebSocketHandler([this](const std::string& msg) {
        ws_.textAll(msg.c_str());
    });
}

void WebServerDashboard::start() {
    server_.begin();
    DebugLogger::getInstance().log(LogLevel::INFO, "WebServerDashboard started");
}

void WebServerDashboard::stop() {
    server_.end();
    DebugLogger::getInstance().log(LogLevel::INFO, "WebServerDashboard stopped");
}

void WebServerDashboard::broadcastSensorData() {
    DynamicJsonDocument doc(256);
    doc["type"] = "sensor";
    doc["ping_us"] = SensorManager::getInstance().getLastPingUs();
    doc["valid"] = SensorManager::getInstance().hasValidReading();
    doc["rohdistanz"] = SensorManager::getInstance().getRawDistance();
    doc["salzstandCm"] = SensorManager::getInstance().getDistanceCm();
    doc["salzstandPercent"] = SensorManager::getInstance().getDistancePercent();
    std::string json;
    serializeJson(doc, json);
    ws_.textAll(json.c_str());
}

void WebServerDashboard::broadcastWifiData() {
    DynamicJsonDocument doc(256);
    doc["type"] = "wifi";
    doc["signal"] = WiFi.RSSI();
    String ip = WiFi.localIP().toString();
    String ssid = WiFi.SSID();
    String bssid = WiFi.BSSIDstr();
    doc["ip"] = ip.c_str();
    doc["ssid"] = ssid.c_str();
    doc["bssid"] = bssid.c_str();
    std::string json;
    serializeJson(doc, json);
    ws_.textAll(json.c_str());
}

void WebServerDashboard::broadcastUptime() {
    DynamicJsonDocument doc(128);
    doc["type"] = "uptime";
    doc["uptime"] = millis() / 1000;
    std::string json;
    serializeJson(doc, json);
    ws_.textAll(json.c_str());
}

void WebServerDashboard::setupRoutes() {
    // API routes
    server_.on("/api/config", HTTP_GET, [](AsyncWebServerRequest *request) {
        Config config;
        if (ConfigStore::getInstance().load(config)) {
            DynamicJsonDocument doc(256);
            doc["behaelterhoehe"] = config.behaelterhoehe;
            doc["offset"] = config.offset;
            std::string json;
            serializeJson(doc, json);
            request->send(200, "application/json", json.c_str());
        } else {
            request->send(500, "application/json", "{\"error\":\"Config load failed\"}");
        }
    });

    server_.on("/api/config", HTTP_POST, [](AsyncWebServerRequest *request) {}, NULL,
        [](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
        static std::string body;
        if (index == 0) body = "";
        body += std::string((char*)data, len);
        if (index + len == total) {
            DynamicJsonDocument doc(256);
            deserializeJson(doc, body);
            Config config;
            ConfigStore::getInstance().load(config); // Load current
            config.behaelterhoehe = doc["behaelterhoehe"] | 95.0;
            config.offset = doc["offset"] | 0.0;
            ConfigStore::getInstance().save(config);
            request->send(200, "application/json", "{\"status\":\"ok\"}");
        }
    });

    server_.on("/api/wifi", HTTP_GET, [](AsyncWebServerRequest *request) {
        Config config;
        if (ConfigStore::getInstance().load(config)) {
            DynamicJsonDocument doc(256);
            doc["ssid"] = config.wifi.ssid;
            doc["password"] = "";
            doc["staticIp"]["ip"] = config.staticIp.ip;
            doc["staticIp"]["gateway"] = config.staticIp.gateway;
            doc["staticIp"]["subnet"] = config.staticIp.subnet;
            doc["staticIp"]["dns"] = config.staticIp.dns;
            std::string json;
            serializeJson(doc, json);
            request->send(200, "application/json", json.c_str());
        } else {
            request->send(500, "application/json", "{\"error\":\"WiFi config load failed\"}");
        }
    });

    server_.on("/api/wifi", HTTP_POST, [](AsyncWebServerRequest *request) {}, NULL,
        [](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
        static std::string body;
        if (index == 0) body = "";
        body += std::string((char*)data, len);
        if (index + len == total) {
            DynamicJsonDocument doc(256);
            deserializeJson(doc, body);
            Config config;
            ConfigStore::getInstance().load(config); // Load current
            config.wifi.ssid = doc["ssid"] | "";
            std::string newWifiPassword = doc["password"] | "";
            if (!newWifiPassword.empty() && newWifiPassword != "***") {
                config.wifi.password = newWifiPassword;
            }
            config.staticIp.ip = doc["staticIp"]["ip"] | "";
            config.staticIp.gateway = doc["staticIp"]["gateway"] | "";
            config.staticIp.subnet = doc["staticIp"]["subnet"] | "";
            config.staticIp.dns = doc["staticIp"]["dns"] | "";
            ConfigStore::getInstance().save(config);
            request->send(200, "application/json", "{\"status\":\"ok\"}");
        }
    });

    server_.on("/api/mqtt", HTTP_GET, [](AsyncWebServerRequest *request) {
        Config config;
        if (ConfigStore::getInstance().load(config)) {
            DynamicJsonDocument doc(256);
            doc["server"] = config.mqtt.server;
            doc["port"] = config.mqtt.port;
            doc["user"] = config.mqtt.user;
            doc["password"] = config.mqtt.password.empty() ? "" : "***";
            doc["hasPassword"] = !config.mqtt.password.empty();
            doc["discovery"] = config.mqtt.discovery;
            std::string json;
            serializeJson(doc, json);
            request->send(200, "application/json", json.c_str());
        } else {
            request->send(500, "application/json", "{\"error\":\"MQTT config load failed\"}");
        }
    });

    server_.on("/api/mqtt", HTTP_POST, [](AsyncWebServerRequest *request) {}, NULL,
        [](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
        static std::string body;
        if (index == 0) body = "";
        body += std::string((char*)data, len);
        if (index + len == total) {
            DynamicJsonDocument doc(256);
            deserializeJson(doc, body);
            Config config;
            ConfigStore::getInstance().load(config); // Load current
            config.mqtt.server = doc["server"] | "";
            config.mqtt.port = doc["port"] | 1883;
            config.mqtt.user = doc["user"] | "";
            std::string newMqttPassword = doc["password"] | "";
            if (!newMqttPassword.empty() && newMqttPassword != "***") {
                config.mqtt.password = newMqttPassword;
            }
            config.mqtt.discovery = doc["discovery"] | true;
            ConfigStore::getInstance().save(config);

            // Re-init/connect MQTT with the new settings
            if (!config.mqtt.server.empty()) {
                MqttManager::getInstance().init(config.mqtt.server.c_str(), config.mqtt.port);
                MqttManager::getInstance().connect();
            }

            request->send(200, "application/json", "{\"status\":\"ok\"}");
        }
    });

    server_.on("/api/restart", HTTP_POST, [](AsyncWebServerRequest *request) {
        request->send(200, "application/json", "{\"status\":\"restarting\"}");
        delay(1000);
        ESP.restart();
    });

    server_.on("/api/nvs", HTTP_GET, [](AsyncWebServerRequest *request) {
        // Return NVS content as JSON
        Config config;
        if (ConfigStore::getInstance().load(config)) {
            DynamicJsonDocument doc(512);
            doc["version"] = config.version;
            doc["wifi"]["ssid"] = config.wifi.ssid;
            doc["wifi"]["password"] = "***"; // Don't expose password
            doc["staticIp"]["ip"] = config.staticIp.ip;
            doc["staticIp"]["gateway"] = config.staticIp.gateway;
            doc["staticIp"]["subnet"] = config.staticIp.subnet;
            doc["staticIp"]["dns"] = config.staticIp.dns;
            doc["mqtt"]["server"] = config.mqtt.server;
            doc["mqtt"]["port"] = config.mqtt.port;
            doc["mqtt"]["user"] = config.mqtt.user;
            doc["mqtt"]["password"] = "***"; // Don't expose password
            doc["mqtt"]["discovery"] = config.mqtt.discovery;
            doc["behaelterhoehe"] = config.behaelterhoehe;
            doc["offset"] = config.offset;
            std::string json;
            serializeJson(doc, json);
            request->send(200, "application/json", json.c_str());
        } else {
            request->send(500, "application/json", "{\"error\":\"NVS load failed\"}");
        }
    });

    // Serve static files from LittleFS (nach API-Routen registrieren)
    server_.serveStatic("/", LittleFS, "/").setDefaultFile("index.html");
}

void WebServerDashboard::onWsEvent(AsyncWebSocket *server, AsyncWebSocketClient *client, AwsEventType type, void *arg, uint8_t *data, size_t len) {
    if (type == WS_EVT_CONNECT) {
        DebugLogger::getInstance().log(LogLevel::INFO, "WebSocket client connected");
    } else if (type == WS_EVT_DISCONNECT) {
        DebugLogger::getInstance().log(LogLevel::INFO, "WebSocket client disconnected");
    }
}