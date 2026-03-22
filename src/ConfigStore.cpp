#include "ConfigStore.h"
#include <Preferences.h>
#include <ArduinoJson.h>

namespace {
std::string normalizeReminderCycle(const std::string& value) {
    if (value == "week" || value == "month") {
        return value;
    }
    return "day";
}

std::string reminderCycleFromLegacyMinutes(unsigned long cycleMinutes) {
    if (cycleMinutes >= 43200UL) {
        return "month";
    }
    if (cycleMinutes >= 10080UL) {
        return "week";
    }
    return "day";
}

int normalizeReminderWeekday(int value) {
    if (value < 1 || value > 7) {
        return 1;
    }
    return value;
}

unsigned long legacyMinutesFromReminderCycle(const std::string& cycle) {
    if (cycle == "week") {
        return 10080UL;
    }
    if (cycle == "month") {
        return 43200UL;
    }
    return 1440UL;
}
}

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
    // Rueckwaertskompatibilitaet: altes NVS ohne das Feld -> Zertifikatspruefung deaktivieren,
    // damit bestehende SMTP-Konfigurationen nicht sofort brechen.
    config.push.smtpSkipCertVerify = doc["push"]["smtpSkipCertVerify"] | true;
    config.push.authUser = doc["push"]["authUser"] | "";
    config.push.authPassword = doc["push"]["authPassword"] | "";
    config.push.senderName = doc["push"]["senderName"] | "Salzstand Control";
    config.push.senderEmail = doc["push"]["senderEmail"] | "";
    config.push.recipientEmail = doc["push"]["recipientEmail"] | "";
    config.push.triggerPercent = doc["push"]["triggerPercent"] | 20.0f;
    config.push.sendHour = doc["push"]["sendHour"] | 8;
    config.push.sendMinute = doc["push"]["sendMinute"] | 0;
    const unsigned long legacyCycleMinutes = doc["push"]["cycleMinutes"] | 1440UL;
    const std::string storedReminderCycle = doc["push"]["reminderCycle"] | "";
    config.push.reminderCycle = storedReminderCycle.empty()
        ? reminderCycleFromLegacyMinutes(legacyCycleMinutes)
        : normalizeReminderCycle(storedReminderCycle);
    config.push.reminderWeekday = normalizeReminderWeekday(doc["push"]["reminderWeekday"] | 1);
    config.push.subjectTemplate = doc["push"]["subjectTemplate"]
        | "Salzstand Control Warnung: Stand hat {level_percent}% erreicht. Salz nachfüllen!";
    config.push.bodyTemplate = doc["push"]["bodyTemplate"]
        | "Der Füllstand hat {level_percent}% ({level_cm} cm) erreicht.\nBitte Salz nachfüllen!\nDein Salzstand Control";
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
    if (config.push.senderName.empty()) {
        config.push.senderName = "Salzstand Control";
    }
    config.push.reminderCycle = normalizeReminderCycle(config.push.reminderCycle);
    config.push.reminderWeekday = normalizeReminderWeekday(config.push.reminderWeekday);
    if (config.push.subjectTemplate.empty()) {
        config.push.subjectTemplate = "Salzstand Control Warnung: Stand hat {level_percent}% erreicht. Salz nachfüllen!";
    }
    if (config.push.bodyTemplate.empty()) {
        config.push.bodyTemplate = "Der Füllstand hat {level_percent}% ({level_cm} cm) erreicht.\nBitte Salz nachfüllen!\nDein Salzstand Control";
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
    doc["push"]["smtpSkipCertVerify"] = config.push.smtpSkipCertVerify;
    doc["push"]["authUser"] = config.push.authUser;
    doc["push"]["authPassword"] = config.push.authPassword;
    doc["push"]["senderName"] = config.push.senderName.empty() ? "Salzstand Control" : config.push.senderName;
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
    const std::string reminderCycle = normalizeReminderCycle(config.push.reminderCycle);
    doc["push"]["reminderCycle"] = reminderCycle;
    doc["push"]["reminderWeekday"] = normalizeReminderWeekday(config.push.reminderWeekday);
    doc["push"]["cycleMinutes"] = legacyMinutesFromReminderCycle(reminderCycle);
    doc["push"]["subjectTemplate"] = config.push.subjectTemplate.empty()
        ? "Salzstand Control Warnung: Stand hat {level_percent}% erreicht. Salz nachfüllen!"
        : config.push.subjectTemplate;
    doc["push"]["bodyTemplate"] = config.push.bodyTemplate.empty()
        ? "Der Füllstand hat {level_percent}% ({level_cm} cm) erreicht.\nBitte Salz nachfüllen!\nDein Salzstand Control"
        : config.push.bodyTemplate;
    doc["behaelterhoehe"] = config.behaelterhoehe;
    doc["offset"] = config.offset;
    doc["sampleIntervalSeconds"] = config.sampleIntervalSeconds < 5UL ? 5UL : config.sampleIntervalSeconds;

    std::string jsonStr;
    serializeJson(doc, jsonStr);

    bool success = prefs.putString("config", jsonStr.c_str());
    prefs.end();
    return success;
}