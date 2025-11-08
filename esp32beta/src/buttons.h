#pragma once
#include <Arduino.h>
typedef void (*ButtonHandler)(int idx, bool longPress);
void buttons_begin(ButtonHandler cb);
void buttons_loop(uint32_t nowMs);