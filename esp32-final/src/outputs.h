#pragma once
#include <Arduino.h>
#include <functional>

enum class ChanType { LIGHT, OUTLET };
enum class OutputOrigin : uint8_t { Unknown=0, Manual, WebUI, Button, PIR, Rule, MQTT, Restore, System };

void outputs_begin();
bool output_get(ChanType t, int idx, bool &on, bool &present);
bool output_set(ChanType t, int idx, bool on, bool byRule=false, OutputOrigin origin=OutputOrigin::Unknown);
bool output_toggle(ChanType t, int idx, bool byRule=false, OutputOrigin origin=OutputOrigin::Unknown);
void outputs_loop(uint32_t nowMs);
bool outputs_safe_active();
void outputs_set_manual_light(int idx, bool manual);
bool outputs_get_manual_light(int idx);
using StateChangeCb = std::function<void(ChanType,int,bool,bool,bool,OutputOrigin)>;
void outputs_on_change(StateChangeCb cb);
const char* light_name(int idx);
const char* outlet_name(int idx);
