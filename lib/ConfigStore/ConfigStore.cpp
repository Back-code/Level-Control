#include "ConfigStore.h"
#include <Preferences.h>
#include <ArduinoJson.h>

ConfigStore& ConfigStore::getInstance() {
    static ConfigStore instance;
    return instance;
}

ConfigStore::ConfigStore() {}

bool ConfigStore::load(Config& config) {
    Preferences prefs;
    if (!prefs.begin(nvsNamespace_, true)) return false; // Read-only

    std::string jsonStr = prefs.getString("config", "").c_str();
    prefs.end();

    if (jsonStr.empty()) return false;

    DynamicJsonDocument doc(1024);
    deserializeJson(doc, jsonStr);

    config.version = doc["version"] | 1;
    config.wifi.ssid = doc["wifi"]["ssid"] | "";
    config.wifi.password = doc["wifi"]["password"] | "";
    config.staticIp.ip = doc["staticIp"]["ip"] | "";
    config.staticIp.gateway = doc["staticIp"]["gateway"] | "";
    config.staticIp.subnet = doc["staticIp"]["subnet"] | "";
    config.staticIp.dns = doc["staticIp"]["dns"] | "";
    config.mqtt.server = doc["mqtt"]["server"] | "";
    config.mqtt.port = doc["mqtt"]["port"] | 1883;
    config.mqtt.user = doc["mqtt"]["user"] | "";
    config.mqtt.password = doc["mqtt"]["password"] | "";
    config.mqtt.discovery = doc["mqtt"]["discovery"] | true;
    config.behaelterhoehe = doc["behaelterhoehe"] | 95.0;
    config.offset = doc["offset"] | 0.0;

    return true;
}

bool ConfigStore::save(const Config& config) {
    Preferences prefs;
    if (!prefs.begin(nvsNamespace_, false)) return false; // Read-write

    DynamicJsonDocument doc(1024);
    doc["version"] = config.version;
    doc["wifi"]["ssid"] = config.wifi.ssid;
    doc["wifi"]["password"] = config.wifi.password;
    doc["staticIp"]["ip"] = config.staticIp.ip;
    doc["staticIp"]["gateway"] = config.staticIp.gateway;
    doc["staticIp"]["subnet"] = config.staticIp.subnet;
    doc["staticIp"]["dns"] = config.staticIp.dns;
    doc["mqtt"]["server"] = config.mqtt.server;
    doc["mqtt"]["port"] = config.mqtt.port;
    doc["mqtt"]["user"] = config.mqtt.user;
    doc["mqtt"]["password"] = config.mqtt.password;
    doc["mqtt"]["discovery"] = config.mqtt.discovery;
    doc["behaelterhoehe"] = config.behaelterhoehe;
    doc["offset"] = config.offset;

    std::string jsonStr;
    serializeJson(doc, jsonStr);

    bool success = prefs.putString("config", jsonStr.c_str());
    prefs.end();
    return success;
}