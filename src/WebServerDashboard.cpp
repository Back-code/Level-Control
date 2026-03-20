#include "WebServerDashboard.h"
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <LittleFS.h>
#include <Update.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <esp_ota_ops.h>
#include <esp_partition.h>
#include <mbedtls/sha256.h>
#include "ConfigStore.h"
#include "EventBus.h"
#include "MqttManager.h"
#include "SensorManager.h"

namespace {
constexpr char kLatestManifestUrl[] = "https://github.com/Back-code/Salzstand/releases/latest/download/manifest.json";
constexpr char kLatestReleaseUrl[] = "https://github.com/Back-code/Salzstand/releases/latest";
constexpr char kUpdateUserAgent[] = "Salzstand-OTA/1.0";
constexpr uint32_t kRestartDelayMs = 1500;
constexpr size_t kUploadBufferSize = 4096;

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
}

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

        const std::string target = doc["target"] | "";
        if (target != "firmware" && target != "webui" && target != "full") {
            request->send(400, "application/json", "{\"error\":\"Ungültiges Update-Ziel\"}");
            return;
        }

        if (WiFi.status() != WL_CONNECTED) {
            request->send(503, "application/json", "{\"error\":\"Kein WLAN für Repo-Update verfügbar\"}");
            return;
        }

        startRemoteUpdateTask(target);
        request->send(202, "application/json", "{\"status\":\"started\"}");
    });

    server_.on("/api/update/upload/firmware", HTTP_POST,
        [](AsyncWebServerRequest *request) {},
        [this](AsyncWebServerRequest *request, const String& filename, size_t index, uint8_t *data, size_t len, bool final) {
            handleUpload(request, "firmware", filename, index, data, len, final);
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
    doc["availableVersion"] = updateState_.availableVersion;
    doc["received"] = updateState_.received;
    doc["total"] = updateState_.total;
    doc["firmwareMaxSize"] = getFirmwarePartitionSize();
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
        DynamicJsonDocument doc(256);
        doc["error"] = error;
        doc["manifestUrl"] = kLatestManifestUrl;
        std::string json;
        serializeJson(doc, json);
        request->send(502, "application/json", json.c_str());
        return;
    }

    request->send(200, "application/json", rawManifest.c_str());
}

bool WebServerDashboard::fetchLatestManifest(ReleaseManifest& manifest, std::string& rawManifest, std::string& error) {
    WiFiClientSecure client;
    client.setInsecure();

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
    manifest.firmware.name = doc["assets"]["firmware"]["name"] | "";
    manifest.firmware.url = doc["assets"]["firmware"]["url"] | "";
    manifest.firmware.sha256 = toLowerCopy(doc["assets"]["firmware"]["sha256"] | "");
    manifest.firmware.size = doc["assets"]["firmware"]["size"] | 0;
    manifest.webui.name = doc["assets"]["webui"]["name"] | "";
    manifest.webui.url = doc["assets"]["webui"]["url"] | "";
    manifest.webui.sha256 = toLowerCopy(doc["assets"]["webui"]["sha256"] | "");
    manifest.webui.size = doc["assets"]["webui"]["size"] | 0;
    manifest.valid = !manifest.version.empty() && !manifest.firmware.url.empty() && !manifest.webui.url.empty();

    if (!manifest.valid) {
        error = "Manifest enthält nicht alle erforderlichen Assets";
        return false;
    }

    return true;
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
        markUpdateFailed(error);
        return;
    }

    updateState_.availableVersion = manifest.version;

    if ((target == "webui" || target == "full") && !manifest.webui.url.empty()) {
        if (!applyRemoteAsset(manifest.webui, U_SPIFFS, "webui", error)) {
            markUpdateFailed(error);
            return;
        }
    }

    if ((target == "firmware" || target == "full") && !manifest.firmware.url.empty()) {
        if (!applyRemoteAsset(manifest.firmware, U_FLASH, "firmware", error)) {
            markUpdateFailed(error);
            return;
        }
    }

    if (target == "full") {
        markUpdateSucceeded("Firmware und Web-UI wurden erfolgreich aktualisiert");
    } else if (target == "firmware") {
        markUpdateSucceeded("Firmware wurde erfolgreich aktualisiert");
    } else {
        markUpdateSucceeded("Web-UI wurde erfolgreich aktualisiert");
    }

    scheduleRestart(kRestartDelayMs);
}

bool WebServerDashboard::applyRemoteAsset(const ManifestAsset& asset, int command, const std::string& phase, std::string& error) {
    WiFiClientSecure client;
    client.setInsecure();

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

        const size_t chunkSize = stream->readBytes(buffer, std::min(available, sizeof(buffer)));
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
                error = command == U_FLASH ? "Firmware-Asset ist kein gültiges ESP32-Image" : "Web-UI-Asset sieht wie eine Firmware aus";
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

    if (target == "firmware") {
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
        if (target == "firmware" && data[0] != 0xE9) {
            error = "Die hochgeladene Firmware ist kein gültiges ESP32-Binary";
            return false;
        }
        if (target == "webui" && data[0] == 0xE9) {
            error = "Die Web-UI-Datei sieht wie eine Firmware aus";
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
        setUpdatePhase("validating", "Upload wird geprüft", 0, 0);
        EventBus::getInstance().publish({ EventType::OTA_STARTED, std::string(target.c_str()) });

        if (target == "webui") {
            LittleFS.end();
        }

        if (!Update.begin(UPDATE_SIZE_UNKNOWN, target == "firmware" ? U_FLASH : U_SPIFFS)) {
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
        const size_t limit = target == "firmware" ? getFirmwarePartitionSize() : getFilesystemPartitionSize();
        if (limit > 0 && nextReceived > limit) {
            uploadFailed_ = true;
            Update.abort();
            if (target == "webui") {
                LittleFS.begin();
            }
            markUpdateFailed(target == "firmware"
                ? "Firmware-Datei überschreitet die verfügbare App-Partition"
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
            setUpdatePhase("uploading", "Upload läuft", received, 0);
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
    if ((target == "firmware" && finalSize < 65536) || (target == "webui" && finalSize < 4096)) {
        Update.abort();
        if (target == "webui") {
            LittleFS.begin();
        }
        markUpdateFailed(target == "firmware"
            ? "Firmware-Datei ist für ein ESP32-Image unplausibel klein"
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

    const std::string successMessage = target == "firmware"
        ? "Firmware wurde erfolgreich hochgeladen"
        : "Web-UI wurde erfolgreich hochgeladen";
    markUpdateSucceeded(successMessage);
    scheduleRestart(kRestartDelayMs);
    request->send(200, "application/json", (std::string("{\"status\":\"ok\",\"message\":\"") + successMessage + "\"}").c_str());
}

size_t WebServerDashboard::getFirmwarePartitionSize() const {
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