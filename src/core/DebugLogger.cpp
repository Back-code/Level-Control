#include "DebugLogger.h"
#include <Arduino.h>
#include <ArduinoJson.h>

DebugLogger& DebugLogger::getInstance() {
    static DebugLogger instance;
    return instance;
}

DebugLogger::DebugLogger() {}

void DebugLogger::log(LogLevel level, const std::string& message) {
    const unsigned long timestamp = millis();

    // Serial output
    std::string levelStr;
    switch (level) {
        case LogLevel::ERROR: levelStr = "ERROR"; break;
        case LogLevel::WARN: levelStr = "WARN"; break;
        case LogLevel::INFO: levelStr = "INFO"; break;
        case LogLevel::DEBUG: levelStr = "DEBUG"; break;
    }
#ifndef NDEBUG
    Serial.printf("[%s] %s\n", levelStr.c_str(), message.c_str());
#endif

    // WebSocket if set
    if (wsHandler_) {
        DynamicJsonDocument doc(256);
        doc["type"] = "log";
        doc["level"] = levelStr;
        doc["message"] = message;
        doc["timestamp"] = timestamp;
        std::string json;
        serializeJson(doc, json);
        wsHandler_(json);
    }
}

void DebugLogger::setWebSocketHandler(std::function<void(const std::string&)> handler) {
    wsHandler_ = handler;
}
