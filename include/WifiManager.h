#ifndef WIFI_MANAGER_H
#define WIFI_MANAGER_H
#include "WebConstants.h"

#include <WiFi.h>
#include <vector>
#include <string>
#include "ConfigStore.h"
#include "EventBus.h"

struct WifiNetwork {
    std::string ssid;
    int rssi;
};

class WifiManager {
public:
    static WifiManager& getInstance();

    void init();
    void process();
    bool connect();
    void startAP(const std::string& ssid, const std::string& password, uint8_t maxConnections = 1);
    void stopAP();
    std::vector<WifiNetwork> scanNetworks();
    void setConfig(const WifiConfig& config, const StaticIpConfig& staticConfig = {});
    WifiConfig getConfig() const;
    std::string getMdnsHostname() const;
    std::string getLocalUrl() const;
    bool isConnected() const { return WiFi.status() == WL_CONNECTED; }
    std::string getIP() const { return WiFi.localIP().toString().c_str(); }
    std::string getSSID() const { return WiFi.SSID().c_str(); }
    std::string getBSSID() const { return WiFi.BSSIDstr().c_str(); }
    int getSignalStrength() const { return WiFi.RSSI(); }

private:
    WifiManager();
    void onWifiEvent(WiFiEvent_t event);
    void scheduleReconnect();
    void resetReconnectBackoff();
    void applyStationIdentity();
    void startMdns();
    void stopMdns();
    WifiConfig config_;
    StaticIpConfig staticConfig_;
    bool mdnsRunning_ = false;
    bool reconnectPending_ = false;
    unsigned long nextReconnectAttemptMs_ = 0;
    unsigned long reconnectBackoffMs_ = 2000;
    static constexpr unsigned long reconnectBackoffMinMs_ = WebConstants::WIFI_RECONNECT_BACKOFF_MIN_MS;
    static constexpr unsigned long reconnectBackoffMaxMs_ = WebConstants::WIFI_RECONNECT_BACKOFF_MAX_MS;
};

#endif // WIFI_MANAGER_H