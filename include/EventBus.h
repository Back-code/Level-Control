#ifndef EVENT_BUS_H
#define EVENT_BUS_H

#include <functional>
#include <vector>
#include <map>
#include <string>

enum class EventType {
    WIFI_CONNECTED,
    WIFI_DISCONNECTED,
    SYSTEM_MQTT_CONNECTED,
    SYSTEM_MQTT_DISCONNECTED,
    SENSOR_TIMEOUT,
    SENSOR_OUT_OF_RANGE,
    CONFIG_SAVED,
    OTA_STARTED,
    OTA_SUCCESS,
    OTA_FAILED
};

struct Event {
    EventType type;
    std::string data; // JSON string for additional data
};

class EventBus {
public:
    using EventHandler = std::function<void(const Event&)>;

    static EventBus& getInstance();

    void subscribe(EventType type, EventHandler handler);
    void publish(const Event& event);

private:
    EventBus() = default;
    std::map<EventType, std::vector<EventHandler>> handlers_;
};

#endif // EVENT_BUS_H