#include "WebServerSetup.h"
#include "DebugLogger.h"
#include "ConfigStore.h"

WebServerSetup& WebServerSetup::getInstance() {
    static WebServerSetup instance;
    return instance;
}

WebServerSetup::WebServerSetup() : server_(80) {}

void WebServerSetup::init() {
    setupRoutes();
}

void WebServerSetup::start() {
    server_.begin();
    DebugLogger::getInstance().log(LogLevel::INFO, "WebServerSetup started");
}

void WebServerSetup::stop() {
    server_.end();
    DebugLogger::getInstance().log(LogLevel::INFO, "WebServerSetup stopped");
}

void WebServerSetup::setupRoutes() {
    server_.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
        std::string html = R"(
        <html>
        <body>
        <h1>WLAN Setup</h1>
        <form action="/save" method="post">
        SSID: <input type="text" name="ssid"><br>
        Password: <input type="password" name="password"><br>
        <input type="submit" value="Save and Restart">
        </form>
        </body>
        </html>
        )";
        request->send(200, "text/html", html.c_str());
    });

    server_.on("/save", HTTP_POST, [](AsyncWebServerRequest *request) {
        std::string ssid = request->arg("ssid").c_str();
        std::string password = request->arg("password").c_str();
        WifiConfig config{ssid, password};
        WifiManager::getInstance().setConfig(config);
        request->send(200, "text/plain", "Configuration saved. Restarting...");
        delay(1000);
        ESP.restart();
    });
}