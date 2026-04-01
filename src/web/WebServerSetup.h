#ifndef WEB_SERVER_SETUP_H
#define WEB_SERVER_SETUP_H

#include <ESPAsyncWebServer.h>
#include "WifiManager.h"

class WebServerSetup {
public:
    static WebServerSetup& getInstance();

    void init();
    void start();

private:
    WebServerSetup();
    AsyncWebServer server_;
    void setupRoutes();
};

#endif // WEB_SERVER_SETUP_H