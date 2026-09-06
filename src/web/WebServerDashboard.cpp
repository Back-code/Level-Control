#include <string>
#include "WebServerDashboard.h"
#include <AsyncJson.h>
#include <ArduinoJson.h>
#include "GeneratedVersion.h"
#include <HTTPClient.h>
#include <LittleFS.h>
#include <Preferences.h>
#include <Update.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <algorithm>
#include <cctype>
#include <climits>
#include <cstdio>
#include <cstring>
#include <sstream>
#include <vector>
#include <esp_ota_ops.h>
#include <esp_partition.h>
#include <esp_system.h>
#include <mbedtls/md.h>
#include <mbedtls/pk.h>
#include <mbedtls/sha256.h>
#include "ConfigStore.h"
#include "EventBus.h"
#include "HistoryManager.h"
#include "MqttManager.h"
#include "OtaTrustedRoots.h"
#include "PushNotificationManager.h"
#include "ReleaseSigningPublicKey.h"
#include "SensorManager.h"
#include "WifiManager.h"

namespace {
constexpr char kLatestReleaseApiUrl[] = LEVEL_CONTROL_LATEST_RELEASE_API_URL;
constexpr char kLatestReleaseUrl[] = LEVEL_CONTROL_LATEST_RELEASE_URL;
constexpr char kReleaseTagBaseUrl[] = LEVEL_CONTROL_RELEASE_TAG_BASE_URL;
constexpr char kUpdateUserAgent[] = "Level-Control-OTA/1.0";
constexpr char kPasswordMask[] = "*****";
constexpr unsigned long kMinSampleIntervalSeconds = 5UL;
constexpr uint32_t kRestartDelayMs = 2500;
constexpr size_t kUploadBufferSize = 4096;
constexpr unsigned long kManifestCacheTtlMs = 300000;
constexpr unsigned long kUploadStallTimeoutMs = 300000;
constexpr unsigned long kAdminSessionTtlMs = 8UL * 60UL * 60UL * 1000UL;
constexpr size_t kMaxConfigBodyBytes = 2048;
constexpr size_t kMaxWifiBodyBytes = 4096;
constexpr size_t kMaxMqttBodyBytes = 2048;
constexpr size_t kMaxImportBodyBytes = 48 * 1024;
constexpr size_t kMaxUpdateRequestBytes = 512;
constexpr size_t kEmbeddedSigBytes = 72;
constexpr size_t kEmbeddedSigTrailerSize = 8 + 1 + kEmbeddedSigBytes;
const uint8_t kEmbeddedSigMagic[8] = { 'L', 'C', 'S', 'I', 'G', 'V', '1', '!' };

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

std::string bytesToHex(const unsigned char *data, size_t length);

std::string sha256Hex(const std::string& value) {
    unsigned char digest[32];
    mbedtls_sha256_context context;
    mbedtls_sha256_init(&context);
    if (mbedtls_sha256_starts_ret(&context, 0) != 0
        || mbedtls_sha256_update_ret(&context,
            reinterpret_cast<const unsigned char*>(value.data()), value.size()) != 0
        || mbedtls_sha256_finish_ret(&context, digest) != 0) {
        mbedtls_sha256_free(&context);
        return "";
    }
    mbedtls_sha256_free(&context);
    return bytesToHex(digest, sizeof(digest));
}

bool parseVersionNumberPart(const std::string& text, size_t& pos, int& value) {
    if (pos >= text.size() || !isdigit(static_cast<unsigned char>(text[pos]))) {
        return false;
    }

    int parsed = 0;
    while (pos < text.size() && isdigit(static_cast<unsigned char>(text[pos]))) {
        const int digit = text[pos] - '0';
        // Prevent integer overflow while parsing malformed version strings.
        if (parsed > (INT_MAX - digit) / 10) {
            return false;
        }
        parsed = parsed * 10 + digit;
        ++pos;
    }

    value = parsed;
    return true;
}

bool parseVersion(const std::string& version, int& major, int& minor, int& patch) {
    major = 0;
    minor = 0;
    patch = 0;
    if (version.empty()) {
        return false;
    }

    size_t pos = 0;
    if (!parseVersionNumberPart(version, pos, major)) {
        return false;
    }
    if (pos >= version.size() || version[pos] != '.') {
        return false;
    }
    ++pos;

    if (!parseVersionNumberPart(version, pos, minor)) {
        return false;
    }
    if (pos >= version.size() || version[pos] != '.') {
        return false;
    }
    ++pos;

    if (!parseVersionNumberPart(version, pos, patch)) {
        return false;
    }

    return pos == version.size();
}

bool isHttpsUrl(const std::string& url) {
    return url.rfind("https://", 0) == 0;
}

std::string normalizeVersionTag(std::string value) {
    if (!value.empty() && (value[0] == 'v' || value[0] == 'V')) {
        value.erase(0, 1);
    }
    return value;
}

bool hasBinSuffix(const std::string& name) {
    return name.size() >= 4 && name.rfind(".bin") == name.size() - 4;
}

bool isAppAssetName(const std::string& lowerName) {
    if (!hasBinSuffix(lowerName)) {
        return false;
    }
    if (lowerName.find("bootloader") != std::string::npos
        || lowerName.find("partition") != std::string::npos
        || lowerName.find("web-ui") != std::string::npos
        || lowerName.find("webui") != std::string::npos
        || lowerName.find("littlefs") != std::string::npos) {
        return false;
    }
    return lowerName.find("-app.bin") != std::string::npos
        || lowerName.find("firmware") != std::string::npos
        || lowerName.find("app") != std::string::npos;
}

bool isWebUiAssetName(const std::string& lowerName) {
    if (!hasBinSuffix(lowerName)) {
        return false;
    }
    return lowerName.find("-web-ui.bin") != std::string::npos
        || lowerName.find("webui") != std::string::npos
        || lowerName.find("littlefs") != std::string::npos;
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

std::string normalizePushEncryptionMode(const std::string& rawMode) {
    const std::string mode = toLowerCopy(rawMode);
    if (mode == "ssl" || mode == "tls") {
        return "ssl";
    }
    if (mode == "starttls" || mode == "start_tls" || mode == "start-tls") {
        return "starttls";
    }
    return "none";
}

std::string derivePushEncryptionMode(bool useSsl, bool startTls) {
    if (useSsl) {
        return "ssl";
    }
    if (startTls) {
        return "starttls";
    }
    return "none";
}

void applyPushEncryptionModeToConfig(const std::string& mode, bool& useSsl, bool& startTls) {
    const std::string normalized = normalizePushEncryptionMode(mode);
    useSsl = normalized == "ssl";
    startTls = normalized == "starttls";
}

const char* resetReasonToString(esp_reset_reason_t reason) {
    switch (reason) {
        case ESP_RST_UNKNOWN:   return "unknown";
        case ESP_RST_POWERON:   return "poweron";
        case ESP_RST_EXT:       return "external";
        case ESP_RST_SW:        return "software";
        case ESP_RST_PANIC:     return "panic";
        case ESP_RST_INT_WDT:   return "int_wdt";
        case ESP_RST_TASK_WDT:  return "task_wdt";
        case ESP_RST_WDT:       return "wdt";
        case ESP_RST_DEEPSLEEP: return "deepsleep";
        case ESP_RST_BROWNOUT:  return "brownout";
        case ESP_RST_SDIO:      return "sdio";
        default:                return "other";
    }
}

bool ensureConfiguredBootPartition(const esp_partition_t* targetPartition, std::string& error) {
    if (!targetPartition) {
        error = "Keine OTA-Zielpartition gefunden";
        return false;
    }

    esp_err_t setErr = esp_ota_set_boot_partition(targetPartition);
    if (setErr != ESP_OK) {
        error = std::string("Boot-Partition konnte nicht gesetzt werden: ") + esp_err_to_name(setErr);
        return false;
    }

    const esp_partition_t* configuredBoot = esp_ota_get_boot_partition();
    if (!configuredBoot || configuredBoot->address != targetPartition->address) {
        error = "Boot-Partition wurde nach OTA nicht korrekt übernommen";
        return false;
    }

    return true;
}
}

WebServerDashboard& WebServerDashboard::getInstance() {
    static WebServerDashboard instance;
    return instance;
}

bool WebServerDashboard::isAdminAuthenticated(AsyncWebServerRequest *request) const {
    Config config;
    if (!ConfigStore::getInstance().load(config) || config.adminPasswordHash.empty()
        || adminSessionToken_.empty() || (millis() - adminSessionIssuedMs_) > kAdminSessionTtlMs
        || !request->hasHeader("X-Admin-Token")) {
        return false;
    }

    const AsyncWebHeader* header = request->getHeader("X-Admin-Token");
    return header != nullptr && header->value().equals(adminSessionToken_.c_str());
}

bool WebServerDashboard::requireAdmin(AsyncWebServerRequest *request) {
    if (isAdminAuthenticated(request)) {
        return true;
    }

    request->send(401, "application/json", "{\"error\":\"admin_auth_required\"}");
    return false;
}

std::string WebServerDashboard::issueAdminSession() {
    char token[65];
    snprintf(token, sizeof(token), "%08lx%08lx%08lx%08lx",
        static_cast<unsigned long>(esp_random()),
        static_cast<unsigned long>(esp_random()),
        static_cast<unsigned long>(esp_random()),
        static_cast<unsigned long>(esp_random()));
    adminSessionToken_ = token;
    adminSessionIssuedMs_ = millis();
    return adminSessionToken_;
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
    return LEVEL_CONTROL_VERSION;
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
    recoverStuckUploadIfNeeded();

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
    server_.on("/api/auth/status", HTTP_GET, [this](AsyncWebServerRequest *request) {
        Config config;
        const bool configured = ConfigStore::getInstance().load(config) && !config.adminPasswordHash.empty();
        DynamicJsonDocument doc(192);
        doc["configured"] = configured;
        doc["authenticated"] = configured && isAdminAuthenticated(request);
        doc["sessionTtlMs"] = kAdminSessionTtlMs;
        std::string json;
        serializeJson(doc, json);
        request->send(200, "application/json", json.c_str());
    });

    server_.on("/api/auth/logout", HTTP_POST, [this](AsyncWebServerRequest *request) {
        adminSessionToken_.clear();
        adminSessionIssuedMs_ = 0;
        request->send(200, "application/json", "{\"status\":\"ok\"}");
    });

    server_.on("/api/auth/change", HTTP_POST, [](AsyncWebServerRequest *request) {}, nullptr,
        [this](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
            if (index == 0 && !requireAdmin(request)) return;
            auto *body = static_cast<std::string*>(request->_tempObject);
            if (index == 0) {
                delete body;
                body = new std::string();
                body->reserve(std::min(total, static_cast<size_t>(512)));
                request->_tempObject = body;
            }
            if (body == nullptr || total > 512) {
                request->send(413, "application/json", "{\"error\":\"auth_payload_too_large\"}");
                delete body;
                request->_tempObject = nullptr;
                return;
            }
            body->append(reinterpret_cast<const char*>(data), len);
            if (index + len != total) return;

            DynamicJsonDocument doc(256);
            const bool validJson = deserializeJson(doc, *body) == DeserializationError::Ok;
            const std::string password = validJson ? std::string(doc["password"] | "") : "";
            Config config;
            if (!validJson || password.size() < 8 || !ConfigStore::getInstance().load(config)) {
                request->send(400, "application/json", "{\"error\":\"admin_password_invalid\"}");
            } else {
                config.adminPasswordHash = sha256Hex(password);
                if (!ConfigStore::getInstance().save(config)) {
                    request->send(500, "application/json", "{\"error\":\"admin_password_save_failed\"}");
                } else {
                    DynamicJsonDocument response(256);
                    response["status"] = "ok";
                    response["token"] = issueAdminSession();
                    std::string json;
                    serializeJson(response, json);
                    request->send(200, "application/json", json.c_str());
                }
            }
            delete body;
            request->_tempObject = nullptr;
        });

    for (const char* route : {"/api/auth/setup", "/api/auth/login"}) {
        server_.on(route, HTTP_POST, [](AsyncWebServerRequest *request) {}, nullptr,
            [this, route](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
                auto *body = static_cast<std::string*>(request->_tempObject);
                if (index == 0) {
                    delete body;
                    body = new std::string();
                    body->reserve(std::min(total, static_cast<size_t>(512)));
                    request->_tempObject = body;
                }
                if (body == nullptr || total > 512) {
                    request->send(413, "application/json", "{\"error\":\"auth_payload_too_large\"}");
                    delete body;
                    request->_tempObject = nullptr;
                    return;
                }
                body->append(reinterpret_cast<const char*>(data), len);
                if (index + len != total) {
                    return;
                }

                DynamicJsonDocument doc(256);
                const bool validJson = deserializeJson(doc, *body) == DeserializationError::Ok;
                const std::string password = validJson ? std::string(doc["password"] | "") : "";
                Config config;
                const bool configLoaded = ConfigStore::getInstance().load(config);
                const bool isSetup = std::strcmp(route, "/api/auth/setup") == 0;
                if (!validJson || !configLoaded || password.size() < 8) {
                    request->send(400, "application/json", "{\"error\":\"admin_password_invalid\"}");
                } else if (isSetup && !config.adminPasswordHash.empty()) {
                    request->send(409, "application/json", "{\"error\":\"admin_already_configured\"}");
                } else if (!isSetup && (config.adminPasswordHash.empty()
                    || sha256Hex(password) != config.adminPasswordHash)) {
                    request->send(401, "application/json", "{\"error\":\"admin_login_failed\"}");
                } else {
                    if (isSetup) {
                        config.adminPasswordHash = sha256Hex(password);
                        if (!ConfigStore::getInstance().save(config)) {
                            request->send(500, "application/json", "{\"error\":\"admin_password_save_failed\"}");
                            delete body;
                            request->_tempObject = nullptr;
                            return;
                        }
                    }
                    DynamicJsonDocument response(256);
                    response["status"] = "ok";
                    response["token"] = issueAdminSession();
                    std::string json;
                    serializeJson(response, json);
                    request->send(200, "application/json", json.c_str());
                }
                delete body;
                request->_tempObject = nullptr;
            });
    }

        // API-Endpunkt: Laser-Version
        server_.on("/api/laser-version", HTTP_GET, [](AsyncWebServerRequest *request) {
            std::string version = SensorManager::getInstance().getLaserVersion();
            DynamicJsonDocument doc(64);
            doc["version"] = version;
            std::string json;
            serializeJson(doc, json);
            request->send(200, "application/json", json.c_str());
        });
    // API routes
    server_.on("/api/config", HTTP_GET, [](AsyncWebServerRequest *request) {
        Config config;
        if (ConfigStore::getInstance().load(config)) {
            DynamicJsonDocument doc(256);
            doc["behaelterhoehe"] = config.behaelterhoehe;
            doc["offset"] = config.offset;
            doc["sampleIntervalSeconds"] = config.sampleIntervalSeconds;
            doc["sensorType"] = config.sensorType;
            std::string json;
            serializeJson(doc, json);
            request->send(200, "application/json", json.c_str());
        } else {
            request->send(500, "application/json", "{\"error\":\"Config load failed\"}");
        }
    });

    server_.on("/api/config", HTTP_POST, [](AsyncWebServerRequest *request) {}, NULL,
        [this](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
        if (index == 0 && !requireAdmin(request)) return;
        if (index == 0 && total > kMaxConfigBodyBytes) {
            request->send(413, "application/json", "{\"error\":\"config_payload_too_large\"}");
            return;
        }
        auto *body = static_cast<std::string*>(request->_tempObject);
        if (index == 0) {
            delete body;
            body = new std::string();
            body->reserve(total);
            request->_tempObject = body;
        }
        if (body == nullptr) {
            // Defensive fallback; should not happen if index==0 was called.
            body = new std::string();
            request->_tempObject = body;
        }
        body->append(reinterpret_cast<const char*>(data), len);

        if (index + len == total) {
            DynamicJsonDocument doc(512);
            if (deserializeJson(doc, *body) != DeserializationError::Ok) {
                request->send(400, "application/json", "{\"error\":\"invalid_config_payload\"}");
                delete body;
                request->_tempObject = nullptr;
                return;
            }
            Config config;
            ConfigStore::getInstance().load(config); // Load current
            config.behaelterhoehe = doc["behaelterhoehe"] | 95.0;
            config.offset = doc["offset"] | 0.0;
            config.sampleIntervalSeconds = doc["sampleIntervalSeconds"] | 5UL;
            if (config.sampleIntervalSeconds < kMinSampleIntervalSeconds) {
                request->send(400, "application/json", "{\"error\":\"sample_interval_too_small\"}");
                delete body;
                request->_tempObject = nullptr;
                return;
            }
            const std::string newSensorType = doc["sensorType"] | config.sensorType.c_str();
            const bool sensorTypeChanged = newSensorType != config.sensorType;
            config.sensorType = (newSensorType == "vl53l1x") ? "vl53l1x" : "rcwl1670";
            if (!ConfigStore::getInstance().save(config)) {
                request->send(500, "application/json", "{\"error\":\"sensor_config_save_failed\"}");
                delete body;
                request->_tempObject = nullptr;
                return;
            }
            SensorManager::getInstance().setBehaelterhoehe(config.behaelterhoehe);
            SensorManager::getInstance().setOffset(config.offset);
            SensorManager::getInstance().setSampleIntervalSeconds(config.sampleIntervalSeconds);
            if (sensorTypeChanged) {
                SensorManager::getInstance().setSensorType(config.sensorType);
                SensorManager::getInstance().init();
            }
            request->send(200, "application/json", "{\"status\":\"ok\"}");
            delete body;
            request->_tempObject = nullptr;
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

        DynamicJsonDocument doc(512);
        doc["ssid"] = !configuredSsid.empty() ? configuredSsid : runtimeSsid;
        doc["password"] = wifiHasPassword ? kPasswordMask : "";
        doc["hasPassword"] = wifiHasPassword;
        doc["deviceName"] = loaded ? config.wifi.deviceName : WifiManager::getInstance().getConfig().deviceName;
        doc["ntpServerPrimary"] = loaded ? config.wifi.ntpServerPrimary : WifiManager::getInstance().getConfig().ntpServerPrimary;
        doc["ntpServerSecondary"] = loaded ? config.wifi.ntpServerSecondary : WifiManager::getInstance().getConfig().ntpServerSecondary;
        doc["dnsHostname"] = WifiManager::getInstance().getDnsHostname();
        doc["localUrl"] = WifiManager::getInstance().getLocalUrl();
        doc["useStaticIp"] = loaded && (!config.staticIp.ip.empty() || !config.staticIp.subnet.empty() || !config.staticIp.dns.empty());
        doc["staticIp"]["ip"] = loaded ? config.staticIp.ip : "";
        doc["staticIp"]["gateway"] = loaded ? config.staticIp.gateway : "";
        doc["staticIp"]["subnet"] = loaded ? config.staticIp.subnet : "";
        doc["staticIp"]["dns"] = loaded ? config.staticIp.dns : "";

        std::string json;
        serializeJson(doc, json);
        request->send(200, "application/json", json.c_str());
    });

    server_.on("/api/wifi/scan", HTTP_POST, [this](AsyncWebServerRequest *request) {
        if (!requireAdmin(request)) return;
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
        [this](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
        if (index == 0 && !requireAdmin(request)) return;
        if (index == 0 && total > kMaxWifiBodyBytes) {
            request->send(413, "application/json", "{\"error\":\"wifi_payload_too_large\"}");
            return;
        }
        auto *body = static_cast<std::string*>(request->_tempObject);
        if (index == 0) {
            delete body;
            body = new std::string();
            body->reserve(total);
            request->_tempObject = body;
        }
        if (body == nullptr) {
            // Defensive fallback; should not happen if index==0 was called.
            body = new std::string();
            request->_tempObject = body;
        }
        body->append(reinterpret_cast<const char*>(data), len);

        if (index + len == total) {
            DynamicJsonDocument doc(1024);
            DeserializationError jsonError = deserializeJson(doc, *body);
            if (jsonError) {
                request->send(400, "application/json", "{\"error\":\"invalid_wifi_payload\"}");
                delete body;
                request->_tempObject = nullptr;
                return;
            }
            Config config;
            ConfigStore::getInstance().load(config); // Load current
            config.wifi.ssid = doc["ssid"] | "";
            config.wifi.deviceName = doc["deviceName"] | "Level-Control";
            config.wifi.ntpServerPrimary = doc["ntpServerPrimary"] | "pool.ntp.org";
            config.wifi.ntpServerSecondary = doc["ntpServerSecondary"] | "time.cloudflare.com";
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
                    delete body;
                    request->_tempObject = nullptr;
                    return;
                }
            } else {
                config.staticIp = {};
            }

            if (!WifiManager::getInstance().setConfig(config.wifi, config.staticIp)) {
                request->send(500, "application/json", "{\"error\":\"wifi_config_save_failed\"}");
                delete body;
                request->_tempObject = nullptr;
                return;
            }
            request->send(200, "application/json", "{\"status\":\"ok\"}");
            delete body;
            request->_tempObject = nullptr;
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
        [this](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
        if (index == 0 && !requireAdmin(request)) return;
        if (index == 0 && total > kMaxMqttBodyBytes) {
            request->send(413, "application/json", "{\"error\":\"mqtt_payload_too_large\"}");
            return;
        }
        auto *body = static_cast<std::string*>(request->_tempObject);
        if (index == 0) {
            delete body;
            body = new std::string();
            body->reserve(total);
            request->_tempObject = body;
        }
        if (body == nullptr) {
            // Defensive fallback; should not happen if index==0 was called.
            body = new std::string();
            request->_tempObject = body;
        }
        body->append(reinterpret_cast<const char*>(data), len);

        if (index + len == total) {
            DynamicJsonDocument doc(768);
            DeserializationError jsonError = deserializeJson(doc, *body);
            if (jsonError) {
                request->send(400, "application/json", "{\"error\":\"invalid_mqtt_payload\"}");
                delete body;
                request->_tempObject = nullptr;
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
            if (!ConfigStore::getInstance().save(config)) {
                request->send(500, "application/json", "{\"error\":\"mqtt_config_save_failed\"}");
                delete body;
                request->_tempObject = nullptr;
                return;
            }

            // Re-init/connect MQTT with the new settings
            if (!config.mqtt.server.empty()) {
                MqttManager::getInstance().init(config.mqtt.server.c_str(), config.mqtt.port);
                MqttManager::getInstance().connect();
            }

            request->send(200, "application/json", "{\"status\":\"ok\"}");
            delete body;
            request->_tempObject = nullptr;
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

    server_.on("/api/mqtt/reconnect", HTTP_POST, [this](AsyncWebServerRequest *request) {
        if (!requireAdmin(request)) return;
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
            doc["encryption"] = derivePushEncryptionMode(config.push.useSsl, config.push.startTls);
            doc["security"] = derivePushEncryptionMode(config.push.useSsl, config.push.startTls);
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

    auto* pushPostHandler = new AsyncCallbackJsonWebHandler("/api/push", [this](AsyncWebServerRequest *request, JsonVariant &json) {
        if (!requireAdmin(request)) return;
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

        if (!config.push.useSsl) {
            config.push.useSsl = doc["ssl"] | false;
        }

        std::string encryptionMode;
        if (doc.containsKey("encryption")) {
            encryptionMode = doc["encryption"] | "";
        } else if (doc.containsKey("security")) {
            encryptionMode = doc["security"] | "";
        }
        if (!encryptionMode.empty()) {
            applyPushEncryptionModeToConfig(encryptionMode, config.push.useSsl, config.push.startTls);
        }

        if (config.push.useSsl && config.push.startTls) {
            config.push.startTls = false;
        }
        config.push.smtpSkipCertVerify = doc["smtpSkipCertVerify"] | false;
        config.push.authUser = doc["authUser"] | "";

        std::string newAuthPassword = doc["authPassword"] | "";
        if (!newAuthPassword.empty() && !isMaskedPassword(newAuthPassword)) {
            config.push.authPassword = newAuthPassword;
        }

        config.push.senderName = doc["senderName"] | "Level-Control";
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
            | "Level-Control Warnung: Stand hat {level_percent}% erreicht. Salz nachfüllen!";
        config.push.bodyTemplate = doc["bodyTemplate"]
            | "Der Füllstand hat {level_percent}% ({level_cm} cm) erreicht.\nBitte Salz nachfüllen!\nDein Level-Control";

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
            config.push.senderName = "Level-Control";
        }
        config.push.reminderCycle = normalizeReminderCycle(config.push.reminderCycle);
        config.push.reminderWeekday = normalizeReminderWeekday(config.push.reminderWeekday);
        if (config.push.subjectTemplate.empty()) {
            config.push.subjectTemplate = "Level-Control Warnung: Stand hat {level_percent}% erreicht. Salz nachfüllen!";
        }
        if (config.push.bodyTemplate.empty()) {
            config.push.bodyTemplate = "Der Füllstand hat {level_percent}% ({level_cm} cm) erreicht.\nBitte Salz nachfüllen!\nDein Level-Control";
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

    server_.on("/api/push/test", HTTP_POST, [this](AsyncWebServerRequest *request) {
        if (!requireAdmin(request)) return;
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

    server_.on("/api/push/smtp-check", HTTP_POST, [this](AsyncWebServerRequest *request) {
        if (!requireAdmin(request)) return;
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

    // GET /api/history – liefert alle gespeicherten Level-Control-Verläufe als JSON-Array
    server_.on("/api/history", HTTP_GET, [](AsyncWebServerRequest *request) {
        const auto entries = HistoryManager::getInstance().getHistory();
        AsyncResponseStream* resp = request->beginResponseStream("application/json");
        resp->print('[');
        bool first = true;
        for (const auto& e : entries) {
            if (!first) resp->print(',');
            first = false;
            char buf[40];
            snprintf(buf, sizeof(buf), "{\"ts\":%lu,\"v\":%.1f}",
                     static_cast<unsigned long>(e.timestamp), e.value);
            resp->print(buf);
        }
        resp->print(']');
        request->send(resp);
    });

    // DELETE /api/history – löscht alle gespeicherten Verlaufsdaten
    server_.on("/api/history", HTTP_DELETE, [this](AsyncWebServerRequest *request) {
        if (!requireAdmin(request)) return;
        HistoryManager::getInstance().clear();
        request->send(204);
    });

    // GET /api/export – Konfiguration und Verlaufsdaten als JSON-Backup herunterladen
    server_.on("/api/export", HTTP_GET, [this](AsyncWebServerRequest *request) {
        if (!requireAdmin(request)) return;
        Config config;
        if (!ConfigStore::getInstance().load(config)) {
            request->send(500, "application/json", "{\"error\":\"config_load_failed\"}");
            return;
        }

        const auto entries = HistoryManager::getInstance().getHistory();
        const time_t t = time(nullptr);
        struct tm* tmInfo = localtime(&t);
        char filename[64];
        strftime(filename, sizeof(filename), "level-control-backup-%Y-%m-%d.json", tmInfo);
        const bool includeSecrets = request->hasParam("includeSecrets")
            && request->getParam("includeSecrets")->value() == "1";

        DynamicJsonDocument cfgDoc(3072);
        cfgDoc["behaelterhoehe"] = config.behaelterhoehe;
        cfgDoc["offset"] = config.offset;
        cfgDoc["sampleIntervalSeconds"] = config.sampleIntervalSeconds;
        cfgDoc["sensorType"] = config.sensorType;
        JsonObject wifiJ = cfgDoc.createNestedObject("wifi");
        wifiJ["ssid"] = config.wifi.ssid;
        wifiJ["password"] = includeSecrets ? config.wifi.password : kPasswordMask;
        wifiJ["deviceName"] = config.wifi.deviceName;
        wifiJ["ntpServerPrimary"] = config.wifi.ntpServerPrimary;
        wifiJ["ntpServerSecondary"] = config.wifi.ntpServerSecondary;
        JsonObject sipJ = cfgDoc.createNestedObject("staticIp");
        sipJ["ip"] = config.staticIp.ip;
        sipJ["gateway"] = config.staticIp.gateway;
        sipJ["subnet"] = config.staticIp.subnet;
        sipJ["dns"] = config.staticIp.dns;
        JsonObject mqttJ = cfgDoc.createNestedObject("mqtt");
        mqttJ["server"] = config.mqtt.server;
        mqttJ["port"] = config.mqtt.port;
        mqttJ["user"] = config.mqtt.user;
        mqttJ["password"] = includeSecrets ? config.mqtt.password : kPasswordMask;
        mqttJ["discovery"] = config.mqtt.discovery;
        JsonObject pushJ = cfgDoc.createNestedObject("push");
        pushJ["enabled"] = config.push.enabled;
        pushJ["smtpServer"] = config.push.smtpServer;
        pushJ["smtpPort"] = config.push.smtpPort;
        pushJ["useSsl"] = config.push.useSsl;
        pushJ["startTls"] = config.push.startTls;
        pushJ["encryption"] = derivePushEncryptionMode(config.push.useSsl, config.push.startTls);
        pushJ["security"] = derivePushEncryptionMode(config.push.useSsl, config.push.startTls);
        pushJ["smtpSkipCertVerify"] = config.push.smtpSkipCertVerify;
        pushJ["authUser"] = config.push.authUser;
        pushJ["authPassword"] = includeSecrets ? config.push.authPassword : kPasswordMask;
        pushJ["senderName"] = config.push.senderName;
        pushJ["senderEmail"] = config.push.senderEmail;
        pushJ["recipientEmail"] = config.push.recipientEmail;
        pushJ["triggerPercent"] = config.push.triggerPercent;
        pushJ["sendHour"] = config.push.sendHour;
        pushJ["sendMinute"] = config.push.sendMinute;
        pushJ["reminderCycle"] = config.push.reminderCycle;
        pushJ["reminderWeekday"] = config.push.reminderWeekday;
        pushJ["subjectTemplate"] = config.push.subjectTemplate;
        pushJ["bodyTemplate"] = config.push.bodyTemplate;
        std::string cfgJson;
        serializeJson(cfgDoc, cfgJson);

        AsyncResponseStream* resp = request->beginResponseStream("application/json");
        resp->addHeader("Content-Disposition",
            String("attachment; filename=\"") + filename + "\"");
        resp->printf("{\"exportVersion\":1,\"exportedAt\":%lu,\"config\":",
            static_cast<unsigned long>(t));
        resp->print(cfgJson.c_str());
        resp->print(",\"history\":[");
        bool firstEntry = true;
        for (const auto& e : entries) {
            if (!firstEntry) resp->print(',');
            firstEntry = false;
            char buf[40];
            snprintf(buf, sizeof(buf), "{\"ts\":%lu,\"v\":%.1f}",
                static_cast<unsigned long>(e.timestamp), e.value);
            resp->print(buf);
        }
        resp->print("]}");
        request->send(resp);
    });

    // POST /api/import – Konfiguration und Verlaufsdaten aus JSON-Backup wiederherstellen
    server_.on("/api/import", HTTP_POST, [](AsyncWebServerRequest *request) {}, NULL,
        [this](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
        if (index == 0 && !requireAdmin(request)) return;
        if (index == 0 && total > kMaxImportBodyBytes) {
            request->send(413, "application/json", "{\"error\":\"import_payload_too_large\"}");
            return;
        }
        auto *body = static_cast<std::string*>(request->_tempObject);
        if (index == 0) {
            delete body;
            body = new std::string();
            body->reserve(total);
            request->_tempObject = body;
        }
        if (body == nullptr) {
            // Defensive fallback; should not happen if index==0 was called.
            body = new std::string();
            request->_tempObject = body;
        }

        body->append(reinterpret_cast<const char*>(data), len);
        if (index + len < total) return;

        DynamicJsonDocument doc(48 * 1024);
        if (deserializeJson(doc, *body) != DeserializationError::Ok) {
            request->send(400, "application/json", "{\"error\":\"invalid_json\"}");
            delete body;
            request->_tempObject = nullptr;
            return;
        }
        if ((doc["exportVersion"] | 0) != 1) {
            request->send(400, "application/json", "{\"error\":\"unsupported_export_version\"}");
            delete body;
            request->_tempObject = nullptr;
            return;
        }

        bool configRestored = false;
        bool historyRestored = false;

        if (doc.containsKey("config")) {
            Config config;
            ConfigStore::getInstance().load(config);
            const JsonObjectConst cfg = doc["config"];
            config.behaelterhoehe = cfg["behaelterhoehe"] | config.behaelterhoehe;
            config.offset = cfg["offset"] | config.offset;
            config.sampleIntervalSeconds = cfg["sampleIntervalSeconds"] | config.sampleIntervalSeconds;
            config.sensorType = cfg["sensorType"] | config.sensorType.c_str();

            if (cfg.containsKey("wifi")) {
                const JsonObjectConst w = cfg["wifi"];
                if (w.containsKey("ssid"))             config.wifi.ssid = w["ssid"].as<std::string>();
                if (w.containsKey("password") && !isMaskedPassword(w["password"] | std::string()))
                    config.wifi.password = w["password"].as<std::string>();
                if (w.containsKey("deviceName"))       config.wifi.deviceName = w["deviceName"].as<std::string>();
                if (w.containsKey("ntpServerPrimary")) config.wifi.ntpServerPrimary = w["ntpServerPrimary"].as<std::string>();
                if (w.containsKey("ntpServerSecondary")) config.wifi.ntpServerSecondary = w["ntpServerSecondary"].as<std::string>();
            }
            if (cfg.containsKey("staticIp")) {
                const JsonObjectConst sip = cfg["staticIp"];
                if (sip.containsKey("ip"))      config.staticIp.ip = sip["ip"].as<std::string>();
                if (sip.containsKey("gateway")) config.staticIp.gateway = sip["gateway"].as<std::string>();
                if (sip.containsKey("subnet"))  config.staticIp.subnet = sip["subnet"].as<std::string>();
                if (sip.containsKey("dns"))     config.staticIp.dns = sip["dns"].as<std::string>();
            }
            if (cfg.containsKey("mqtt")) {
                const JsonObjectConst m = cfg["mqtt"];
                if (m.containsKey("server"))    config.mqtt.server = m["server"].as<std::string>();
                if (m.containsKey("port"))      config.mqtt.port = m["port"] | config.mqtt.port;
                if (m.containsKey("user"))      config.mqtt.user = m["user"].as<std::string>();
                if (m.containsKey("password") && !isMaskedPassword(m["password"] | std::string()))
                    config.mqtt.password = m["password"].as<std::string>();
                if (m.containsKey("discovery")) config.mqtt.discovery = m["discovery"] | config.mqtt.discovery;
            }
            if (cfg.containsKey("push")) {
                const JsonObjectConst p = cfg["push"];
                if (p.containsKey("enabled"))            config.push.enabled = p["enabled"] | config.push.enabled;
                if (p.containsKey("smtpServer"))         config.push.smtpServer = p["smtpServer"].as<std::string>();
                if (p.containsKey("smtpPort"))           config.push.smtpPort = p["smtpPort"] | config.push.smtpPort;
                if (p.containsKey("useSsl"))             config.push.useSsl = p["useSsl"] | config.push.useSsl;
                if (p.containsKey("startTls"))           config.push.startTls = p["startTls"] | config.push.startTls;
                if (p.containsKey("smtpSkipCertVerify")) config.push.smtpSkipCertVerify = p["smtpSkipCertVerify"] | config.push.smtpSkipCertVerify;
                if (p.containsKey("authUser"))           config.push.authUser = p["authUser"].as<std::string>();
                if (p.containsKey("authPassword") && !isMaskedPassword(p["authPassword"] | std::string()))
                    config.push.authPassword = p["authPassword"].as<std::string>();
                if (p.containsKey("senderName"))         config.push.senderName = p["senderName"].as<std::string>();
                if (p.containsKey("senderEmail"))        config.push.senderEmail = p["senderEmail"].as<std::string>();
                if (p.containsKey("recipientEmail"))     config.push.recipientEmail = p["recipientEmail"].as<std::string>();
                if (p.containsKey("triggerPercent"))     config.push.triggerPercent = p["triggerPercent"] | config.push.triggerPercent;
                if (p.containsKey("sendHour"))           config.push.sendHour = p["sendHour"] | config.push.sendHour;
                if (p.containsKey("sendMinute"))         config.push.sendMinute = p["sendMinute"] | config.push.sendMinute;
                if (p.containsKey("reminderCycle"))      config.push.reminderCycle = p["reminderCycle"].as<std::string>();
                if (p.containsKey("reminderWeekday"))    config.push.reminderWeekday = p["reminderWeekday"] | config.push.reminderWeekday;
                if (p.containsKey("subjectTemplate"))    config.push.subjectTemplate = p["subjectTemplate"].as<std::string>();
                if (p.containsKey("bodyTemplate"))       config.push.bodyTemplate = p["bodyTemplate"].as<std::string>();
            }
            configRestored = ConfigStore::getInstance().save(config);
            if (configRestored) {
                SensorManager::getInstance().setBehaelterhoehe(config.behaelterhoehe);
                SensorManager::getInstance().setOffset(config.offset);
                SensorManager::getInstance().setSampleIntervalSeconds(config.sampleIntervalSeconds);
                SensorManager::getInstance().setSensorType(config.sensorType);
            }
        }

        if (doc["history"].is<JsonArrayConst>()) {
            const JsonArrayConst arr = doc["history"];
            std::vector<HistoryEntry> entries;
            entries.reserve(std::min(arr.size(), static_cast<size_t>(HistoryManager::kCapacity)));
            for (JsonVariantConst item : arr) {
                const uint32_t ts = item["ts"] | 0U;
                if (ts > 0) entries.push_back({ts, item["v"] | 0.0f});
            }
            HistoryManager::getInstance().restore(entries);
            historyRestored = true;
        }

        if (doc.containsKey("config") && !configRestored) {
            request->send(500, "application/json", "{\"error\":\"config_restore_failed\",\"configRestored\":false}");
            delete body;
            request->_tempObject = nullptr;
            return;
        }

        char respBuf[100];
        snprintf(respBuf, sizeof(respBuf),
            "{\"status\":\"ok\",\"configRestored\":%s,\"historyRestored\":%s}",
            configRestored ? "true" : "false",
            historyRestored ? "true" : "false");
        request->send(200, "application/json", respBuf);
        delete body;
        request->_tempObject = nullptr;
    });

    // POST /api/factory-reset – setzt Konfiguration und Historie auf Werkseinstellungen zurueck
    server_.on("/api/factory-reset", HTTP_POST, [this](AsyncWebServerRequest *request) {
        if (!requireAdmin(request)) return;
        Preferences configPrefs;
        if (configPrefs.begin("config", false)) {
            configPrefs.clear();
            configPrefs.end();
        }

        Config defaultConfig;
        if (!ConfigStore::getInstance().save(defaultConfig)) {
            request->send(500, "application/json", "{\"error\":\"factory_reset_failed\"}");
            return;
        }

        HistoryManager::getInstance().clear();

        // Gespeicherte WLAN-Credentials aus der WiFi-Stack-NVS loeschen.
        WiFi.disconnect(true, true);

        request->send(200, "application/json", "{\"status\":\"ok\",\"message\":\"factory_reset_started\"}");
        delay(kRestartDelayMs);
        ESP.restart();
    });

    server_.on("/api/restart", HTTP_POST, [this](AsyncWebServerRequest *request) {
        if (!requireAdmin(request)) return;
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

        AsyncWebServerResponse *response = request->beginResponse(LittleFS, "/index.html", "text/html");
        response->addHeader("Cache-Control", "no-store, no-cache, must-revalidate, max-age=0");
        response->addHeader("Pragma", "no-cache");
        response->addHeader("Expires", "0");
        request->send(response);
    });

    // Serve static files from LittleFS (nach API-Routen registrieren)
    // no-store: verhindert, dass der Browser JS/CSS nach einem OTA-Update aus dem Cache laed
    // und so die alte UI anzeigt obwohl neue Dateien geflasht wurden.
    server_.serveStatic("/", LittleFS, "/").setDefaultFile("index.html").setCacheControl("no-store");

    server_.onNotFound([this](AsyncWebServerRequest *request) {
        const String url = request->url();
        const bool isApiRoute = url.startsWith("/api/");
        const bool isWsRoute = url == "/ws";
        const int lastSlash = url.lastIndexOf('/');
        const int lastDot = url.lastIndexOf('.');
        const bool hasExtension = lastDot > lastSlash;

        // SPA-Fallback nur fuer URL-Pfade ohne Dateiendung (z. B. /dashboard).
        // Fuer Asset-Dateien (/assets/*.js, /favicon.svg, ...) bleibt 404 korrekt,
        // damit Browser kein HTML als Script/CSS bekommt.
        if (request->method() == HTTP_GET && !isApiRoute && !isWsRoute && !hasExtension) {
            if (!ensureLittleFsMounted()) {
                request->send(503, "application/json", "{\"error\":\"ui_not_available\"}");
                return;
            }

            AsyncWebServerResponse *response = request->beginResponse(LittleFS, "/index.html", "text/html");
            response->addHeader("Cache-Control", "no-store, no-cache, must-revalidate, max-age=0");
            response->addHeader("Pragma", "no-cache");
            response->addHeader("Expires", "0");
            request->send(response);
            return;
        }

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

    server_.on("/api/update/reset", HTTP_POST, [this](AsyncWebServerRequest *request) {
        if (!requireAdmin(request)) return;
        recoverStuckUploadIfNeeded();

        if (updateState_.inProgress && updateState_.source != "upload") {
            request->send(409, "application/json", "{\"error\":\"Aktives Repo-Update kann nicht manuell zurückgesetzt werden\"}");
            return;
        }

        if (updateState_.rebootPending) {
            request->send(409, "application/json", "{\"error\":\"Neustart steht bereits aus\"}");
            return;
        }

        clearUploadSession(true);
        resetUpdateState("", "");
        updateState_.message = "Update-Status wurde zurückgesetzt";
        touchUpdateActivity();
        DebugLogger::getInstance().log(LogLevel::INFO, "Update status reset requested via API");
        request->send(200, "application/json", "{\"status\":\"ok\",\"message\":\"Update-Status wurde zurückgesetzt\"}");
    });

    server_.on("/api/update/manifest", HTTP_GET, [this](AsyncWebServerRequest *request) {
        sendUpdateManifest(request);
    });

    for (const char* target : {"app", "webui"}) {
        const String route = String("/api/update/signature/") + target;
        server_.on(route.c_str(), HTTP_POST, [](AsyncWebServerRequest *request) {}, nullptr,
            [this, target](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
                if (index == 0 && !requireAdmin(request)) return;
                auto& signature = std::strcmp(target, "app") == 0
                    ? uploadAppSignature_
                    : uploadWebUiSignature_;
                if (index == 0) {
                    signature.clear();
                    signature.reserve(total);
                }
                signature.insert(signature.end(), data, data + len);
                if (index + len != total) {
                    return;
                }
                if (signature.empty() || signature.size() > kEmbeddedSigBytes) {
                    signature.clear();
                    request->send(400, "application/json", "{\"error\":\"Ungueltige Signaturdatei\"}");
                    return;
                }
                request->send(200, "application/json", "{\"status\":\"ok\"}");
            });
    }

    server_.on("/api/update/repo", HTTP_POST, [](AsyncWebServerRequest *request) {}, NULL,
        [this](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
        if (index == 0 && !requireAdmin(request)) return;
        if (index == 0 && total > kMaxUpdateRequestBytes) {
            request->send(413, "application/json", "{\"error\":\"update_request_too_large\"}");
            return;
        }
        auto *body = static_cast<std::string*>(request->_tempObject);
        if (index == 0) {
            delete body;
            body = new std::string();
            body->reserve(total);
            request->_tempObject = body;
        }
        if (body == nullptr) {
            // Defensive fallback; should not happen if index==0 was called.
            body = new std::string();
            request->_tempObject = body;
        }
        body->append(reinterpret_cast<const char*>(data), len);

        if (index + len != total) {
            return;
        }

        recoverStuckUploadIfNeeded();

        if (updateState_.inProgress || uploadActive_) {
            request->send(409, "application/json", "{\"error\":\"Update läuft bereits\"}");
            delete body;
            request->_tempObject = nullptr;
            return;
        }

        DynamicJsonDocument doc(256);
        if (deserializeJson(doc, *body) != DeserializationError::Ok) {
            request->send(400, "application/json", "{\"error\":\"Ungültiger Request-Body\"}");
            delete body;
            request->_tempObject = nullptr;
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
            delete body;
            request->_tempObject = nullptr;
            return;
        }

        request->send(202, "application/json", "{\"status\":\"started\"}");
        delete body;
        request->_tempObject = nullptr;
    });

    server_.on("/api/update/upload/app", HTTP_POST,
        [](AsyncWebServerRequest *request) {},
        [this](AsyncWebServerRequest *request, const String& filename, size_t index, uint8_t *data, size_t len, bool final) {
            if (index == 0 && !requireAdmin(request)) return;
            handleUpload(request, "app", filename, index, data, len, final);
        });

    // Backward-compatible alias
    server_.on("/api/update/upload/firmware", HTTP_POST,
        [](AsyncWebServerRequest *request) {},
        [this](AsyncWebServerRequest *request, const String& filename, size_t index, uint8_t *data, size_t len, bool final) {
            if (index == 0 && !requireAdmin(request)) return;
            handleUpload(request, "app", filename, index, data, len, final);
        });

    server_.on("/api/update/upload/webui", HTTP_POST,
        [](AsyncWebServerRequest *request) {},
        [this](AsyncWebServerRequest *request, const String& filename, size_t index, uint8_t *data, size_t len, bool final) {
            if (index == 0 && !requireAdmin(request)) return;
            handleUpload(request, "webui", filename, index, data, len, final);
        });
}

void WebServerDashboard::resetUpdateState(const std::string& source, const std::string& target) {
    updateState_ = {};
    updateState_.source = source;
    updateState_.target = target;
    updateState_.phase = "idle";
    restartScheduled_ = false;
    touchUpdateActivity();
}

void WebServerDashboard::touchUpdateActivity() {
    updateState_.lastActivityMs = millis();
}

void WebServerDashboard::setUpdatePhase(const std::string& phase, const std::string& message, size_t received, size_t total) {
    updateState_.phase = phase;
    updateState_.message = message;
    updateState_.received = received;
    updateState_.total = total;
    touchUpdateActivity();
}

void WebServerDashboard::clearUploadSession(bool abortUpdate) {
    const bool shouldRemountFs = uploadTarget_ == "webui";

    if (abortUpdate && (uploadActive_ || (updateState_.source == "upload" && updateState_.inProgress))) {
        Update.abort();
    }

    uploadActive_ = false;
    uploadFailed_ = false;
    uploadTarget_ = "";
    uploadFilename_ = "";
    uploadDetachedMode_ = false;
    uploadAppSignature_.clear();
    uploadWebUiSignature_.clear();
    uploadTailBuffer_.clear();
    uploadTargetPartition_ = nullptr;
    uploadLastChunkMs_ = 0;
    if (uploadShaActive_) {
        mbedtls_sha256_free(&uploadShaContext_);
        uploadShaActive_ = false;
    }

    if (shouldRemountFs) {
        ensureLittleFsMounted();
    }
}

void WebServerDashboard::recoverStuckUploadIfNeeded() {
    if (!uploadActive_ || updateState_.source != "upload" || !updateState_.inProgress) {
        return;
    }

    const unsigned long referenceMs = uploadLastChunkMs_ != 0 ? uploadLastChunkMs_ : updateState_.lastActivityMs;
    const unsigned long now = millis();
    if (referenceMs == 0 || (now - referenceMs) < kUploadStallTimeoutMs) {
        return;
    }

    DebugLogger::getInstance().log(
        LogLevel::ERROR,
        std::string("Upload stalled, resetting state after ") + std::to_string(now - referenceMs) + " ms");
    clearUploadSession(true);
    markUpdateFailed("Upload wurde wegen Inaktivität abgebrochen. Bitte erneut versuchen.");
}

void WebServerDashboard::markUpdateFailed(const std::string& message) {
    if (updateState_.source == "upload") {
        clearUploadSession(false);
    }
    updateState_.inProgress = false;
    updateState_.success = false;
    updateState_.rebootPending = false;
    restartScheduled_ = false;
    setUpdatePhase("failed", message, updateState_.received, updateState_.total);
    EventBus::getInstance().publish({ EventType::OTA_FAILED, message });
    DebugLogger::getInstance().log(LogLevel::ERROR, std::string("OTA failed: ") + message);
}

void WebServerDashboard::markUpdateSucceeded(const std::string& message) {
    if (updateState_.source == "upload") {
        clearUploadSession(false);
    }
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
    const BaseType_t created = xTaskCreate([](void *param) {
        auto *ctx = static_cast<RestartContext*>(param);
        vTaskDelay(pdMS_TO_TICKS(ctx->delayMs));
        delete ctx;
        ESP.restart();
    }, "level-control-restart", 4096, context, 1, nullptr);

    if (created != pdPASS) {
        delete context;
        restartScheduled_ = false;
        DebugLogger::getInstance().log(LogLevel::ERROR, "Restart-Task konnte nicht erstellt werden, starte sofort neu");
        ESP.restart();
    }
}

void WebServerDashboard::sendUpdateStatus(AsyncWebServerRequest *request) {
    recoverStuckUploadIfNeeded();

    DynamicJsonDocument doc(1280);
    doc["inProgress"] = updateState_.inProgress;
    doc["success"] = updateState_.success;
    doc["rebootPending"] = updateState_.rebootPending;
    doc["source"] = updateState_.source;
    doc["target"] = updateState_.target;
    doc["phase"] = updateState_.phase;
    doc["message"] = updateState_.message;
    doc["installedVersion"] = getInstalledVersion();
    doc["availableVersion"] = updateState_.availableVersion;
    doc["pendingVersion"] = (updateState_.success && updateState_.rebootPending
        && (updateState_.target == "app" || updateState_.target == "full"))
        ? updateState_.availableVersion
        : "";
    doc["manifestError"] = manifestError_;
    doc["received"] = updateState_.received;
    doc["total"] = updateState_.total;
    doc["uploadActive"] = uploadActive_;
    doc["activityAgeMs"] = updateState_.lastActivityMs == 0 ? 0UL : (millis() - updateState_.lastActivityMs);
    doc["uploadStallTimeoutMs"] = kUploadStallTimeoutMs;
    doc["canReset"] = !updateState_.rebootPending
        && updateState_.source == "upload"
        && (!updateState_.inProgress || updateState_.phase == "failed");
    doc["configFallbackApplied"] = ConfigStore::getInstance().wasFallbackApplied();
    doc["configFallbackMessage"] = ConfigStore::getInstance().getFallbackMessage();
    doc["appMaxSize"] = getAppPartitionSize();
    doc["firmwareMaxSize"] = getAppPartitionSize();
    doc["webuiMaxSize"] = getFilesystemPartitionSize();
    doc["manifestUrl"] = kLatestReleaseApiUrl;
    doc["releaseTagBaseUrl"] = kReleaseTagBaseUrl;

    const esp_partition_t* runningPartition = esp_ota_get_running_partition();
    const esp_partition_t* bootPartition = esp_ota_get_boot_partition();
    if (runningPartition) {
        doc["runningPartitionLabel"] = runningPartition->label;
        doc["runningPartitionAddress"] = runningPartition->address;
        doc["runningPartitionSize"] = runningPartition->size;
    } else {
        doc["runningPartitionLabel"] = "";
        doc["runningPartitionAddress"] = 0;
        doc["runningPartitionSize"] = 0;
    }

    if (bootPartition) {
        doc["configuredBootPartitionLabel"] = bootPartition->label;
        doc["configuredBootPartitionAddress"] = bootPartition->address;
        doc["configuredBootPartitionSize"] = bootPartition->size;
    } else {
        doc["configuredBootPartitionLabel"] = "";
        doc["configuredBootPartitionAddress"] = 0;
        doc["configuredBootPartitionSize"] = 0;
    }

    const esp_reset_reason_t resetReason = esp_reset_reason();
    doc["lastResetReason"] = resetReasonToString(resetReason);
    doc["lastResetReasonCode"] = static_cast<int>(resetReason);

    std::string json;
    serializeJson(doc, json);
    request->send(200, "application/json", json.c_str());
}

void WebServerDashboard::sendUpdateManifest(AsyncWebServerRequest *request) {
    // Non-blocking: Liefert Cache-Daten sofort und startet bei Bedarf einen Background-Fetch.
    const unsigned long now = millis();
    const bool cacheExpired = lastManifestCheckMs_ == 0 || (now - lastManifestCheckMs_) >= kManifestCacheTtlMs;

    if (cacheExpired && !manifestFetchInProgress_) {
        startBackgroundManifestFetch();
    }

    if (!cachedManifest_.valid && !manifestError_.empty()) {
        DynamicJsonDocument doc(256);
        doc["error"] = manifestError_;
        doc["manifestUrl"] = kLatestReleaseApiUrl;
        doc["fetching"] = manifestFetchInProgress_;
        std::string json;
        serializeJson(doc, json);
        request->send(502, "application/json", json.c_str());
        return;
    }

    if (!cachedManifest_.valid) {
        DynamicJsonDocument doc(256);
        doc["error"] = manifestFetchInProgress_ ? "Release-Informationen werden geladen" : "Kein Release-Manifest verfügbar";
        doc["manifestUrl"] = kLatestReleaseApiUrl;
        doc["fetching"] = manifestFetchInProgress_;
        std::string json;
        serializeJson(doc, json);
        request->send(manifestFetchInProgress_ ? 202 : 502, "application/json", json.c_str());
        return;
    }

    DynamicJsonDocument doc(1024);
    doc["fetching"] = manifestFetchInProgress_;
    doc["version"] = cachedManifest_.version;
    doc["releaseUrl"] = cachedManifest_.releaseUrl;

    JsonObject assets = doc.createNestedObject("assets");
    if (!cachedManifest_.app.url.empty()) {
        JsonObject app = assets.createNestedObject("app");
        app["name"] = cachedManifest_.app.name;
        app["url"] = cachedManifest_.app.url;
        app["size"] = cachedManifest_.app.size;
    }
    if (!cachedManifest_.webui.url.empty()) {
        JsonObject webui = assets.createNestedObject("webui");
        webui["name"] = cachedManifest_.webui.name;
        webui["url"] = cachedManifest_.webui.url;
        webui["size"] = cachedManifest_.webui.size;
    }

    std::string json;
    serializeJson(doc, json);
    request->send(200, "application/json", json.c_str());
}

bool WebServerDashboard::fetchLatestManifest(ReleaseManifest& manifest, std::string& rawManifest, std::string& error) {
    if (!isHttpsUrl(kLatestReleaseApiUrl)) {
        error = "Release-API-URL muss HTTPS verwenden";
        return false;
    }

    WiFiClientSecure client;
    client.setCACert(kOtaTrustedRootCAs);

    HTTPClient http;
    http.setTimeout(15000);
    http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
    if (!http.begin(client, kLatestReleaseApiUrl)) {
        error = "Release-Information konnte nicht geladen werden";
        return false;
    }

    http.addHeader("Accept", "application/json");
    http.addHeader("User-Agent", kUpdateUserAgent);

    const int statusCode = http.GET();
    if (statusCode != HTTP_CODE_OK) {
        error = "Release-Request fehlgeschlagen (HTTP " + std::to_string(statusCode) + ")";
        http.end();
        return false;
    }

    rawManifest = http.getString().c_str();
    http.end();

    return parseLatestReleaseInfo(rawManifest, manifest, error);
}

bool WebServerDashboard::parseLatestReleaseInfo(const std::string& rawManifest, ReleaseManifest& manifest, std::string& error) const {
    manifest = {};
    DynamicJsonDocument doc(16384);
    if (deserializeJson(doc, rawManifest) != DeserializationError::Ok) {
        error = "Release-Antwort ist kein gültiges JSON";
        return false;
    }

    manifest.version = normalizeVersionTag(doc["tag_name"] | "");
    manifest.releaseUrl = doc["html_url"] | kLatestReleaseUrl;

    ManifestAsset detachedApp;
    ManifestAsset detachedWebUi;
    ManifestAsset legacyApp;
    ManifestAsset legacyWebUi;
    const auto hasSuffix = [](const std::string& value, const char* suffix) {
        const size_t suffixLength = strlen(suffix);
        return value.size() >= suffixLength
            && value.compare(value.size() - suffixLength, suffixLength, suffix) == 0;
    };

    JsonArray assets = doc["assets"].as<JsonArray>();
    for (JsonObject asset : assets) {
        const std::string name = asset["name"] | "";
        const std::string lowerName = toLowerCopy(name);
        const std::string url = asset["browser_download_url"] | "";
        const size_t size = asset["size"] | 0;

        if (hasSuffix(lowerName, "-image.bin")) {
            detachedApp.name = name;
            detachedApp.url = url;
            detachedApp.size = size;
        } else if (hasSuffix(lowerName, "-image.sig")) {
            detachedApp.signatureUrl = url;
        } else if (hasSuffix(lowerName, "-filesystem.bin")) {
            detachedWebUi.name = name;
            detachedWebUi.url = url;
            detachedWebUi.size = size;
        } else if (hasSuffix(lowerName, "-filesystem.sig")) {
            detachedWebUi.signatureUrl = url;
        } else if (isAppAssetName(lowerName)) {
            legacyApp.name = name;
            legacyApp.url = url;
            legacyApp.size = size;
        } else if (isWebUiAssetName(lowerName)) {
            legacyWebUi.name = name;
            legacyWebUi.url = url;
            legacyWebUi.size = size;
        }
    }

    manifest.app = !detachedApp.url.empty() && !detachedApp.signatureUrl.empty()
        ? detachedApp
        : legacyApp;
    manifest.webui = !detachedWebUi.url.empty() && !detachedWebUi.signatureUrl.empty()
        ? detachedWebUi
        : legacyWebUi;

    manifest.valid = !manifest.version.empty() && (!manifest.app.url.empty() || !manifest.webui.url.empty());

    if (!manifest.valid) {
        error = "Release enthält keine unterstützten OTA-Assets";
        return false;
    }

    return true;
}
bool WebServerDashboard::verifyDetachedFileSignature(const unsigned char* hash, size_t hashLen, const uint8_t* signature, size_t signatureLen, std::string& error) const {
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
        hashLen,
        signature,
        signatureLen);
    mbedtls_pk_free(&publicKey);

    if (verifyResult != 0) {
        error = "Dateisignatur ungültig";
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
        // Keep the last valid manifest to survive transient network/release endpoint issues.
        if (cachedManifest_.valid) {
            manifestError_ = error;
            lastManifestCheckMs_ = now;
            return true;
        }

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

void WebServerDashboard::startBackgroundManifestFetch() {
    if (manifestFetchInProgress_) {
        return;
    }
    manifestFetchInProgress_ = true;

    if (xTaskCreate([](void *param) {
            auto *dashboard = static_cast<WebServerDashboard*>(param);
            dashboard->runManifestFetchTask();
            vTaskDelete(nullptr);
        }, "manifest-fetch", 8192, this, 1, nullptr) != pdPASS) {
        manifestFetchInProgress_ = false;
        manifestError_ = "Manifest-Task konnte nicht gestartet werden";
        DebugLogger::getInstance().log(LogLevel::ERROR, manifestError_);
    }
}

void WebServerDashboard::runManifestFetchTask() {
    ReleaseManifest manifest;
    std::string rawManifest;
    std::string error;

    if (fetchLatestManifest(manifest, rawManifest, error)) {
        cachedManifest_ = manifest;
        manifestError_.clear();
    } else {
        if (!cachedManifest_.valid) {
            cachedManifest_ = {};
        }
        manifestError_ = error;
    }

    lastManifestCheckMs_ = millis();
    manifestFetchInProgress_ = false;
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
        }, "level-control-ota", 16384, context, 1, nullptr) != pdPASS) {
        delete context;
        markUpdateFailed("OTA-Task konnte nicht gestartet werden");
    }
}

void WebServerDashboard::runRemoteUpdateTask(const std::string& target) {
    ReleaseManifest manifest;
    std::string rawManifest;
    std::string error;

    setUpdatePhase("release", "Release-Informationen werden geladen");
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
        if (!applyRemoteAsset(manifest.webui, U_SPIFFS, "webui", manifest.version, error)) {
            markUpdateFailed(error);
            return;
        }
    }

    if ((target == "app" || target == "full") && !manifest.app.url.empty()) {
        if (!applyRemoteAsset(manifest.app, U_FLASH, "app", manifest.version, error)) {
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

bool WebServerDashboard::applyDetachedRemoteAsset(const ManifestAsset& asset, int command, const std::string& phase, const std::string& version, std::string& error) {
    if (!isHttpsUrl(asset.url) || !isHttpsUrl(asset.signatureUrl)) {
        error = "Asset- und Signatur-URL müssen HTTPS verwenden";
        return false;
    }

    WiFiClientSecure signatureClient;
    signatureClient.setCACert(kOtaTrustedRootCAs);
    HTTPClient signatureHttp;
    signatureHttp.setTimeout(15000);
    signatureHttp.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
    if (!signatureHttp.begin(signatureClient, asset.signatureUrl.c_str())) {
        error = "Signatur konnte nicht geöffnet werden";
        return false;
    }
    signatureHttp.addHeader("User-Agent", kUpdateUserAgent);
    if (signatureHttp.GET() != HTTP_CODE_OK) {
        error = "Signatur-Download fehlgeschlagen";
        signatureHttp.end();
        return false;
    }
    const String signatureBody = signatureHttp.getString();
    signatureHttp.end();
    const size_t signatureLength = signatureBody.length();
    if (signatureLength == 0 || signatureLength > kEmbeddedSigBytes) {
        error = "Signatur hat eine ungültige Länge";
        return false;
    }
    std::vector<uint8_t> signature(
        reinterpret_cast<const uint8_t*>(signatureBody.c_str()),
        reinterpret_cast<const uint8_t*>(signatureBody.c_str()) + signatureLength);

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
    if (http.GET() != HTTP_CODE_OK) {
        error = "Asset-Download fehlgeschlagen";
        http.end();
        return false;
    }

    const int contentLength = http.getSize();
    if (contentLength <= 0) {
        error = "Asset liefert keine gültige Dateigröße";
        http.end();
        return false;
    }
    const size_t expectedSize = static_cast<size_t>(contentLength);
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

    mbedtls_sha256_context shaContext;
    mbedtls_sha256_init(&shaContext);
    if (mbedtls_sha256_starts_ret(&shaContext, 0) != 0) {
        error = "SHA256-Kontext konnte nicht initialisiert werden";
        Update.abort();
        if (command == U_SPIFFS) {
            LittleFS.begin();
        }
        http.end();
        return false;
    }

    setUpdatePhase(phase, "Download läuft", 0, expectedSize);
    WiFiClient *stream = http.getStreamPtr();
    uint8_t buffer[kUploadBufferSize];
    size_t received = 0;
    while (received < expectedSize) {
        const size_t available = stream->available();
        if (available == 0) {
            if (!http.connected()) {
                break;
            }
            delay(1);
            continue;
        }

        const size_t chunkSize = stream->readBytes(buffer, std::min<size_t>(available, sizeof(buffer)));
        if (chunkSize == 0) {
            continue;
        }
        if (received + chunkSize > expectedSize
            || mbedtls_sha256_update_ret(&shaContext, buffer, chunkSize) != 0
            || Update.write(buffer, chunkSize) != chunkSize) {
            error = Update.errorString();
            Update.abort();
            mbedtls_sha256_free(&shaContext);
            if (command == U_SPIFFS) {
                LittleFS.begin();
            }
            http.end();
            return false;
        }
        received += chunkSize;
        setUpdatePhase(phase, "Download läuft", received, expectedSize);
    }

    if (received != expectedSize) {
        error = "Asset-Download wurde vorzeitig beendet";
        Update.abort();
        mbedtls_sha256_free(&shaContext);
        if (command == U_SPIFFS) {
            LittleFS.begin();
        }
        http.end();
        return false;
    }

    unsigned char shaResult[32];
    if (mbedtls_sha256_finish_ret(&shaContext, shaResult) != 0
        || !verifyDetachedFileSignature(shaResult, sizeof(shaResult), signature.data(), signature.size(), error)) {
        if (error.empty()) {
            error = "Signaturprüfung fehlgeschlagen";
        }
        Update.abort();
        mbedtls_sha256_free(&shaContext);
        if (command == U_SPIFFS) {
            LittleFS.begin();
        }
        http.end();
        return false;
    }
    mbedtls_sha256_free(&shaContext);
    setUpdatePhase(phase, "Signatur geprüft", received, expectedSize);

    if (!Update.end(true)) {
        error = Update.errorString();
        if (command == U_SPIFFS) {
            LittleFS.begin();
        }
        http.end();
        return false;
    }

    if (command == U_FLASH) {
        const esp_partition_t* targetPartition = esp_ota_get_next_update_partition(nullptr);
        if (!ensureConfiguredBootPartition(targetPartition, error)) {
            http.end();
            return false;
        }
    }
    if (command == U_SPIFFS) {
        ensureLittleFsMounted();
    }
    http.end();
    return true;
}

bool WebServerDashboard::applyRemoteAsset(const ManifestAsset& asset, int command, const std::string& phase, const std::string& version, std::string& error) {
    if (!asset.signatureUrl.empty()) {
        return applyDetachedRemoteAsset(asset, command, phase, version, error);
    }

    if (!isHttpsUrl(asset.url)) {
        error = "Asset-URL muss HTTPS verwenden";
        return false;
    }

    DebugLogger::getInstance().log(
        LogLevel::INFO,
        std::string("OTA asset start: phase=") + phase + ", version=" + version + ", url=" + asset.url);

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
    if (contentLength > 0 && static_cast<size_t>(contentLength) <= kEmbeddedSigTrailerSize) {
        error = "Asset ist zu klein oder enthält keinen Signatur-Trailer";
        http.end();
        return false;
    }
    const size_t expectedSize = contentLength > 0
        ? static_cast<size_t>(contentLength) - kEmbeddedSigTrailerSize
        : UPDATE_SIZE_UNKNOWN;

    if (command == U_SPIFFS) {
        LittleFS.end();
    }

    const esp_partition_t* targetPartition = nullptr;
    if (command == U_FLASH) {
        targetPartition = esp_ota_get_next_update_partition(nullptr);
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
    size_t streamReceived = 0;
    bool firstPayloadChunk = true;
    std::vector<uint8_t> tailBuffer;
    tailBuffer.reserve(kEmbeddedSigTrailerSize + kUploadBufferSize);

    mbedtls_sha256_context shaContext;
    mbedtls_sha256_init(&shaContext);
    if (mbedtls_sha256_starts_ret(&shaContext, 0) != 0) {
        error = "SHA256-Kontext konnte nicht initialisiert werden";
        if (command == U_SPIFFS) {
            LittleFS.begin();
        }
        http.end();
        return false;
    }

    setUpdatePhase(phase, "Download läuft", 0, expectedSize);

    while (http.connected() && (contentLength < 0 || streamReceived < static_cast<size_t>(contentLength))) {
        const size_t available = stream->available();
        if (available == 0) {
            delay(1);
            continue;
        }

        const size_t chunkSize = stream->readBytes(buffer, std::min<size_t>(available, sizeof(buffer)));
        if (chunkSize == 0) {
            continue;
        }

        streamReceived += chunkSize;
        tailBuffer.insert(tailBuffer.end(), buffer, buffer + chunkSize);

        while (tailBuffer.size() > kEmbeddedSigTrailerSize) {
            const size_t processLen = tailBuffer.size() - kEmbeddedSigTrailerSize;
            std::vector<uint8_t> payloadChunk(tailBuffer.begin(), tailBuffer.begin() + processLen);
            tailBuffer.erase(tailBuffer.begin(), tailBuffer.begin() + processLen);

            if (firstPayloadChunk && !payloadChunk.empty()) {
                if ((command == U_FLASH && payloadChunk[0] != 0xE9) || (command == U_SPIFFS && payloadChunk[0] == 0xE9)) {
                    Update.abort();
                    if (command == U_SPIFFS) {
                        LittleFS.begin();
                    }
                    http.end();
                    error = command == U_FLASH ? "App-Asset ist kein gültiges ESP32-Image" : "Web-UI-Asset sieht wie eine App aus";
                    mbedtls_sha256_free(&shaContext);
                    return false;
                }
                firstPayloadChunk = false;
            }

            if (mbedtls_sha256_update_ret(&shaContext, payloadChunk.data(), payloadChunk.size()) != 0) {
                Update.abort();
                if (command == U_SPIFFS) {
                    LittleFS.begin();
                }
                http.end();
                error = "SHA256-Berechnung fehlgeschlagen";
                mbedtls_sha256_free(&shaContext);
                return false;
            }
            if (Update.write(payloadChunk.data(), payloadChunk.size()) != payloadChunk.size()) {
                Update.abort();
                if (command == U_SPIFFS) {
                    LittleFS.begin();
                }
                http.end();
                error = Update.errorString();
                mbedtls_sha256_free(&shaContext);
                return false;
            }

            received += payloadChunk.size();
        }

        setUpdatePhase(phase, "Download läuft", received, expectedSize);
    }

    if (tailBuffer.size() != kEmbeddedSigTrailerSize) {
        Update.abort();
        if (command == U_SPIFFS) {
            LittleFS.begin();
        }
        http.end();
        mbedtls_sha256_free(&shaContext);
        error = "Signatur-Trailer fehlt oder ist unvollständig";
        return false;
    }

    if ((command == U_FLASH && received < 65536) || (command == U_SPIFFS && received < 4096)) {
        Update.abort();
        if (command == U_SPIFFS) {
            LittleFS.begin();
        }
        http.end();
        mbedtls_sha256_free(&shaContext);
        error = command == U_FLASH ? "App-Asset ist unplausibel klein" : "Web-UI-Asset ist unplausibel klein";
        return false;
    }

    if (memcmp(tailBuffer.data(), kEmbeddedSigMagic, sizeof(kEmbeddedSigMagic)) != 0) {
        Update.abort();
        if (command == U_SPIFFS) {
            LittleFS.begin();
        }
        http.end();
        mbedtls_sha256_free(&shaContext);
        error = "Asset-Signatur-Trailer ist ungültig";
        return false;
    }
    const size_t sigLen = tailBuffer[sizeof(kEmbeddedSigMagic)];
    if (sigLen == 0 || sigLen > kEmbeddedSigBytes) {
        Update.abort();
        if (command == U_SPIFFS) {
            LittleFS.begin();
        }
        http.end();
        mbedtls_sha256_free(&shaContext);
        error = "Asset-Signaturlänge ist ungültig";
        return false;
    }

    unsigned char shaResult[32];
    if (mbedtls_sha256_finish_ret(&shaContext, shaResult) != 0) {
        Update.abort();
        if (command == U_SPIFFS) {
            LittleFS.begin();
        }
        http.end();
        mbedtls_sha256_free(&shaContext);
        error = "SHA256-Abschluss fehlgeschlagen";
        return false;
    }
    mbedtls_sha256_free(&shaContext);

    if (!verifyDetachedFileSignature(
            shaResult,
            sizeof(shaResult),
            tailBuffer.data() + sizeof(kEmbeddedSigMagic) + 1,
            sigLen,
            error)) {
        Update.abort();
        if (command == U_SPIFFS) {
            LittleFS.begin();
        }
        http.end();
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

    if (command == U_FLASH) {
        // Nach erfolgreichem Flashen die neue Partition als Boot-Partition setzen und verifizieren.
        if (!ensureConfiguredBootPartition(targetPartition, error)) {
            http.end();
            return false;
        }
    }

    if (command == U_SPIFFS) {
        ensureLittleFsMounted();
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
    recoverStuckUploadIfNeeded();

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
        const std::string lowerFilename = toLowerCopy(filename.c_str());
        uploadDetachedMode_ = lowerFilename.find("-image.bin") != std::string::npos
            || lowerFilename.find("-filesystem.bin") != std::string::npos;
        const auto& detachedSignature = target == "app" ? uploadAppSignature_ : uploadWebUiSignature_;
        if (uploadDetachedMode_ && detachedSignature.empty()) {
            request->send(400, "application/json", "{\"error\":\"Fuer dieses Detached-Asset zuerst die .sig-Datei waehlen\"}");
            uploadActive_ = false;
            return;
        }
        uploadTailBuffer_.clear();
        uploadTailBuffer_.reserve(kEmbeddedSigTrailerSize + kUploadBufferSize);
        uploadLastChunkMs_ = millis();
        resetUpdateState("upload", target.c_str());
        updateState_.inProgress = true;
        setUpdatePhase("validating", "Upload wird geprüft", 0, 0);
        EventBus::getInstance().publish({ EventType::OTA_STARTED, std::string(target.c_str()) });
        DebugLogger::getInstance().log(
            LogLevel::INFO,
            std::string("Manual upload started: target=") + target.c_str()
                + ", file=" + filename.c_str()
                + ", size=" + std::to_string(request->contentLength()));

        if (target == "webui") {
            LittleFS.end();
        }

        mbedtls_sha256_init(&uploadShaContext_);
        if (mbedtls_sha256_starts_ret(&uploadShaContext_, 0) != 0) {
            uploadFailed_ = true;
            mbedtls_sha256_free(&uploadShaContext_);
            markUpdateFailed("SHA256-Kontext konnte nicht initialisiert werden");
            request->send(500, "application/json", (std::string("{\"error\":\"") + updateState_.message + "\"}").c_str());
            return;
        }
        uploadShaActive_ = true;

        // Für App-Uploads die Ziel-OTA-Partition bereits vor Beginn ermitteln
        uploadTargetPartition_ = nullptr;
        if (target == "app") {
            uploadTargetPartition_ = esp_ota_get_next_update_partition(nullptr);
        }

        const size_t partitionLimit = target == "app" ? getAppPartitionSize() : getFilesystemPartitionSize();
        const size_t beginSize = partitionLimit > 0 ? partitionLimit : UPDATE_SIZE_UNKNOWN;
        if (!Update.begin(beginSize, target == "app" ? U_FLASH : U_SPIFFS)) {
            uploadFailed_ = true;
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
        uploadTargetPartition_ = nullptr;
        if (target == "webui") {
            LittleFS.begin();
        }
        markUpdateFailed(error);
    }

    if (!uploadFailed_ && len > 0) {
        uploadLastChunkMs_ = millis();
        const size_t limit = target == "app" ? getAppPartitionSize() : getFilesystemPartitionSize();
        if (uploadDetachedMode_) {
            const size_t nextPayloadSize = updateState_.received + len;
            if ((limit > 0 && nextPayloadSize > limit)
                || (uploadShaActive_ && mbedtls_sha256_update_ret(&uploadShaContext_, data, len) != 0)
                || Update.write(data, len) != len) {
                uploadFailed_ = true;
                Update.abort();
                uploadTargetPartition_ = nullptr;
                if (target == "webui") {
                    LittleFS.begin();
                }
                markUpdateFailed(Update.errorString());
            } else {
                setUpdatePhase("uploading", "Upload läuft", nextPayloadSize, 0);
            }
        } else {
        uploadTailBuffer_.insert(uploadTailBuffer_.end(), data, data + len);
        while (!uploadFailed_ && uploadTailBuffer_.size() > kEmbeddedSigTrailerSize) {
            const size_t processLen = uploadTailBuffer_.size() - kEmbeddedSigTrailerSize;
            std::vector<uint8_t> payloadChunk(uploadTailBuffer_.begin(), uploadTailBuffer_.begin() + processLen);
            uploadTailBuffer_.erase(uploadTailBuffer_.begin(), uploadTailBuffer_.begin() + processLen);

            const size_t nextPayloadSize = updateState_.received + payloadChunk.size();
            if (limit > 0 && nextPayloadSize > limit) {
                uploadFailed_ = true;
                Update.abort();
                uploadTargetPartition_ = nullptr;
                if (target == "webui") {
                    LittleFS.begin();
                }
                markUpdateFailed(target == "app"
                    ? "App-Datei überschreitet die verfügbare App-Partition"
                    : "Web-UI-Datei überschreitet die LittleFS-Partition");
                break;
            }

            if (uploadShaActive_ && mbedtls_sha256_update_ret(&uploadShaContext_, payloadChunk.data(), payloadChunk.size()) != 0) {
                uploadFailed_ = true;
                Update.abort();
                uploadTargetPartition_ = nullptr;
                if (target == "webui") {
                    LittleFS.begin();
                }
                markUpdateFailed("SHA256-Berechnung fehlgeschlagen");
                break;
            }

            if (Update.write(payloadChunk.data(), payloadChunk.size()) != payloadChunk.size()) {
                uploadFailed_ = true;
                Update.abort();
                uploadTargetPartition_ = nullptr;
                if (target == "webui") {
                    LittleFS.begin();
                }
                markUpdateFailed(Update.errorString());
                break;
            }

            setUpdatePhase("uploading", "Upload läuft", nextPayloadSize, 0);
        }
        }
    }

    if (!final) {
        return;
    }

    if (uploadFailed_) {
        request->send(400, "application/json", (std::string("{\"error\":\"") + updateState_.message + "\"}").c_str());
        return;
    }

    const size_t payloadSize = updateState_.received;
    if ((target == "app" && payloadSize < 65536) || (target == "webui" && payloadSize < 4096)) {
        Update.abort();
        uploadTargetPartition_ = nullptr;
        if (target == "webui") {
            LittleFS.begin();
        }
        markUpdateFailed(target == "app"
            ? "App-Datei ist für ein ESP32-Image unplausibel klein"
            : "Web-UI-Datei ist unplausibel klein");
        request->send(400, "application/json", (std::string("{\"error\":\"") + updateState_.message + "\"}").c_str());
        return;
    }

    if (!uploadDetachedMode_ && (uploadTailBuffer_.size() != kEmbeddedSigTrailerSize
        || memcmp(uploadTailBuffer_.data(), kEmbeddedSigMagic, sizeof(kEmbeddedSigMagic)) != 0)) {
        Update.abort();
        uploadTargetPartition_ = nullptr;
        if (target == "webui") {
            LittleFS.begin();
        }
        markUpdateFailed("Signatur-Trailer fehlt oder ist ungültig");
        request->send(400, "application/json", (std::string("{\"error\":\"") + updateState_.message + "\"}").c_str());
        return;
    }
    const auto& detachedSignature = target == "app" ? uploadAppSignature_ : uploadWebUiSignature_;
    const size_t sigLen = uploadDetachedMode_
        ? detachedSignature.size()
        : uploadTailBuffer_[sizeof(kEmbeddedSigMagic)];
    if (sigLen == 0 || sigLen > kEmbeddedSigBytes) {
        Update.abort();
        uploadTargetPartition_ = nullptr;
        if (target == "webui") {
            LittleFS.begin();
        }
        markUpdateFailed("Signaturlänge im Trailer ist ungültig");
        request->send(400, "application/json", (std::string("{\"error\":\"") + updateState_.message + "\"}").c_str());
        return;
    }

    setUpdatePhase("verifying", "Signatur wird geprüft", payloadSize, 0);
    unsigned char uploadSha[32];
    if (!uploadShaActive_ || mbedtls_sha256_finish_ret(&uploadShaContext_, uploadSha) != 0) {
        if (uploadShaActive_) {
            mbedtls_sha256_free(&uploadShaContext_);
            uploadShaActive_ = false;
        }
        Update.abort();
        uploadTargetPartition_ = nullptr;
        if (target == "webui") {
            LittleFS.begin();
        }
        markUpdateFailed("Signaturprüfung fehlgeschlagen");
        request->send(400, "application/json", (std::string("{\"error\":\"") + updateState_.message + "\"}").c_str());
        return;
    }
    mbedtls_sha256_free(&uploadShaContext_);
    uploadShaActive_ = false;

    std::string signatureError;
    if (!verifyDetachedFileSignature(
            uploadSha,
            sizeof(uploadSha),
            uploadDetachedMode_
                ? detachedSignature.data()
                : uploadTailBuffer_.data() + sizeof(kEmbeddedSigMagic) + 1,
            sigLen,
            signatureError)) {
        Update.abort();
        uploadTargetPartition_ = nullptr;
        if (target == "webui") {
            LittleFS.begin();
        }
        markUpdateFailed(std::string("Signaturprüfung fehlgeschlagen: ") + signatureError);
        request->send(400, "application/json", (std::string("{\"error\":\"") + updateState_.message + "\"}").c_str());
        return;
    }

    if (!Update.end(true)) {
        uploadTargetPartition_ = nullptr;
        if (target == "webui") {
            LittleFS.begin();
        }
        markUpdateFailed(Update.errorString());
        request->send(500, "application/json", (std::string("{\"error\":\"") + Update.errorString() + "\"}").c_str());
        return;
    }

    if (target == "app") {
        // Falls wir vor dem Upload die Ziel-Partition ermittelt haben, diese jetzt als Boot-Partition setzen.
        std::string bootPartitionError;
        if (!ensureConfiguredBootPartition(uploadTargetPartition_, bootPartitionError)) {
            uploadTargetPartition_ = nullptr;
            if (target == "webui") {
                LittleFS.begin();
            }
            markUpdateFailed(bootPartitionError);
            request->send(500, "application/json", (std::string("{\"error\":\"") + updateState_.message + "\"}").c_str());
            return;
        }
        uploadTargetPartition_ = nullptr;
    }

    if (target == "webui") {
        ensureLittleFsMounted();
    }

    const std::string successMessage = target == "app"
        ? "App wurde erfolgreich hochgeladen"
        : "Web-UI wurde erfolgreich hochgeladen";
    const bool deferReboot = request->hasParam("deferReboot")
        && (request->getParam("deferReboot")->value() == "1"
            || request->getParam("deferReboot")->value().equalsIgnoreCase("true"));

    if (deferReboot) {
        markUpdateSucceeded(successMessage + " (Neustart ausstehend)");
        updateState_.rebootPending = false;
        request->send(200, "application/json", (std::string("{\"status\":\"ok\",\"message\":\"") + successMessage + "\",\"deferredReboot\":true}").c_str());
        return;
    }

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
