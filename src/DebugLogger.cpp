#include "DebugLogger.h"
#include <Arduino.h>
#include <ArduinoJson.h>

DebugLogger& DebugLogger::getInstance() {
    static DebugLogger instance;
    return instance;
}

DebugLogger::DebugLogger() {}

void DebugLogger::log(LogLevel level, const std::string& message) {
    std::lock_guard<std::mutex> lock(mutex_);
    LogEntry entry{level, message, millis()};
    entries_.push_back(entry);
    if (entries_.size() > maxEntries_) {
        entries_.erase(entries_.begin());
    }

    // Serial output
    std::string levelStr;
    switch (level) {
        case LogLevel::ERROR: levelStr = "ERROR"; break;
        case LogLevel::WARN: levelStr = "WARN"; break;
        case LogLevel::INFO: levelStr = "INFO"; break;
        case LogLevel::DEBUG: levelStr = "DEBUG"; break;
    }
    Serial.printf("[%s] %s\n", levelStr.c_str(), message.c_str());

    // WebSocket if set
    if (wsHandler_) {
        DynamicJsonDocument doc(256);
        doc["type"] = "log";
        doc["level"] = levelStr;
        doc["message"] = message;
        doc["timestamp"] = entry.timestamp;
        std::string json;
        serializeJson(doc, json);
        wsHandler_(json);
    }
}

std::vector<LogEntry> DebugLogger::getLastEntries(int count) {
    std::lock_guard<std::mutex> lock(mutex_);
    int start = std::max(0, (int)entries_.size() - count);
    return std::vector<LogEntry>(entries_.begin() + start, entries_.end());
}

void DebugLogger::setWebSocketHandler(std::function<void(const std::string&)> handler) {
    wsHandler_ = handler;
}