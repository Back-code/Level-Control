#ifndef DEBUG_LOGGER_H
#define DEBUG_LOGGER_H

#include <functional>
#include <string>

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
    void setWebSocketHandler(std::function<void(const std::string&)> handler);

private:
    DebugLogger();
    std::function<void(const std::string&)> wsHandler_;
};

#endif // DEBUG_LOGGER_H
