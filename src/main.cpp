#include <Arduino.h>
#include "App.h"

void setup() {
    App::getInstance().init();
}

void loop() {
    App::getInstance().loop();
}
