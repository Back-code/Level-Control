#include "WifiManager.h"
#include "DebugLogger.h"

WifiManager& WifiManager::getInstance() {
    static WifiManager instance;
    return instance;
}

WifiManager::WifiManager() {
    Config fullConfig;
    if (ConfigStore::getInstance().load(fullConfig)) {
        config_ = fullConfig.wifi;
    }
}

void WifiManager::init() {
    WiFi.setAutoReconnect(false);

    WiFi.onEvent([this](WiFiEvent_t event, WiFiEventInfo_t info) {
        DebugLogger::getInstance().log(LogLevel::INFO, "WiFi connected");
        EventBus::getInstance().publish({EventType::WIFI_CONNECTED, ""});
    }, WiFiEvent_t::ARDUINO_EVENT_WIFI_STA_CONNECTED);

    WiFi.onEvent([this](WiFiEvent_t event, WiFiEventInfo_t info) {
        resetReconnectBackoff();
        std::string ip = WiFi.localIP().toString().c_str();
        DebugLogger::getInstance().log(LogLevel::INFO, std::string("WiFi got IP: ") + ip);
        // Kein MQTT-Connect im WiFi-Event-Task: wird im main loop verarbeitet.
    }, WiFiEvent_t::ARDUINO_EVENT_WIFI_STA_GOT_IP);

    WiFi.onEvent([this](WiFiEvent_t event, WiFiEventInfo_t info) {
        int reason = info.wifi_sta_disconnected.reason;
        DebugLogger::getInstance().log(LogLevel::WARN, "WiFi disconnected (reason=" + std::to_string(reason) + ")");
        EventBus::getInstance().publish({EventType::WIFI_DISCONNECTED, ""});
        scheduleReconnect();
    }, WiFiEvent_t::ARDUINO_EVENT_WIFI_STA_DISCONNECTED);
}

void WifiManager::process() {
    if (!reconnectPending_) {
        return;
    }

    if (WiFi.status() == WL_CONNECTED) {
        reconnectPending_ = false;
        return;
    }

    unsigned long now = millis();
    if (now < nextReconnectAttemptMs_) {
        return;
    }

    reconnectPending_ = false;
    DebugLogger::getInstance().log(LogLevel::INFO, "WiFi reconnect attempt...");
    WiFi.begin(config_.ssid.c_str(), config_.password.c_str());
}

bool WifiManager::connect() {
    if (config_.ssid.empty()) return false;

    reconnectPending_ = false;
    resetReconnectBackoff();

    WiFi.begin(config_.ssid.c_str(), config_.password.c_str());
    int attempts = 0;
    while (WiFi.status() != WL_CONNECTED && attempts < 20) {
        delay(500);
        attempts++;
    }

    bool connected = (WiFi.status() == WL_CONNECTED);
    if (!connected) {
        scheduleReconnect();
    }
    return connected;
}

void WifiManager::scheduleReconnect() {
    if (config_.ssid.empty()) {
        reconnectPending_ = false;
        return;
    }

    unsigned long now = millis();
    nextReconnectAttemptMs_ = now + reconnectBackoffMs_;
    reconnectPending_ = true;

    DebugLogger::getInstance().log(
        LogLevel::WARN,
        "WiFi reconnect scheduled in " + std::to_string(reconnectBackoffMs_) + " ms"
    );

    if (reconnectBackoffMs_ < reconnectBackoffMaxMs_) {
        reconnectBackoffMs_ *= 2;
        if (reconnectBackoffMs_ > reconnectBackoffMaxMs_) {
            reconnectBackoffMs_ = reconnectBackoffMaxMs_;
        }
    }
}

void WifiManager::resetReconnectBackoff() {
    reconnectPending_ = false;
    reconnectBackoffMs_ = reconnectBackoffMinMs_;
}

void WifiManager::startAP(const std::string& ssid, const std::string& password) {
    WiFi.softAP(ssid.c_str(), password.c_str());
    DebugLogger::getInstance().log(LogLevel::INFO, "AP started: " + ssid);
}

void WifiManager::stopAP() {
    WiFi.softAPdisconnect(true);
    DebugLogger::getInstance().log(LogLevel::INFO, "AP stopped");
}

std::vector<WifiNetwork> WifiManager::scanNetworks() {
    int n = WiFi.scanNetworks();
    std::vector<WifiNetwork> networks;
    for (int i = 0; i < n; ++i) {
        networks.push_back({WiFi.SSID(i).c_str(), WiFi.RSSI(i)});
    }
    return networks;
}

void WifiManager::setConfig(const WifiConfig& config) {
    config_ = config;
    Config fullConfig;
    fullConfig.version = 1;
    fullConfig.wifi = config_;
    fullConfig.staticIp = {};
    fullConfig.behaelterhoehe = 95.0;
    fullConfig.offset = 0.0;
    ConfigStore::getInstance().save(fullConfig);
}

WifiConfig WifiManager::getConfig() const {
    return config_;
}