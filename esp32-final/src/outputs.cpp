#include "outputs.h"
#include "io_map.h"
#include "hw_mcp.h"
#include "persist.h"
#include "pir.h"
#include "config.h"
#include "events.h"
#include <vector>

struct CState { bool present = false; bool state = false; };
static CState L[16], O[16];
static bool L_manual[16];
struct RestoreItem { ChanType type; int idx; };
static std::vector<RestoreItem> g_restoreQueue;
static bool g_safeActive = false;
static uint32_t g_nextRestoreMs = 0;
static StateChangeCb g_cb = nullptr;

void outputs_begin() {
  Serial.println("Initializing outputs...");
  
  // Initialize output states first
  for (int i = 0; i < 16; ++i) {
    L[i].present = mcp_lights_available();
    O[i].present = mcp_outlet_pir_available();
    L[i].state = false;
    O[i].state = false;
    L_manual[i] = false;
  }
  
  // Only write to outputs if hardware is available
  for (int i = 0; i < 16; ++i) {
    if (L[i].present) {
      mcp_write(LIGHTS_MCP[i], false);
      yield(); // Feed watchdog during hardware operations
    }
    if (O[i].present && !OUTLET_MCP[i].isInput) {
      mcp_write(OUTLET_MCP[i].pin, false);
      yield(); // Feed watchdog during hardware operations
    }
  }
  for (int i = 0; i < 16; ++i) {
    bool on = false;
    persist_get_bool("L"+String(i), on);
    if (on) g_restoreQueue.push_back({ChanType::LIGHT, i});
    persist_get_bool("O"+String(i), on);
    if (on) g_restoreQueue.push_back({ChanType::OUTLET, i});
    bool manual = false;
    persist_get_bool("LM"+String(i), manual);
    L_manual[i] = manual;
  }
  g_safeActive = !g_restoreQueue.empty();
  if (g_safeActive) g_nextRestoreMs = millis() + SAFE_RESTORE_INTERVAL_MS;
  Events::logBasic(Events::Type::INIT, Events::Severity::INFO, "outputs", "Safe-restore prepared: " + String(g_restoreQueue.size()) + " outputs to restore");
}

void outputs_loop(uint32_t nowMs) {
  if (g_safeActive && nowMs >= g_nextRestoreMs) {
    if (!g_restoreQueue.empty()) {
      RestoreItem item = g_restoreQueue.front();
      g_restoreQueue.erase(g_restoreQueue.begin());
      output_set(item.type, item.idx, true, false, OutputOrigin::Restore);
      if (g_restoreQueue.empty()) {
        g_safeActive = false;
        Events::logBasic(Events::Type::INIT, Events::Severity::INFO, "outputs", "Safe-restore complete");
      } else {
        g_nextRestoreMs = nowMs + SAFE_RESTORE_INTERVAL_MS;
      }
    }
  }
  static uint32_t lastResync = 0;
  if (nowMs - lastResync > 5000) {
    for (int i = 0; i < 16; ++i) {
      if (L[i].present) {
        mcp_write(LIGHTS_MCP[i], L[i].state);
        yield(); // Feed watchdog during hardware operations
      }
      if (O[i].present && !OUTLET_MCP[i].isInput) {
        mcp_write(OUTLET_MCP[i].pin, O[i].state);
        yield(); // Feed watchdog during hardware operations
      }
    }
    lastResync = nowMs;
  }
}

bool outputs_safe_active() { return g_safeActive; }

bool output_get(ChanType t, int idx, bool &on, bool &present) {
  if (t == ChanType::LIGHT && idx >= 0 && idx < 16) {
    on = L[idx].state;
    present = L[idx].present;
    return true;
  } else if (t == ChanType::OUTLET && idx >= 0 && idx < 16) {
    on = O[idx].state;
    present = O[idx].present;
    return true;
  }
  return false;
}

bool output_set(ChanType t, int idx, bool on, bool byRule, OutputOrigin origin) {
  if (t == ChanType::LIGHT && idx >= 0 && idx < 16 && L[idx].present) {
    bool prev = L[idx].state;
    bool prevManual = L_manual[idx];
    if (L_manual[idx] && !on) L_manual[idx] = false;
    L[idx].state = on;
    mcp_write(LIGHTS_MCP[idx], on);
    if (on != prev) persist_set_bool("L"+String(idx), on);
    if (L_manual[idx] != prevManual) persist_set_bool("LM"+String(idx), L_manual[idx]);
    Events::logIO(Events::Type::IO_CHANGE, Events::Severity::INFO, idx, light_name(idx), String(!on), String(on), "outputs", "", origin);
    if (g_cb) g_cb(ChanType::LIGHT, idx, on, byRule, L_manual[idx], origin);
    return true;
  } else if (t == ChanType::OUTLET && idx >= 0 && idx < 16 && O[idx].present && !OUTLET_MCP[idx].isInput) {
    bool prev = O[idx].state;
    O[idx].state = on;
    mcp_write(OUTLET_MCP[idx].pin, on);
    if (on != prev) persist_set_bool("O"+String(idx), on);
    Events::logIO(Events::Type::IO_CHANGE, Events::Severity::INFO, idx, outlet_name(idx), String(!on), String(on), "outputs", "", origin);
    if (g_cb) g_cb(ChanType::OUTLET, idx, on, byRule, false, origin);
    return true;
  }
  return false;
}

bool output_toggle(ChanType t, int idx, bool byRule, OutputOrigin origin) {
  bool on, present;
  if (!output_get(t, idx, on, present) || !present) return false;
  return output_set(t, idx, !on, byRule, origin);
}

void outputs_set_manual_light(int idx, bool manual) {
  if (idx >= 0 && idx < 16) {
    L_manual[idx] = manual;
    persist_set_bool("LM"+String(idx), manual);
  }
}

bool outputs_get_manual_light(int idx) {
  return (idx >= 0 && idx < 16) ? L_manual[idx] : false;
}

void outputs_on_change(StateChangeCb cb) { g_cb = cb; }

const char* light_name(int idx) { return io_light_label(idx); }
const char* outlet_name(int idx) { return io_outlet_label(idx); }
