#ifndef DEBUG_LOGGER_H
#define DEBUG_LOGGER_H

#include <string>
#include <vector>
#include <mutex>

enum class LogLevel {
    ERROR,
    WARN,
    INFO,
    DEBUG
};

struct LogEntry {
    LogLevel level;
    std::string message;
    unsigned long timestamp;
};

class DebugLogger {
public:
    static DebugLogger& getInstance();

    void log(LogLevel level, const std::string& message);
    std::vector<LogEntry> getLastEntries(int count = 50);
    void setWebSocketHandler(std::function<void(const std::string&)> handler);

private:
    DebugLogger();
    std::vector<LogEntry> entries_;
    std::mutex mutex_;
    std::function<void(const std::string&)> wsHandler_;
    const int maxEntries_ = 100;
};

#endif // DEBUG_LOGGER_H