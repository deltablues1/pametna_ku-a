#pragma once
#include <Arduino.h>

// PIR lifecycle
void pir_begin();
void pir_loop(uint32_t nowMs);

// Timers control (used by outputs/manual logic and safe-start)
void pir_cancel_timer_for_light(int lightIdx);
void pir_start_full_timer_for_light(int lightIdx, uint32_t nowMs);