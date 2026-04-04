#ifndef WEB_SERVER_DASHBOARD_H
#define WEB_SERVER_DASHBOARD_H

#include <ESPAsyncWebServer.h>
#include <AsyncWebSocket.h>
#include <string>
#include <esp_partition.h>
#include <mbedtls/sha256.h>
#include "SensorManager.h"
#include "DebugLogger.h"

class WebServerDashboard {
public:
    static WebServerDashboard& getInstance();

    void init();
    void start();
    void broadcastSensorData();
    void broadcastWifiData();
    void broadcastUptime();
    void broadcastMqttState();
    std::string getInstalledVersion() const;
    std::string getAvailableVersion(bool forceRefresh = false);
    std::string getLatestReleaseUrl(bool forceRefresh = false);
    bool requestRepoUpdate(const std::string& target, std::string& error);
    bool isUpdateInProgress() const;
    int getUpdateProgressPercent() const;

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
        std::string signatureAlgorithm;
        std::string signatureValue;
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
        unsigned long lastActivityMs = 0;
    };

    WebServerDashboard();
    AsyncWebServer server_;
    AsyncWebSocket ws_;
    UpdateState updateState_;
    ReleaseManifest cachedManifest_;
    ReleaseManifest localManifest_;
    std::string manifestError_;
    unsigned long lastManifestCheckMs_ = 0;
    bool uploadActive_ = false;
    bool uploadFailed_ = false;
    bool restartScheduled_ = false;
    bool littleFsMounted_ = false;
    bool uploadShaActive_ = false;
    String uploadTarget_;
    String uploadFilename_;
    std::string uploadExpectedSha_;
    mbedtls_sha256_context uploadShaContext_;
    const esp_partition_t* uploadTargetPartition_ = nullptr;

    void setupRoutes();
    void setupUpdateRoutes();
    bool ensureLittleFsMounted();
    void onWsEvent(AsyncWebSocket *server, AsyncWebSocketClient *client, AwsEventType type, void *arg, uint8_t *data, size_t len);

    void resetUpdateState(const std::string& source, const std::string& target);
    void touchUpdateActivity();
    void setUpdatePhase(const std::string& phase, const std::string& message, size_t received = 0, size_t total = 0);
    void clearUploadSession(bool abortUpdate = false);
    void recoverStuckUploadIfNeeded();
    void markUpdateFailed(const std::string& message);
    void markUpdateSucceeded(const std::string& message);
    void scheduleRestart(uint32_t delayMs = 1500);
    void sendUpdateStatus(AsyncWebServerRequest *request);
    void sendUpdateManifest(AsyncWebServerRequest *request);
    bool fetchLatestManifest(ReleaseManifest& manifest, std::string& rawManifest, std::string& error);
    bool parseReleaseManifest(const std::string& rawManifest, ReleaseManifest& manifest, std::string& error) const;
    bool importLocalManifest(const std::string& rawManifest, std::string& error);
    const ReleaseManifest* resolveUploadManifest(std::string& error);
    bool refreshManifestCache(bool forceRefresh, std::string& error);
    std::string buildManifestSigningPayload(const ReleaseManifest& manifest) const;
    bool verifyManifestSignature(const ReleaseManifest& manifest, std::string& error) const;
    std::string resolveUpdateTarget(const ReleaseManifest& manifest, const std::string& requestedTarget, std::string& error) const;
    void startRemoteUpdateTask(const std::string& target);
    void runRemoteUpdateTask(const std::string& target);
    bool applyRemoteAsset(const ManifestAsset& asset, int command, const std::string& phase, const std::string& version, std::string& error);
    bool validateUploadStart(const String& target, const String& filename, size_t contentLength, std::string& error) const;
    bool validateUploadChunk(const String& target, const uint8_t *data, size_t len, size_t index, std::string& error) const;
    void handleUpload(AsyncWebServerRequest *request, const String& target, const String& filename, size_t index, uint8_t *data, size_t len, bool final);
    size_t getAppPartitionSize() const;
    size_t getFilesystemPartitionSize() const;

    unsigned long uploadLastChunkMs_ = 0;
};

#endif // WEB_SERVER_DASHBOARD_H