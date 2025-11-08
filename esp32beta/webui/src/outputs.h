#pragma once

#include <Arduino.h>
#include <functional>

enum class ChanType { LIGHT, OUTLET };

// Origin of an output change for event logging and automations
enum class OutputOrigin : uint8_t {
  Unknown = 0,
  Manual,      // manual but unspecified source
  WebUI,       // web interface
  Button,      // physical button press
  PIR,         // motion sensor
  Rule,        // rule engine / automation
  MQTT,        // MQTT command
  Restore,     // state restored after reboot
  System       // internal/system initiated
};

// Init
void outputs_begin();

// State accessors
bool output_get (ChanType t, int idx, bool &on, bool &present);
bool output_set (ChanType t, int idx, bool on, bool byRule=false, OutputOrigin origin = OutputOrigin::Unknown);
bool output_toggle(ChanType t, int idx, bool byRule=false, OutputOrigin origin = OutputOrigin::Unknown);

// Persistent restore with safe-start (deferred ON sequence)
// This loads persisted states and manual flags, ensures all are OFF,
// and schedules ON for previously-ON devices via safe start.
void outputs_load_from_nvs();

// Safe-start processing (call from main loop)
void outputs_loop(uint32_t nowMs);
bool outputs_safe_active();

// Manual mode (lights only)
void outputs_set_manual_light(int idx, bool manual);
bool outputs_get_manual_light(int idx);

// Change callback
using StateChangeCb = std::function<void(ChanType,int,bool,bool,bool,OutputOrigin)>;
void outputs_on_change(StateChangeCb cb);

// Names
const char* light_name(int idx);
const char* outlet_name(int idx);
