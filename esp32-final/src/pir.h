#pragma once
#include <Arduino.h>
void pir_begin();
void pir_loop(uint32_t nowMs);
void pir_cancel_timer_for_light(int lightIdx);
void pir_start_full_timer_for_light(int lightIdx, uint32_t nowMs);
