#pragma once
#include <Arduino.h>
bool net_begin();
void net_loop();
bool net_have_ip();
String net_ip_str();
bool net_is_ap();