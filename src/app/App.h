#ifndef APP_H
#define APP_H

class App {
public:
    static App& getInstance();

    void init();
    void loop();

private:
    App() = default;

    unsigned long lastBroadcast_     = 0;
    unsigned long lastSerial_        = 0;
    unsigned long lastMeasurement_   = 0;
    unsigned long lastMqttReconnect_ = 0;
    unsigned long lastMqttPublish_   = 0;
    unsigned long lastHistoryCheck_  = 0;
};

#endif // APP_H
