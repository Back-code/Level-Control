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

struct PushConfig {
    bool enabled = false;
    std::string smtpServer;
    int smtpPort = 587;
    bool useSsl = false;
    bool startTls = false;
    std::string authUser;
    std::string authPassword;
    std::string senderName;
    std::string senderEmail;
    std::string recipientEmail;
    float triggerPercent = 20.0f;
    int sendHour = 8;
    int sendMinute = 0;
    unsigned long cycleMinutes = 1440UL;
    std::string subjectTemplate = "Salzstand Warnung: {level_percent}%";
    std::string bodyTemplate = "Der Fuellstand liegt bei {level_percent}% ({level_cm} cm).";
};

struct Config {
    int version = 1;
    WifiConfig wifi;
    StaticIpConfig staticIp;
    MqttConfig mqtt;
    PushConfig push;
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