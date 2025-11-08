#pragma once
#include <Arduino.h>
void mqtt_begin();
void mqtt_loop();
void mqtt_pub_state_light(int idx, bool on);
void mqtt_pub_state_outlet(int idx, bool on);