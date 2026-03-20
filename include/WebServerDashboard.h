#ifndef WEB_SERVER_DASHBOARD_H
#define WEB_SERVER_DASHBOARD_H

#include <ESPAsyncWebServer.h>
#include <AsyncWebSocket.h>
#include <string>
#include "SensorManager.h"
#include "DebugLogger.h"

class WebServerDashboard {
public:
    static WebServerDashboard& getInstance();

    void init();
    void start();
    void stop();
    void broadcastSensorData();
    void broadcastWifiData();
    void broadcastUptime();

private:
    struct ManifestAsset {
        std::string name;
        std::string url;
        std::string sha256;
        size_t size = 0;
    };

    struct ReleaseManifest {
        std::string version;
        std::string releaseUrl;
        ManifestAsset app;
        ManifestAsset webui;
        bool valid = false;
    };

    struct UpdateState {
        bool inProgress = false;
        bool success = false;
        bool rebootPending = false;
        std::string source;
        std::string target;
        std::string phase;
        std::string message;
        std::string availableVersion;
        size_t received = 0;
        size_t total = 0;
    };

    WebServerDashboard();
    AsyncWebServer server_;
    AsyncWebSocket ws_;
    UpdateState updateState_;
    bool uploadActive_ = false;
    bool uploadFailed_ = false;
    bool restartScheduled_ = false;
    String uploadTarget_;
    String uploadFilename_;

    void setupRoutes();
    void setupUpdateRoutes();
    void onWsEvent(AsyncWebSocket *server, AsyncWebSocketClient *client, AwsEventType type, void *arg, uint8_t *data, size_t len);

    void resetUpdateState(const std::string& source, const std::string& target);
    void setUpdatePhase(const std::string& phase, const std::string& message, size_t received = 0, size_t total = 0);
    void markUpdateFailed(const std::string& message);
    void markUpdateSucceeded(const std::string& message);
    void scheduleRestart(uint32_t delayMs = 1500);
    void sendUpdateStatus(AsyncWebServerRequest *request) const;
    void sendUpdateManifest(AsyncWebServerRequest *request);
    bool fetchLatestManifest(ReleaseManifest& manifest, std::string& rawManifest, std::string& error);
    void startRemoteUpdateTask(const std::string& target);
    void runRemoteUpdateTask(const std::string& target);
    bool applyRemoteAsset(const ManifestAsset& asset, int command, const std::string& phase, std::string& error);
    bool validateUploadStart(const String& target, const String& filename, size_t contentLength, std::string& error) const;
    bool validateUploadChunk(const String& target, const uint8_t *data, size_t len, size_t index, std::string& error) const;
    void handleUpload(AsyncWebServerRequest *request, const String& target, const String& filename, size_t index, uint8_t *data, size_t len, bool final);
    size_t getAppPartitionSize() const;
    size_t getFilesystemPartitionSize() const;
};

#endif // WEB_SERVER_DASHBOARD_H