#ifndef CONFIG_STORE_H
#define CONFIG_STORE_H

#include <string>

struct WifiConfig {
    std::string ssid;
    std::string password;
};

struct StaticIpConfig {
    std::string ip;
    std::string gateway;
    std::string subnet;
    std::string dns;
};

struct MqttConfig {
    std::string server;
    int port = 1883;
    std::string user;
    std::string password;
    bool discovery = true;
};

struct Config {
    int version = 1;
    WifiConfig wifi;
    StaticIpConfig staticIp;
    MqttConfig mqtt;
    float behaelterhoehe = 95.0;
    float offset = 0.0;
    unsigned long sampleIntervalSeconds = 5;
};

class ConfigStore {
public:
    static ConfigStore& getInstance();

    bool load(Config& config);
    bool save(const Config& config);

private:
    ConfigStore();
    const char* nvsNamespace_ = "config";
};

#endif // CONFIG_STORE_H