#include "WebServerDashboard.h"
#include <AsyncJson.h>
#include <ArduinoJson.h>
#include "GeneratedVersion.h"
#include <HTTPClient.h>
#include <LittleFS.h>
#include <Update.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <vector>
#include <esp_ota_ops.h>
#include <esp_partition.h>
#include <mbedtls/base64.h>
#include <mbedtls/md.h>
#include <mbedtls/pk.h>
#include <mbedtls/sha256.h>
#include "ConfigStore.h"
#include "EventBus.h"
#include "MqttManager.h"
#include "OtaTrustedRoots.h"
#include "PushNotificationManager.h"
#include "ReleaseSigningPublicKey.h"
#include "SensorManager.h"
#include "WifiManager.h"

namespace {
constexpr char kLatestManifestUrl[] = "https://github.com/Back-code/Salzstand/releases/latest/download/manifest.json";
constexpr char kLatestReleaseUrl[] = "https://github.com/Back-code/Salzstand/releases/latest";
constexpr char kUpdateUserAgent[] = "Salzstand-OTA/1.0";
constexpr char kPasswordMask[] = "*****";
constexpr unsigned long kMinSampleIntervalSeconds = 5UL;
constexpr uint32_t kRestartDelayMs = 1500;
constexpr size_t kUploadBufferSize = 4096;
constexpr unsigned long kManifestCacheTtlMs = 300000;
constexpr char kManifestSignatureAlgorithm[] = "ECDSA_P256_SHA256";

bool mountLittleFsWithKnownLabels() {
    // Custom partitions often use label "littlefs"; Arduino default is "spiffs".
    if (LittleFS.begin(false, "/littlefs", 10, "littlefs")) {
        return true;
    }
    return LittleFS.begin(false, "/littlefs", 10, "spiffs");
}

struct RemoteUpdateContext {
    WebServerDashboard *dashboard;
    std::string target;
};

std::string toLowerCopy(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return value;
}

bool isMaskedPassword(const std::string& value) {
    return value == "***" || value == kPasswordMask;
}

bool parseVersion(const std::string& version, int& major, int& minor, int& patch) {
    major = 0;
    minor = 0;
    patch = 0;
    if (version.empty()) {
        return false;
    }

    char separator1 = 0;
    char separator2 = 0;
    return sscanf(version.c_str(), "%d%c%d%c%d", &major, &separator1, &minor, &separator2, &patch) == 5
        && separator1 == '.' && separator2 == '.';
}

bool isHttpsUrl(const std::string& url) {
    return url.rfind("https://", 0) == 0;
}

std::string bytesToHex(const unsigned char *data, size_t length) {
    static const char hexChars[] = "0123456789abcdef";
    std::string result;
    result.reserve(length * 2);
    for (size_t index = 0; index < length; ++index) {
        const unsigned char value = data[index];
        result.push_back(hexChars[(value >> 4) & 0x0F]);
        result.push_back(hexChars[value & 0x0F]);
    }
    return result;
}

int compareVersions(const std::string& left, const std::string& right) {
    int leftMajor = 0;
    int leftMinor = 0;
    int leftPatch = 0;
    int rightMajor = 0;
    int rightMinor = 0;
    int rightPatch = 0;

    if (!parseVersion(left, leftMajor, leftMinor, leftPatch) ||
        !parseVersion(right, rightMajor, rightMinor, rightPatch)) {
        return left.compare(right);
    }

    if (leftMajor != rightMajor) {
        return leftMajor < rightMajor ? -1 : 1;
    }
    if (leftMinor != rightMinor) {
        return leftMinor < rightMinor ? -1 : 1;
    }
    if (leftPatch != rightPatch) {
        return leftPatch < rightPatch ? -1 : 1;
    }
    return 0;
}

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
}

WebServerDashboard& WebServerDashboard::getInstance() {
    static WebServerDashboard instance;
    return instance;
}

WebServerDashboard::WebServerDashboard() : server_(80), ws_("/ws") {
    littleFsMounted_ = mountLittleFsWithKnownLabels();
    if (!littleFsMounted_) {
        DebugLogger::getInstance().log(LogLevel::ERROR, "LittleFS mount failed");
    }
    ws_.onEvent([this](AsyncWebSocket *server, AsyncWebSocketClient *client, AwsEventType type, void *arg, uint8_t *data, size_t len) {
        onWsEvent(server, client, type, arg, data, len);
    });
    server_.addHandler(&ws_);
}

bool WebServerDashboard::ensureLittleFsMounted() {
    if (littleFsMounted_) {
        return true;
    }

    littleFsMounted_ = mountLittleFsWithKnownLabels();
    if (littleFsMounted_) {
        DebugLogger::getInstance().log(LogLevel::INFO, "LittleFS remounted");
    } else {
        DebugLogger::getInstance().log(LogLevel::ERROR, "LittleFS remount failed");
    }
    return littleFsMounted_;
}

void WebServerDashboard::init() {
    setupRoutes();
    setupUpdateRoutes();
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

void WebServerDashboard::broadcastMqttState() {
    DynamicJsonDocument doc(128);
    doc["type"] = "mqtt";
    const MqttConnectionState state = MqttManager::getInstance().getState();
    const char* stateStr;
    switch (state) {
        case MqttConnectionState::CONNECTED:    stateStr = "connected"; break;
        case MqttConnectionState::CONNECTING:   stateStr = "connecting"; break;
        case MqttConnectionState::BACKOFF:      stateStr = "backoff"; break;
        case MqttConnectionState::DISCONNECTED: stateStr = "disconnected"; break;
        default:                               stateStr = "uninitialized"; break;
    }
    doc["state"] = stateStr;
    std::string json;
    serializeJson(doc, json);
    ws_.textAll(json.c_str());
}

std::string WebServerDashboard::getInstalledVersion() const {
    return SALZSTAND_VERSION;
}

std::string WebServerDashboard::getAvailableVersion(bool forceRefresh) {
    std::string error;
    if (refreshManifestCache(forceRefresh, error)) {
        return cachedManifest_.version;
    }
    return "";
}

std::string WebServerDashboard::getLatestReleaseUrl(bool forceRefresh) {
    std::string error;
    if (refreshManifestCache(forceRefresh, error) && !cachedManifest_.releaseUrl.empty()) {
        return cachedManifest_.releaseUrl;
    }
    return kLatestReleaseUrl;
}

bool WebServerDashboard::isUpdateInProgress() const {
    return updateState_.inProgress;
}

int WebServerDashboard::getUpdateProgressPercent() const {
    if (updateState_.total == 0) {
        return 0;
    }
    return static_cast<int>(std::min<size_t>(100, (updateState_.received * 100) / updateState_.total));
}

bool WebServerDashboard::requestRepoUpdate(const std::string& target, std::string& error) {
    if (updateState_.inProgress || uploadActive_) {
        error = "Update läuft bereits";
        return false;
    }

    if (WiFi.status() != WL_CONNECTED) {
        error = "Kein WLAN für Repo-Update verfügbar";
        return false;
    }

    if (!refreshManifestCache(true, error)) {
        return false;
    }

    const std::string resolvedTarget = resolveUpdateTarget(cachedManifest_, target, error);
    if (resolvedTarget.empty()) {
        return false;
    }

    startRemoteUpdateTask(resolvedTarget);
    return true;
}

void WebServerDashboard::setupRoutes() {
    // API routes
    server_.on("/api/config", HTTP_GET, [](AsyncWebServerRequest *request) {
        Config config;
        if (ConfigStore::getInstance().load(config)) {
            DynamicJsonDocument doc(256);
            doc["behaelterhoehe"] = config.behaelterhoehe;
            doc["offset"] = config.offset;
            doc["sampleIntervalSeconds"] = config.sampleIntervalSeconds;
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
            DynamicJsonDocument doc(384);
            if (deserializeJson(doc, body) != DeserializationError::Ok) {
                request->send(400, "application/json", "{\"error\":\"invalid_config_payload\"}");
                return;
            }
            Config config;
            ConfigStore::getInstance().load(config); // Load current
            config.behaelterhoehe = doc["behaelterhoehe"] | 95.0;
            config.offset = doc["offset"] | 0.0;
            config.sampleIntervalSeconds = doc["sampleIntervalSeconds"] | 5UL;
            if (config.sampleIntervalSeconds < kMinSampleIntervalSeconds) {
                request->send(400, "application/json", "{\"error\":\"sample_interval_too_small\"}");
                return;
            }
            ConfigStore::getInstance().save(config);
            SensorManager::getInstance().setBehaelterhoehe(config.behaelterhoehe);
            SensorManager::getInstance().setOffset(config.offset);
            SensorManager::getInstance().setSampleIntervalSeconds(config.sampleIntervalSeconds);
            request->send(200, "application/json", "{\"status\":\"ok\"}");
        }
    });

    server_.on("/api/wifi", HTTP_GET, [](AsyncWebServerRequest *request) {
        Config config;
        const bool loaded = ConfigStore::getInstance().load(config);

        const std::string configuredSsid = loaded ? config.wifi.ssid : "";
        const std::string runtimeSsid = WiFi.SSID().c_str();
        const std::string runtimePsk = WiFi.psk().c_str();

        bool wifiHasPassword = loaded && !config.wifi.password.empty();
        if (!wifiHasPassword && !runtimePsk.empty()) {
            // Fallback: WLAN-Stack kennt ein PSK, auch wenn es nicht (mehr) in NVS liegt.
            wifiHasPassword = true;
        }

        DynamicJsonDocument doc(384);
        doc["ssid"] = !configuredSsid.empty() ? configuredSsid : runtimeSsid;
        doc["password"] = wifiHasPassword ? kPasswordMask : "";
        doc["hasPassword"] = wifiHasPassword;
        doc["useStaticIp"] = loaded && (!config.staticIp.ip.empty() || !config.staticIp.subnet.empty() || !config.staticIp.dns.empty());
        doc["staticIp"]["ip"] = loaded ? config.staticIp.ip : "";
        doc["staticIp"]["gateway"] = loaded ? config.staticIp.gateway : "";
        doc["staticIp"]["subnet"] = loaded ? config.staticIp.subnet : "";
        doc["staticIp"]["dns"] = loaded ? config.staticIp.dns : "";

        std::string json;
        serializeJson(doc, json);
        request->send(200, "application/json", json.c_str());
    });

    server_.on("/api/wifi/scan", HTTP_POST, [](AsyncWebServerRequest *request) {
        std::vector<WifiNetwork> networks = WifiManager::getInstance().scanNetworks();
        std::sort(networks.begin(), networks.end(), [](const WifiNetwork& left, const WifiNetwork& right) {
            return left.rssi > right.rssi;
        });

        DynamicJsonDocument doc(2048);
        JsonArray items = doc.createNestedArray("networks");
        for (const auto& network : networks) {
            if (network.ssid.empty()) {
                continue;
            }

            bool duplicate = false;
            for (JsonVariant item : items) {
                if (std::string(item["ssid"] | "") == network.ssid) {
                    duplicate = true;
                    break;
                }
            }
            if (duplicate) {
                continue;
            }

            JsonObject entry = items.createNestedObject();
            entry["ssid"] = network.ssid;
            entry["rssi"] = network.rssi;
        }

        std::string json;
        serializeJson(doc, json);
        request->send(200, "application/json", json.c_str());
    });

    server_.on("/api/wifi", HTTP_POST, [](AsyncWebServerRequest *request) {}, NULL,
        [](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
        static std::string body;
        if (index == 0) body = "";
        body += std::string((char*)data, len);
        if (index + len == total) {
            DynamicJsonDocument doc(1024);
            DeserializationError jsonError = deserializeJson(doc, body);
            if (jsonError) {
                request->send(400, "application/json", "{\"error\":\"invalid_wifi_payload\"}");
                return;
            }
            Config config;
            ConfigStore::getInstance().load(config); // Load current
            config.wifi.ssid = doc["ssid"] | "";
            std::string newWifiPassword = doc["password"] | "";
            if (!newWifiPassword.empty() && !isMaskedPassword(newWifiPassword)) {
                config.wifi.password = newWifiPassword;
            }

            const bool useStaticIp = doc["useStaticIp"] | false;
            if (useStaticIp) {
                config.staticIp.ip = doc["staticIp"]["ip"] | "";
                config.staticIp.gateway = doc["staticIp"]["gateway"] | "";
                config.staticIp.subnet = doc["staticIp"]["subnet"] | "";
                config.staticIp.dns = doc["staticIp"]["dns"] | "";

                if (config.staticIp.ip.empty() || config.staticIp.subnet.empty() || config.staticIp.dns.empty()) {
                    request->send(400, "application/json", "{\"error\":\"static_ip_requires_ip_subnet_dns\"}");
                    return;
                }
            } else {
                config.staticIp = {};
            }

            ConfigStore::getInstance().save(config);
            request->send(200, "application/json", "{\"status\":\"ok\"}");
        }
    });

    server_.on("/api/mqtt", HTTP_GET, [](AsyncWebServerRequest *request) {
        Config config;
        if (ConfigStore::getInstance().load(config)) {
            DynamicJsonDocument doc(384);
            doc["server"] = config.mqtt.server;
            doc["port"] = config.mqtt.port;
            doc["user"] = config.mqtt.user;
            doc["password"] = config.mqtt.password.empty() ? "" : kPasswordMask;
            doc["hasPassword"] = !config.mqtt.password.empty();
            doc["discovery"] = config.mqtt.discovery;
            doc["device_id"] = MqttManager::getInstance().getDeviceId();
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
            DynamicJsonDocument doc(768);
            DeserializationError jsonError = deserializeJson(doc, body);
            if (jsonError) {
                request->send(400, "application/json", "{\"error\":\"invalid_mqtt_payload\"}");
                return;
            }
            Config config;
            ConfigStore::getInstance().load(config); // Load current
            config.mqtt.server = doc["server"] | "";
            config.mqtt.port = doc["port"] | 1883;
            config.mqtt.user = doc["user"] | "";
            std::string newMqttPassword = doc["password"] | "";
            if (!newMqttPassword.empty() && !isMaskedPassword(newMqttPassword)) {
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

    server_.on("/api/mqtt/status", HTTP_GET, [](AsyncWebServerRequest *request) {
        DynamicJsonDocument doc(256);
        const MqttConnectionState state = MqttManager::getInstance().getState();
        const char* stateStr;
        switch (state) {
            case MqttConnectionState::CONNECTED:    stateStr = "connected"; break;
            case MqttConnectionState::CONNECTING:   stateStr = "connecting"; break;
            case MqttConnectionState::BACKOFF:      stateStr = "backoff"; break;
            case MqttConnectionState::DISCONNECTED: stateStr = "disconnected"; break;
            default:                               stateStr = "uninitialized"; break;
        }
        Config config;
        ConfigStore::getInstance().load(config);
        doc["state"] = stateStr;
        doc["server"] = config.mqtt.server;
        doc["port"] = config.mqtt.port;
        doc["deviceId"] = MqttManager::getInstance().getDeviceId();
        std::string json;
        serializeJson(doc, json);
        request->send(200, "application/json", json.c_str());
    });

    server_.on("/api/mqtt/reconnect", HTTP_POST, [](AsyncWebServerRequest *request) {
        Config config;
        if (ConfigStore::getInstance().load(config) && !config.mqtt.server.empty()) {
            MqttManager::getInstance().init(config.mqtt.server.c_str(), config.mqtt.port);
            MqttManager::getInstance().connect();
        }
        request->send(200, "application/json", "{\"status\":\"ok\"}");
    });

    server_.on("/api/push", HTTP_GET, [](AsyncWebServerRequest *request) {
        Config config;
        if (ConfigStore::getInstance().load(config)) {
            DynamicJsonDocument doc(1024);
            doc["enabled"] = config.push.enabled;
            doc["smtpServer"] = config.push.smtpServer;
            doc["smtpPort"] = config.push.smtpPort;
            doc["useSsl"] = config.push.useSsl;
            doc["startTls"] = config.push.startTls;
            doc["smtpSkipCertVerify"] = config.push.smtpSkipCertVerify;
            doc["authUser"] = config.push.authUser;
            doc["authPassword"] = config.push.authPassword.empty() ? "" : kPasswordMask;
            doc["hasAuthPassword"] = !config.push.authPassword.empty();
            doc["senderName"] = config.push.senderName;
            doc["senderEmail"] = config.push.senderEmail;
            doc["recipientEmail"] = config.push.recipientEmail;
            doc["triggerPercent"] = config.push.triggerPercent;
            doc["sendHour"] = config.push.sendHour;
            doc["sendMinute"] = config.push.sendMinute;
            doc["reminderCycle"] = config.push.reminderCycle;
            doc["reminderWeekday"] = config.push.reminderWeekday;
            doc["subjectTemplate"] = config.push.subjectTemplate;
            doc["bodyTemplate"] = config.push.bodyTemplate;
            std::string json;
            serializeJson(doc, json);
            request->send(200, "application/json", json.c_str());
        } else {
            request->send(500, "application/json", "{\"error\":\"Push config load failed\"}");
        }
    });

    auto* pushPostHandler = new AsyncCallbackJsonWebHandler("/api/push", [](AsyncWebServerRequest *request, JsonVariant &json) {
        JsonObject doc = json.as<JsonObject>();
        if (doc.isNull()) {
            request->send(400, "application/json", "{\"error\":\"invalid_push_payload\"}");
            return;
        }

        Config config;
        ConfigStore::getInstance().load(config);

        config.push.enabled = doc["enabled"] | false;
        config.push.smtpServer = doc["smtpServer"] | "";
        config.push.smtpPort = doc["smtpPort"] | 587;
        config.push.useSsl = doc["useSsl"] | false;
        config.push.startTls = doc["startTls"] | false;
        if (config.push.useSsl && config.push.startTls) {
            config.push.startTls = false;
        }
        config.push.smtpSkipCertVerify = doc["smtpSkipCertVerify"] | false;
        config.push.authUser = doc["authUser"] | "";

        std::string newAuthPassword = doc["authPassword"] | "";
        if (!newAuthPassword.empty() && !isMaskedPassword(newAuthPassword)) {
            config.push.authPassword = newAuthPassword;
        }

        config.push.senderName = doc["senderName"] | "Salzstand Control";
        config.push.senderEmail = doc["senderEmail"] | "";
        config.push.recipientEmail = doc["recipientEmail"] | "";
        config.push.triggerPercent = doc["triggerPercent"] | 20.0f;
        config.push.sendHour = doc["sendHour"] | 8;
        config.push.sendMinute = doc["sendMinute"] | 0;
        const unsigned long legacyCycleMinutes = doc["cycleMinutes"] | 1440UL;
        const std::string reminderCycle = doc["reminderCycle"] | "";
        config.push.reminderCycle = reminderCycle.empty()
            ? reminderCycleFromLegacyMinutes(legacyCycleMinutes)
            : normalizeReminderCycle(reminderCycle);
        config.push.reminderWeekday = normalizeReminderWeekday(doc["reminderWeekday"] | 1);
        config.push.subjectTemplate = doc["subjectTemplate"]
            | "Salzstand Control Warnung: Stand hat {level_percent}% erreicht. Salz nachfüllen!";
        config.push.bodyTemplate = doc["bodyTemplate"]
            | "Der Füllstand hat {level_percent}% ({level_cm} cm) erreicht.\nBitte Salz nachfüllen!\nDein Salzstand Control";

        if (config.push.smtpPort <= 0) config.push.smtpPort = 587;
        if (config.push.useSsl && config.push.smtpPort == 587) {
            config.push.smtpPort = 465;
        }
        if (config.push.startTls && config.push.smtpPort == 465) {
            config.push.smtpPort = 587;
        }
        if (config.push.triggerPercent < 0.0f) config.push.triggerPercent = 0.0f;
        if (config.push.triggerPercent > 100.0f) config.push.triggerPercent = 100.0f;
        if (config.push.sendHour < 0) config.push.sendHour = 0;
        if (config.push.sendHour > 23) config.push.sendHour = 23;
        if (config.push.sendMinute < 0) config.push.sendMinute = 0;
        if (config.push.sendMinute > 59) config.push.sendMinute = 59;
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

        if (!ConfigStore::getInstance().save(config)) {
            request->send(500, "application/json", "{\"error\":\"push_config_save_failed\"}");
            return;
        }

        request->send(200, "application/json", "{\"status\":\"ok\"}");
    });
    server_.addHandler(pushPostHandler);

    server_.on("/api/push/test", HTTP_GET, [](AsyncWebServerRequest *request) {
        request->send(405, "application/json", "{\"error\":\"method_not_allowed_use_post\"}");
    });

    server_.on("/api/push/test", HTTP_POST, [](AsyncWebServerRequest *request) {
        std::string error;
        if (PushNotificationManager::getInstance().sendTestEmail(error)) {
            request->send(200, "application/json", "{\"status\":\"ok\"}");
            return;
        }

        if (error.empty()) {
            error = "Unbekannter SMTP-Fehler";
        }

        DynamicJsonDocument doc(768);
        doc["error"] = error;
        std::string json;
        serializeJson(doc, json);

        if (json.empty()) {
            request->send(500, "application/json", "{\"error\":\"SMTP-Fehler (Antwort zu gross)\"}");
            return;
        }

        request->send(500, "application/json", json.c_str());
    });

    server_.on("/api/push/smtp-check", HTTP_GET, [](AsyncWebServerRequest *request) {
        request->send(405, "application/json", "{\"error\":\"method_not_allowed_use_post\"}");
    });

    server_.on("/api/push/smtp-check", HTTP_POST, [](AsyncWebServerRequest *request) {
        SmtpDiagResult diagResult = PushNotificationManager::getInstance().smtpDiagnostic();

        DynamicJsonDocument doc(3072);
        doc["success"] = diagResult.success;
        JsonArray steps = doc.createNestedArray("steps");
        for (const auto& step : diagResult.steps) {
            JsonObject s = steps.createNestedObject();
            s["name"] = step.name;
            s["ok"] = step.ok;
            s["detail"] = step.detail;
        }
        std::string json;
        serializeJson(doc, json);
        const int statusCode = diagResult.success ? 200 : 503;
        request->send(statusCode, "application/json", json.c_str());
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
            DynamicJsonDocument doc(1024);
            doc["version"] = config.version;
            doc["wifi"]["ssid"] = config.wifi.ssid;
            doc["wifi"]["password"] = kPasswordMask; // Don't expose password
            doc["staticIp"]["ip"] = config.staticIp.ip;
            doc["staticIp"]["gateway"] = config.staticIp.gateway;
            doc["staticIp"]["subnet"] = config.staticIp.subnet;
            doc["staticIp"]["dns"] = config.staticIp.dns;
            doc["mqtt"]["server"] = config.mqtt.server;
            doc["mqtt"]["port"] = config.mqtt.port;
            doc["mqtt"]["user"] = config.mqtt.user;
            doc["mqtt"]["password"] = kPasswordMask; // Don't expose password
            doc["mqtt"]["discovery"] = config.mqtt.discovery;
            doc["push"]["enabled"] = config.push.enabled;
            doc["push"]["smtpServer"] = config.push.smtpServer;
            doc["push"]["smtpPort"] = config.push.smtpPort;
            doc["push"]["senderEmail"] = config.push.senderEmail;
            doc["push"]["recipientEmail"] = config.push.recipientEmail;
            doc["push"]["authPassword"] = kPasswordMask;
            doc["behaelterhoehe"] = config.behaelterhoehe;
            doc["offset"] = config.offset;
            std::string json;
            serializeJson(doc, json);
            request->send(200, "application/json", json.c_str());
        } else {
            request->send(500, "application/json", "{\"error\":\"NVS load failed\"}");
        }
    });

    // Root explizit bedienen; eigentliche Datei-Auslieferung macht der Static-Handler.
    server_.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
        request->redirect("/index.html");
    });

    // Explizite Route fuer index.html als robuste Absicherung gegen Static-Fallback-Probleme.
    server_.on("/index.html", HTTP_GET, [this](AsyncWebServerRequest *request) {
        if (!ensureLittleFsMounted()) {
            request->send(503, "application/json", "{\"error\":\"ui_not_available\"}");
            return;
        }
        request->send(LittleFS, "/index.html", "text/html");
    });

    // Serve static files from LittleFS (nach API-Routen registrieren)
    server_.serveStatic("/", LittleFS, "/").setDefaultFile("index.html");

    server_.onNotFound([](AsyncWebServerRequest *request) {
        DynamicJsonDocument doc(256);
        doc["error"] = "route_not_found";
        doc["url"] = request->url();
        doc["method"] = request->methodToString();
        std::string json;
        serializeJson(doc, json);
        request->send(404, "application/json", json.c_str());
    });
}

void WebServerDashboard::setupUpdateRoutes() {
    server_.on("/api/update/status", HTTP_GET, [this](AsyncWebServerRequest *request) {
        sendUpdateStatus(request);
    });

    server_.on("/api/update/manifest", HTTP_GET, [this](AsyncWebServerRequest *request) {
        sendUpdateManifest(request);
    });

    server_.on("/api/update/repo", HTTP_POST, [](AsyncWebServerRequest *request) {}, NULL,
        [this](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
        static std::string body;
        if (index == 0) {
            body.clear();
        }
        body.append(reinterpret_cast<char*>(data), len);

        if (index + len != total) {
            return;
        }

        if (updateState_.inProgress || uploadActive_) {
            request->send(409, "application/json", "{\"error\":\"Update läuft bereits\"}");
            return;
        }

        DynamicJsonDocument doc(256);
        if (deserializeJson(doc, body) != DeserializationError::Ok) {
            request->send(400, "application/json", "{\"error\":\"Ungültiger Request-Body\"}");
            return;
        }

        std::string target = doc["target"] | "";
        if (target == "firmware") {
            target = "app";
        }
        if (target.empty()) {
            target = "auto";
        }
        if (target != "app" && target != "webui" && target != "full" && target != "auto") {
            request->send(400, "application/json", "{\"error\":\"Ungültiges Update-Ziel\"}");
            return;
        }

        std::string error;
        if (!requestRepoUpdate(target, error)) {
            request->send(WiFi.status() == WL_CONNECTED ? 409 : 503, "application/json", (std::string("{\"error\":\"") + error + "\"}").c_str());
            return;
        }

        request->send(202, "application/json", "{\"status\":\"started\"}");
    });

    server_.on("/api/update/upload/app", HTTP_POST,
        [](AsyncWebServerRequest *request) {},
        [this](AsyncWebServerRequest *request, const String& filename, size_t index, uint8_t *data, size_t len, bool final) {
            handleUpload(request, "app", filename, index, data, len, final);
        });

    // Backward-compatible alias
    server_.on("/api/update/upload/firmware", HTTP_POST,
        [](AsyncWebServerRequest *request) {},
        [this](AsyncWebServerRequest *request, const String& filename, size_t index, uint8_t *data, size_t len, bool final) {
            handleUpload(request, "app", filename, index, data, len, final);
        });

    server_.on("/api/update/upload/webui", HTTP_POST,
        [](AsyncWebServerRequest *request) {},
        [this](AsyncWebServerRequest *request, const String& filename, size_t index, uint8_t *data, size_t len, bool final) {
            handleUpload(request, "webui", filename, index, data, len, final);
        });
}

void WebServerDashboard::resetUpdateState(const std::string& source, const std::string& target) {
    updateState_ = {};
    updateState_.source = source;
    updateState_.target = target;
    updateState_.phase = "idle";
}

void WebServerDashboard::setUpdatePhase(const std::string& phase, const std::string& message, size_t received, size_t total) {
    updateState_.phase = phase;
    updateState_.message = message;
    updateState_.received = received;
    updateState_.total = total;
}

void WebServerDashboard::markUpdateFailed(const std::string& message) {
    updateState_.inProgress = false;
    updateState_.success = false;
    updateState_.rebootPending = false;
    setUpdatePhase("failed", message, updateState_.received, updateState_.total);
    EventBus::getInstance().publish({ EventType::OTA_FAILED, message });
    DebugLogger::getInstance().log(LogLevel::ERROR, std::string("OTA failed: ") + message);
}

void WebServerDashboard::markUpdateSucceeded(const std::string& message) {
    updateState_.inProgress = false;
    updateState_.success = true;
    updateState_.rebootPending = true;
    setUpdatePhase("done", message, updateState_.total, updateState_.total);
    EventBus::getInstance().publish({ EventType::OTA_SUCCESS, message });
    DebugLogger::getInstance().log(LogLevel::INFO, std::string("OTA success: ") + message);
}

void WebServerDashboard::scheduleRestart(uint32_t delayMs) {
    if (restartScheduled_) {
        return;
    }

    restartScheduled_ = true;
    struct RestartContext {
        uint32_t delayMs;
    };

    auto *context = new RestartContext{ delayMs };
    xTaskCreate([](void *param) {
        auto *ctx = static_cast<RestartContext*>(param);
        vTaskDelay(pdMS_TO_TICKS(ctx->delayMs));
        delete ctx;
        ESP.restart();
    }, "salzstand-restart", 4096, context, 1, nullptr);
}

void WebServerDashboard::sendUpdateStatus(AsyncWebServerRequest *request) const {
    DynamicJsonDocument doc(512);
    doc["inProgress"] = updateState_.inProgress;
    doc["success"] = updateState_.success;
    doc["rebootPending"] = updateState_.rebootPending;
    doc["source"] = updateState_.source;
    doc["target"] = updateState_.target;
    doc["phase"] = updateState_.phase;
    doc["message"] = updateState_.message;
    doc["installedVersion"] = SALZSTAND_VERSION;
    doc["availableVersion"] = updateState_.availableVersion;
    doc["manifestError"] = manifestError_;
    doc["received"] = updateState_.received;
    doc["total"] = updateState_.total;
    doc["appMaxSize"] = getAppPartitionSize();
    doc["firmwareMaxSize"] = getAppPartitionSize();
    doc["webuiMaxSize"] = getFilesystemPartitionSize();
    doc["manifestUrl"] = kLatestManifestUrl;

    std::string json;
    serializeJson(doc, json);
    request->send(200, "application/json", json.c_str());
}

void WebServerDashboard::sendUpdateManifest(AsyncWebServerRequest *request) {
    ReleaseManifest manifest;
    std::string rawManifest;
    std::string error;

    if (!fetchLatestManifest(manifest, rawManifest, error)) {
        manifestError_ = error;
        lastManifestCheckMs_ = millis();
        DynamicJsonDocument doc(256);
        doc["error"] = error;
        doc["manifestUrl"] = kLatestManifestUrl;
        std::string json;
        serializeJson(doc, json);
        request->send(502, "application/json", json.c_str());
        return;
    }

    cachedManifest_ = manifest;
    manifestError_.clear();
    lastManifestCheckMs_ = millis();

    request->send(200, "application/json", rawManifest.c_str());
}

bool WebServerDashboard::fetchLatestManifest(ReleaseManifest& manifest, std::string& rawManifest, std::string& error) {
    if (!isHttpsUrl(kLatestManifestUrl)) {
        error = "Manifest-URL muss HTTPS verwenden";
        return false;
    }

    WiFiClientSecure client;
    client.setCACert(kOtaTrustedRootCAs);

    HTTPClient http;
    http.setTimeout(15000);
    http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
    if (!http.begin(client, kLatestManifestUrl)) {
        error = "Manifest konnte nicht geladen werden";
        return false;
    }

    http.addHeader("Accept", "application/json");
    http.addHeader("User-Agent", kUpdateUserAgent);

    const int statusCode = http.GET();
    if (statusCode != HTTP_CODE_OK) {
        error = "Manifest-Request fehlgeschlagen (HTTP " + std::to_string(statusCode) + ")";
        http.end();
        return false;
    }

    rawManifest = http.getString().c_str();
    http.end();

    DynamicJsonDocument doc(2048);
    if (deserializeJson(doc, rawManifest) != DeserializationError::Ok) {
        error = "Manifest ist kein gültiges JSON";
        return false;
    }

    manifest.version = doc["version"] | "";
    manifest.releaseUrl = doc["releaseUrl"] | kLatestReleaseUrl;
    manifest.app.name = doc["assets"]["app"]["name"] | "";
    manifest.app.url = doc["assets"]["app"]["url"] | "";
    manifest.app.sha256 = toLowerCopy(doc["assets"]["app"]["sha256"] | "");
    manifest.app.size = doc["assets"]["app"]["size"] | 0;
    if (manifest.app.url.empty()) {
        manifest.app.name = doc["assets"]["firmware"]["name"] | "";
        manifest.app.url = doc["assets"]["firmware"]["url"] | "";
        manifest.app.sha256 = toLowerCopy(doc["assets"]["firmware"]["sha256"] | "");
        manifest.app.size = doc["assets"]["firmware"]["size"] | 0;
    }
    manifest.webui.name = doc["assets"]["webui"]["name"] | "";
    manifest.webui.url = doc["assets"]["webui"]["url"] | "";
    manifest.webui.sha256 = toLowerCopy(doc["assets"]["webui"]["sha256"] | "");
    manifest.webui.size = doc["assets"]["webui"]["size"] | 0;
    manifest.signatureAlgorithm = doc["signature"]["algorithm"] | "";
    manifest.signatureValue = doc["signature"]["value"] | "";
    manifest.valid = !manifest.version.empty() && (!manifest.app.url.empty() || !manifest.webui.url.empty());

    if (!manifest.valid) {
        error = "Manifest enthält keine Update-Assets";
        return false;
    }

    if (!verifyManifestSignature(manifest, error)) {
        return false;
    }

    return true;
}

std::string WebServerDashboard::buildManifestSigningPayload(const ReleaseManifest& manifest) const {
    return
        "version=" + manifest.version + "\n" +
        "releaseUrl=" + manifest.releaseUrl + "\n" +
        "app.name=" + manifest.app.name + "\n" +
        "app.url=" + manifest.app.url + "\n" +
        "app.sha256=" + manifest.app.sha256 + "\n" +
        "app.size=" + std::to_string(manifest.app.size) + "\n" +
        "webui.name=" + manifest.webui.name + "\n" +
        "webui.url=" + manifest.webui.url + "\n" +
        "webui.sha256=" + manifest.webui.sha256 + "\n" +
        "webui.size=" + std::to_string(manifest.webui.size);
}

bool WebServerDashboard::verifyManifestSignature(const ReleaseManifest& manifest, std::string& error) const {
    if (manifest.signatureAlgorithm != kManifestSignatureAlgorithm) {
        error = "Manifest-Signaturalgorithmus wird nicht unterstützt";
        return false;
    }
    if (manifest.signatureValue.empty()) {
        error = "Manifest enthält keine Signatur";
        return false;
    }

    const std::string payload = buildManifestSigningPayload(manifest);

    unsigned char hash[32];
    if (mbedtls_sha256_ret(
            reinterpret_cast<const unsigned char*>(payload.data()),
            payload.size(),
            hash,
            0) != 0) {
        error = "SHA256 für Manifest-Signatur fehlgeschlagen";
        return false;
    }

    const size_t maxSigLen = (manifest.signatureValue.size() * 3) / 4 + 4;
    std::vector<unsigned char> signature(maxSigLen);
    size_t signatureLen = 0;
    if (mbedtls_base64_decode(
            signature.data(),
            signature.size(),
            &signatureLen,
            reinterpret_cast<const unsigned char*>(manifest.signatureValue.data()),
            manifest.signatureValue.size()) != 0) {
        error = "Manifest-Signatur ist kein gültiges Base64";
        return false;
    }

    mbedtls_pk_context publicKey;
    mbedtls_pk_init(&publicKey);

    const int parseResult = mbedtls_pk_parse_public_key(
        &publicKey,
        reinterpret_cast<const unsigned char*>(kReleaseSigningPublicKeyPem),
        strlen(kReleaseSigningPublicKeyPem) + 1);
    if (parseResult != 0) {
        mbedtls_pk_free(&publicKey);
        error = "Öffentlicher Release-Schlüssel konnte nicht geladen werden";
        return false;
    }

    const int verifyResult = mbedtls_pk_verify(
        &publicKey,
        MBEDTLS_MD_SHA256,
        hash,
        sizeof(hash),
        signature.data(),
        signatureLen);
    mbedtls_pk_free(&publicKey);

    if (verifyResult != 0) {
        error = "Manifest-Signatur ungültig";
        return false;
    }

    return true;
}

bool WebServerDashboard::refreshManifestCache(bool forceRefresh, std::string& error) {
    const unsigned long now = millis();
    const bool cacheFresh = !forceRefresh && lastManifestCheckMs_ != 0 && (now - lastManifestCheckMs_) < kManifestCacheTtlMs;
    if (cacheFresh) {
        error = manifestError_;
        return cachedManifest_.valid;
    }

    ReleaseManifest manifest;
    std::string rawManifest;
    if (!fetchLatestManifest(manifest, rawManifest, error)) {
        cachedManifest_ = {};
        manifestError_ = error;
        lastManifestCheckMs_ = now;
        return false;
    }

    cachedManifest_ = manifest;
    manifestError_.clear();
    lastManifestCheckMs_ = now;
    return true;
}

std::string WebServerDashboard::resolveUpdateTarget(const ReleaseManifest& manifest, const std::string& requestedTarget, std::string& error) const {
    const bool hasApp = !manifest.app.url.empty();
    const bool hasWebUi = !manifest.webui.url.empty();
    const bool newerVersion = compareVersions(manifest.version, getInstalledVersion()) > 0;

    if (requestedTarget == "auto" || requestedTarget.empty()) {
        if (!newerVersion) {
            error = "Keine neuere Version verfügbar";
            return "";
        }
        if (hasApp && hasWebUi) {
            return "full";
        }
        if (hasApp) {
            return "app";
        }
        if (hasWebUi) {
            return "webui";
        }

        error = "Manifest enthält keine verfügbaren Update-Assets";
        return "";
    }

    if (requestedTarget == "app") {
        if (hasApp) {
            return "app";
        }
        error = "Im Manifest ist kein App-Update vorhanden";
        return "";
    }

    if (requestedTarget == "webui") {
        if (hasWebUi) {
            return "webui";
        }
        error = "Im Manifest ist kein Web-UI-Update vorhanden";
        return "";
    }

    if (requestedTarget == "full") {
        if (hasApp && hasWebUi) {
            return "full";
        }
        error = "Für ein Komplett-Update fehlen Assets im Manifest";
        return "";
    }

    error = "Ungültiges Update-Ziel";
    return "";
}

void WebServerDashboard::startRemoteUpdateTask(const std::string& target) {
    resetUpdateState("repo", target);
    updateState_.inProgress = true;
    setUpdatePhase("queued", "Repo-Update wird vorbereitet");
    EventBus::getInstance().publish({ EventType::OTA_STARTED, target });

    auto *context = new RemoteUpdateContext{ this, target };
    if (xTaskCreate([](void *param) {
            auto *ctx = static_cast<RemoteUpdateContext*>(param);
            ctx->dashboard->runRemoteUpdateTask(ctx->target);
            delete ctx;
            vTaskDelete(nullptr);
        }, "salzstand-ota", 16384, context, 1, nullptr) != pdPASS) {
        delete context;
        markUpdateFailed("OTA-Task konnte nicht gestartet werden");
    }
}

void WebServerDashboard::runRemoteUpdateTask(const std::string& target) {
    ReleaseManifest manifest;
    std::string rawManifest;
    std::string error;

    setUpdatePhase("manifest", "Manifest wird geladen");
    if (!fetchLatestManifest(manifest, rawManifest, error)) {
        manifestError_ = error;
        lastManifestCheckMs_ = millis();
        markUpdateFailed(error);
        return;
    }

    cachedManifest_ = manifest;
    manifestError_.clear();
    lastManifestCheckMs_ = millis();
    updateState_.availableVersion = manifest.version;

    if ((target == "webui" || target == "full") && !manifest.webui.url.empty()) {
        if (!applyRemoteAsset(manifest.webui, U_SPIFFS, "webui", error)) {
            markUpdateFailed(error);
            return;
        }
    }

    if ((target == "app" || target == "full") && !manifest.app.url.empty()) {
        if (!applyRemoteAsset(manifest.app, U_FLASH, "app", error)) {
            markUpdateFailed(error);
            return;
        }
    }

    if (target == "full") {
        markUpdateSucceeded("App und Web-UI wurden erfolgreich aktualisiert");
    } else if (target == "app") {
        markUpdateSucceeded("App wurde erfolgreich aktualisiert");
    } else {
        markUpdateSucceeded("Web-UI wurde erfolgreich aktualisiert");
    }

    scheduleRestart(kRestartDelayMs);
}

bool WebServerDashboard::applyRemoteAsset(const ManifestAsset& asset, int command, const std::string& phase, std::string& error) {
    if (!isHttpsUrl(asset.url)) {
        error = "Asset-URL muss HTTPS verwenden";
        return false;
    }

    WiFiClientSecure client;
    client.setCACert(kOtaTrustedRootCAs);

    HTTPClient http;
    http.setTimeout(20000);
    http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
    if (!http.begin(client, asset.url.c_str())) {
        error = "Asset konnte nicht geöffnet werden";
        return false;
    }

    http.addHeader("User-Agent", kUpdateUserAgent);
    const int statusCode = http.GET();
    if (statusCode != HTTP_CODE_OK) {
        error = "Asset-Download fehlgeschlagen (HTTP " + std::to_string(statusCode) + ")";
        http.end();
        return false;
    }

    const int contentLength = http.getSize();
    const size_t expectedSize = asset.size > 0 ? asset.size : (contentLength > 0 ? static_cast<size_t>(contentLength) : UPDATE_SIZE_UNKNOWN);
    if (asset.size > 0 && contentLength > 0 && asset.size != static_cast<size_t>(contentLength)) {
        error = "Download-Größe passt nicht zum Manifest";
        http.end();
        return false;
    }

    if (command == U_SPIFFS) {
        LittleFS.end();
    }

    if (!Update.begin(expectedSize, command)) {
        error = Update.errorString();
        if (command == U_SPIFFS) {
            LittleFS.begin();
        }
        http.end();
        return false;
    }

    WiFiClient *stream = http.getStreamPtr();
    uint8_t buffer[kUploadBufferSize];
    size_t received = 0;
    bool firstChunk = true;

    mbedtls_sha256_context shaContext;
    mbedtls_sha256_init(&shaContext);
    mbedtls_sha256_starts_ret(&shaContext, 0);

    setUpdatePhase(phase, "Download läuft", 0, expectedSize);

    while (http.connected() && (contentLength < 0 || received < static_cast<size_t>(contentLength))) {
        const size_t available = stream->available();
        if (available == 0) {
            delay(1);
            continue;
        }

        const size_t chunkSize = stream->readBytes(buffer, std::min<size_t>(available, sizeof(buffer)));
        if (chunkSize == 0) {
            continue;
        }

        if (firstChunk) {
            if ((command == U_FLASH && buffer[0] != 0xE9) || (command == U_SPIFFS && buffer[0] == 0xE9)) {
                Update.abort();
                if (command == U_SPIFFS) {
                    LittleFS.begin();
                }
                http.end();
                error = command == U_FLASH ? "App-Asset ist kein gültiges ESP32-Image" : "Web-UI-Asset sieht wie eine App aus";
                mbedtls_sha256_free(&shaContext);
                return false;
            }
            firstChunk = false;
        }

        mbedtls_sha256_update_ret(&shaContext, buffer, chunkSize);
        if (Update.write(buffer, chunkSize) != chunkSize) {
            Update.abort();
            if (command == U_SPIFFS) {
                LittleFS.begin();
            }
            http.end();
            error = Update.errorString();
            mbedtls_sha256_free(&shaContext);
            return false;
        }

        received += chunkSize;
        setUpdatePhase(phase, "Download läuft", received, expectedSize);
    }

    unsigned char shaResult[32];
    mbedtls_sha256_finish_ret(&shaContext, shaResult);
    mbedtls_sha256_free(&shaContext);
    const std::string actualSha = bytesToHex(shaResult, sizeof(shaResult));
    if (!asset.sha256.empty() && actualSha != asset.sha256) {
        Update.abort();
        if (command == U_SPIFFS) {
            LittleFS.begin();
        }
        http.end();
        error = "SHA256-Prüfsumme stimmt nicht mit dem Manifest überein";
        return false;
    }

    if (!Update.end(true)) {
        if (command == U_SPIFFS) {
            LittleFS.begin();
        }
        http.end();
        error = Update.errorString();
        return false;
    }

    http.end();
    return true;
}

bool WebServerDashboard::validateUploadStart(const String& target, const String& filename, size_t contentLength, std::string& error) const {
    const std::string lowerName = toLowerCopy(filename.c_str());
    if (lowerName.size() < 4 || lowerName.substr(lowerName.size() - 4) != ".bin") {
        error = "Es werden nur .bin-Dateien akzeptiert";
        return false;
    }

    if (target == "app") {
        if (lowerName.find("bootloader") != std::string::npos || lowerName.find("partition") != std::string::npos) {
            error = "Bootloader- oder Partitionsdateien dürfen hier nicht hochgeladen werden";
            return false;
        }
    } else {
        if (lowerName.find("bootloader") != std::string::npos || lowerName.find("partition") != std::string::npos) {
            error = "Bootloader- oder Partitionsdateien dürfen hier nicht hochgeladen werden";
            return false;
        }
    }

    return true;
}

bool WebServerDashboard::validateUploadChunk(const String& target, const uint8_t *data, size_t len, size_t index, std::string& error) const {
    if (index == 0 && len > 0) {
        if (target == "app" && data[0] != 0xE9) {
            error = "Die hochgeladene App ist kein gültiges ESP32-Binary";
            return false;
        }
        if (target == "webui" && data[0] == 0xE9) {
            error = "Die Web-UI-Datei sieht wie eine App aus";
            return false;
        }
    }
    return true;
}

void WebServerDashboard::handleUpload(AsyncWebServerRequest *request, const String& target, const String& filename, size_t index, uint8_t *data, size_t len, bool final) {
    if (index == 0) {
        if (updateState_.inProgress || uploadActive_) {
            request->send(409, "application/json", "{\"error\":\"Update läuft bereits\"}");
            return;
        }

        std::string error;
        if (!validateUploadStart(target, filename, request->contentLength(), error)) {
            request->send(400, "application/json", (std::string("{\"error\":\"") + error + "\"}").c_str());
            return;
        }

        uploadActive_ = true;
        uploadFailed_ = false;
        uploadTarget_ = target;
        uploadFilename_ = filename;
        resetUpdateState("upload", target.c_str());
        updateState_.inProgress = true;
        setUpdatePhase("validating", "Upload wird geprüft", 0, request->contentLength());
        EventBus::getInstance().publish({ EventType::OTA_STARTED, std::string(target.c_str()) });

        if (target == "webui") {
            LittleFS.end();
        }

        if (!Update.begin(UPDATE_SIZE_UNKNOWN, target == "app" ? U_FLASH : U_SPIFFS)) {
            uploadFailed_ = true;
            uploadActive_ = false;
            if (target == "webui") {
                LittleFS.begin();
            }
            markUpdateFailed(Update.errorString());
            request->send(500, "application/json", (std::string("{\"error\":\"") + Update.errorString() + "\"}").c_str());
            return;
        }
    }

    if (!uploadActive_ || uploadTarget_ != target) {
        return;
    }

    std::string error;
    if (!uploadFailed_ && !validateUploadChunk(target, data, len, index, error)) {
        uploadFailed_ = true;
        Update.abort();
        if (target == "webui") {
            LittleFS.begin();
        }
        markUpdateFailed(error);
    }

    if (!uploadFailed_ && len > 0) {
        const size_t nextReceived = index + len;
        const size_t limit = target == "app" ? getAppPartitionSize() : getFilesystemPartitionSize();
        if (limit > 0 && nextReceived > limit) {
            uploadFailed_ = true;
            Update.abort();
            if (target == "webui") {
                LittleFS.begin();
            }
            markUpdateFailed(target == "app"
                ? "App-Datei überschreitet die verfügbare App-Partition"
                : "Web-UI-Datei überschreitet die LittleFS-Partition");
        }
    }

    if (!uploadFailed_ && len > 0) {
        if (Update.write(data, len) != len) {
            uploadFailed_ = true;
            Update.abort();
            if (target == "webui") {
                LittleFS.begin();
            }
            markUpdateFailed(Update.errorString());
        } else {
            const size_t received = index + len;
            setUpdatePhase("uploading", "Upload läuft", received, request->contentLength());
        }
    }

    if (!final) {
        return;
    }

    uploadActive_ = false;

    if (uploadFailed_) {
        request->send(400, "application/json", (std::string("{\"error\":\"") + updateState_.message + "\"}").c_str());
        return;
    }

    const size_t finalSize = index + len;
    if ((target == "app" && finalSize < 65536) || (target == "webui" && finalSize < 4096)) {
        Update.abort();
        if (target == "webui") {
            LittleFS.begin();
        }
        markUpdateFailed(target == "app"
            ? "App-Datei ist für ein ESP32-Image unplausibel klein"
            : "Web-UI-Datei ist unplausibel klein");
        request->send(400, "application/json", (std::string("{\"error\":\"") + updateState_.message + "\"}").c_str());
        return;
    }

    if (!Update.end(true)) {
        if (target == "webui") {
            LittleFS.begin();
        }
        markUpdateFailed(Update.errorString());
        request->send(500, "application/json", (std::string("{\"error\":\"") + Update.errorString() + "\"}").c_str());
        return;
    }

    const std::string successMessage = target == "app"
        ? "App wurde erfolgreich hochgeladen"
        : "Web-UI wurde erfolgreich hochgeladen";
    markUpdateSucceeded(successMessage);
    scheduleRestart(kRestartDelayMs);
    request->send(200, "application/json", (std::string("{\"status\":\"ok\",\"message\":\"") + successMessage + "\"}").c_str());
}

size_t WebServerDashboard::getAppPartitionSize() const {
    const esp_partition_t *partition = esp_ota_get_next_update_partition(nullptr);
    return partition ? partition->size : 0;
}

size_t WebServerDashboard::getFilesystemPartitionSize() const {
    const esp_partition_t *partition = esp_partition_find_first(
        ESP_PARTITION_TYPE_DATA,
        ESP_PARTITION_SUBTYPE_DATA_SPIFFS,
        nullptr);
    return partition ? partition->size : 0;
}

void WebServerDashboard::onWsEvent(AsyncWebSocket *server, AsyncWebSocketClient *client, AwsEventType type, void *arg, uint8_t *data, size_t len) {
    if (type == WS_EVT_CONNECT) {
        DebugLogger::getInstance().log(LogLevel::INFO, "WebSocket client connected");
    } else if (type == WS_EVT_DISCONNECT) {
        DebugLogger::getInstance().log(LogLevel::INFO, "WebSocket client disconnected");
    }
}