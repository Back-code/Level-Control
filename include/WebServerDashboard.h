#ifndef WEB_SERVER_DASHBOARD_H
#define WEB_SERVER_DASHBOARD_H

#include <ESPAsyncWebServer.h>
#include <AsyncWebSocket.h>
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
    WebServerDashboard();
    AsyncWebServer server_;
    AsyncWebSocket ws_;
    void setupRoutes();
    void onWsEvent(AsyncWebSocket *server, AsyncWebSocketClient *client, AwsEventType type, void *arg, uint8_t *data, size_t len);
};

#endif // WEB_SERVER_DASHBOARD_H