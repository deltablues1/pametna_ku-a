#pragma once
#include <Arduino.h>
void web_begin();
void web_loop();
size_t sse_client_count();
void sse_send_keep(uint32_t nowMs);
