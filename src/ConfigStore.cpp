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

    DynamicJsonDocument doc(3072);
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
    config.push.enabled = doc["push"]["enabled"] | false;
    config.push.smtpServer = doc["push"]["smtpServer"] | "";
    config.push.smtpPort = doc["push"]["smtpPort"] | 587;
    config.push.useSsl = doc["push"]["useSsl"] | false;
    config.push.startTls = doc["push"]["startTls"] | false;
    config.push.authUser = doc["push"]["authUser"] | "";
    config.push.authPassword = doc["push"]["authPassword"] | "";
    config.push.senderName = doc["push"]["senderName"] | "";
    config.push.senderEmail = doc["push"]["senderEmail"] | "";
    config.push.recipientEmail = doc["push"]["recipientEmail"] | "";
    config.push.triggerPercent = doc["push"]["triggerPercent"] | 20.0f;
    config.push.sendHour = doc["push"]["sendHour"] | 8;
    config.push.sendMinute = doc["push"]["sendMinute"] | 0;
    config.push.cycleMinutes = doc["push"]["cycleMinutes"] | 1440UL;
    config.push.subjectTemplate = doc["push"]["subjectTemplate"] | "Salzstand Warnung: {level_percent}%";
    config.push.bodyTemplate = doc["push"]["bodyTemplate"] | "Der Fuellstand liegt bei {level_percent}% ({level_cm} cm).";
    if (config.push.smtpPort <= 0) {
        config.push.smtpPort = 587;
    }
    if (config.push.triggerPercent < 0.0f) {
        config.push.triggerPercent = 0.0f;
    }
    if (config.push.triggerPercent > 100.0f) {
        config.push.triggerPercent = 100.0f;
    }
    if (config.push.sendHour < 0 || config.push.sendHour > 23) {
        config.push.sendHour = 8;
    }
    if (config.push.sendMinute < 0 || config.push.sendMinute > 59) {
        config.push.sendMinute = 0;
    }
    if (config.push.cycleMinutes < 1UL) {
        config.push.cycleMinutes = 1UL;
    }
    config.behaelterhoehe = doc["behaelterhoehe"] | 95.0;
    config.offset = doc["offset"] | 0.0;
    config.sampleIntervalSeconds = doc["sampleIntervalSeconds"] | 5UL;
    if (config.sampleIntervalSeconds < 5UL) {
        config.sampleIntervalSeconds = 5UL;
    }

    return true;
}

bool ConfigStore::save(const Config& config) {
    Preferences prefs;
    if (!prefs.begin(nvsNamespace_, false)) return false; // Read-write

    DynamicJsonDocument doc(3072);
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
    doc["push"]["enabled"] = config.push.enabled;
    doc["push"]["smtpServer"] = config.push.smtpServer;
    doc["push"]["smtpPort"] = config.push.smtpPort <= 0 ? 587 : config.push.smtpPort;
    doc["push"]["useSsl"] = config.push.useSsl;
    doc["push"]["startTls"] = config.push.startTls;
    doc["push"]["authUser"] = config.push.authUser;
    doc["push"]["authPassword"] = config.push.authPassword;
    doc["push"]["senderName"] = config.push.senderName;
    doc["push"]["senderEmail"] = config.push.senderEmail;
    doc["push"]["recipientEmail"] = config.push.recipientEmail;
    doc["push"]["triggerPercent"] = config.push.triggerPercent < 0.0f
        ? 0.0f
        : (config.push.triggerPercent > 100.0f ? 100.0f : config.push.triggerPercent);
    doc["push"]["sendHour"] = config.push.sendHour < 0
        ? 0
        : (config.push.sendHour > 23 ? 23 : config.push.sendHour);
    doc["push"]["sendMinute"] = config.push.sendMinute < 0
        ? 0
        : (config.push.sendMinute > 59 ? 59 : config.push.sendMinute);
    doc["push"]["cycleMinutes"] = config.push.cycleMinutes < 1UL ? 1UL : config.push.cycleMinutes;
    doc["push"]["subjectTemplate"] = config.push.subjectTemplate;
    doc["push"]["bodyTemplate"] = config.push.bodyTemplate;
    doc["behaelterhoehe"] = config.behaelterhoehe;
    doc["offset"] = config.offset;
    doc["sampleIntervalSeconds"] = config.sampleIntervalSeconds < 5UL ? 5UL : config.sampleIntervalSeconds;

    std::string jsonStr;
    serializeJson(doc, jsonStr);

    bool success = prefs.putString("config", jsonStr.c_str());
    prefs.end();
    return success;
}