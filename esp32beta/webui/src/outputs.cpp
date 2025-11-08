#include "outputs.h"
#include "io_map.h"
#include "hw_mcp.h"
#include "persist.h"
#include "pir.h"
#include "config.h"
#include "events.h"

static StateChangeCb g_cb;

struct CState { bool present=false; bool state=false; };
static CState L[16], O[16];
static bool L_manual[16]; // manual mode for lights (per requirements it matters for svjetlo_vani=0 and svjetlo_ulaz=3, but keep generic)

static inline bool isPirLight(int idx){
  return idx == PIR_VANI_LIGHT_ID || idx == PIR_ULAZ_LIGHT_ID;
}

static OutputOrigin normalize_origin(bool byRule, OutputOrigin origin){
  if (origin != OutputOrigin::Unknown) return origin;
  return byRule ? OutputOrigin::System : OutputOrigin::Manual;
}

const char* light_name(int i){ return io_light_label(i); }
const char* outlet_name(int i){ return io_outlet_label(i); }

// ===== Low-level drivers =====
static void driveLight(int idx, bool on){
  mcp_write(LIGHTS_MCP[idx], on);
}
static void driveOutlet(int idx, bool on){
  if (OUTLET_MCP[idx].isInput) return; // safety
  mcp_write(OUTLET_MCP[idx].pin, on);
}

// ===== Persistence helpers =====
static void save_one(ChanType t, int idx, bool v){
  String key = String((t==ChanType::LIGHT)?"L":"O")+String(idx);
  persist_set_bool(key, v);
}
static void save_manual_flag(int idx, bool v){
  String key = String("LM")+String(idx);
  persist_set_bool(key, v);
}

// ===== Safe-start queue =====
struct SafeItem {
  ChanType t;
  int idx;
  uint32_t atMs;         // when to turn ON
  bool startPirTimer;    // for PIR lights in auto mode
};
static SafeItem SAFE_Q[40];
static uint8_t  SAFE_N = 0;
static bool     SAFE_ACTIVE = false;

static void safeq_clear(){
  SAFE_N = 0;
  SAFE_ACTIVE = false;
}
static void safeq_add(ChanType t, int idx, uint32_t whenMs, bool startPirTimer){
  if (SAFE_N >= (uint8_t)(sizeof(SAFE_Q)/sizeof(SafeItem))) return;
  SAFE_Q[SAFE_N++] = SafeItem{t, idx, whenMs, startPirTimer};
  SAFE_ACTIVE = true;
}
bool outputs_safe_active(){ return SAFE_ACTIVE; }

// ===== API =====
void outputs_begin(){
  mcp_begin_all();

  // Accurate device/channel presence based on real MCP detection
  const bool lightsOk = mcp_lights_available();
  const bool outletOk = mcp_outlet_pir_available();

  for (int i = 0; i < 16; ++i) {
    L[i].present = lightsOk;
    O[i].present = outletOk && !OUTLET_MCP[i].isInput; // PIR channels are inputs; not output-capable
    L[i].state = false;
    O[i].state = false;
    L_manual[i] = false;
  }
}

bool output_get(ChanType t, int idx, bool &on, bool &present){
  if (idx<0 || idx>=16) return false;
  if (t==ChanType::LIGHT){ present=L[idx].present; on=L[idx].state; }
  else { present=O[idx].present && !OUTLET_MCP[idx].isInput; on=O[idx].state; }
  return present;
}

void outputs_set_manual_light(int idx, bool manual){
  if (idx<0 || idx>=16) return;
  if (!L[idx].present) return;
  L_manual[idx] = manual;
  save_manual_flag(idx, manual);
}
bool outputs_get_manual_light(int idx){
  if (idx<0 || idx>=16) return false;
  return L_manual[idx];
}

// First-run init OR load from NVS and schedule safe-start
void outputs_load_from_nvs(){
  safeq_clear();
  
  // Force all outputs OFF initially for safety
  for (int i=0;i<16;i++){
    if (L[i].present) { L[i].state=false; driveLight(i, false); }
    if (O[i].present) { O[i].state=false; driveOutlet(i, false); }
  }
  
  // Load manual flags and states from persistence
  uint32_t nowMs = millis();
  for (int i=0;i<16;i++){
    if (L[i].present){
      // Load manual flag
      bool manual = false;
      persist_get_bool(String("LM")+String(i), manual);
      L_manual[i] = manual;
      
      // Load previous state
      bool wasOn = false;
      persist_get_bool(String("L")+String(i), wasOn);
      
      if (wasOn){
        // Schedule safe-start ON after 2-second delay
        bool startPirTimer = isPirLight(i) && !manual; // Start PIR timer only if was in auto mode
        safeq_add(ChanType::LIGHT, i, nowMs + 2000, startPirTimer);
      }
    }
    if (O[i].present && !OUTLET_MCP[i].isInput){
      // Load previous outlet state
      bool wasOn = false;
      persist_get_bool(String("O")+String(i), wasOn);
      
      if (wasOn){
        // Schedule safe-start ON after 3-second delay (outlets after lights)
        safeq_add(ChanType::OUTLET, i, nowMs + 3000, false);
      }
    }
  }
  
  if (SAFE_ACTIVE){
    Serial.println("[OUTPUTS] Loaded persistent states; scheduled safe-start sequence");
  } else {
    Serial.println("[OUTPUTS] No persistent states to restore");
  }
}

// Run safe-start schedule; must be called from main loop
void outputs_loop(uint32_t nowMs){
  if (!SAFE_ACTIVE) return;
  // Process any items whose time has come
  bool anyPending=false;
  for (uint8_t i=0;i<SAFE_N;i++){
    SafeItem &it = SAFE_Q[i];
    if (it.atMs==0) continue; // already processed
    if ((int32_t)(nowMs - it.atMs) >= 0){
      // Turn ON the channel
      if (it.t==ChanType::LIGHT){
        // Use byRule=true (system) to avoid setting manual inadvertently
        output_set(ChanType::LIGHT, it.idx, true, true, OutputOrigin::Restore);
        // If PIR-controlled and auto mode, start full timer (requirement)
        if (it.startPirTimer){
          pir_start_full_timer_for_light(it.idx, nowMs);
        }
      } else {
        output_set(ChanType::OUTLET, it.idx, true, true, OutputOrigin::Restore);
      }
      it.atMs = 0; // mark as processed
    } else {
      anyPending = true;
    }
  }
  // Determine if still active
  bool left=false;
  for (uint8_t i=0;i<SAFE_N;i++){ if (SAFE_Q[i].atMs!=0){ left=true; break; } }
  SAFE_ACTIVE = left;
}

// Core setter with manual/auto semantics and persistence
bool output_set(ChanType t, int idx, bool on, bool byRule, OutputOrigin origin){
  if (idx<0||idx>=16) return false;

  if (t==ChanType::LIGHT){
    if (!L[idx].present) return false;

    // Manual semantics for user actions (web/button): byRule==false means user
    if (!byRule){
      if (on){
        // Manual ON: latch manual=true and cancel PIR timer
        if (!L_manual[idx]){
          L_manual[idx]=true; save_manual_flag(idx, true);
        }
        pir_cancel_timer_for_light(idx);
      } else {
        // Manual OFF: manual=false, cancel PIR timer; returns to automatic
        if (L_manual[idx]){
          L_manual[idx]=false; save_manual_flag(idx, false);
        }
        pir_cancel_timer_for_light(idx);
      }
    }

    if (L[idx].state==on){
      // State unchanged; already updated manual flags above if needed
      return true;
    }
    bool prevState = L[idx].state;
    L[idx].state=on; driveLight(idx,on); save_one(t,idx,on);
    
    // Log the IO change with full details
    OutputOrigin resolvedOrigin = normalize_origin(byRule, origin);
    Events::logIO(Events::Severity::INFO,
                  String("light:") + String(idx),
                  idx,
                  light_name(idx),
                  prevState ? "ON" : "OFF",
                  on ? "ON" : "OFF",
                  resolvedOrigin == OutputOrigin::WebUI ? "web" :
                  resolvedOrigin == OutputOrigin::Button ? "button" :
                  resolvedOrigin == OutputOrigin::PIR ? "pir" :
                  resolvedOrigin == OutputOrigin::Rule ? "rule" :
                  resolvedOrigin == OutputOrigin::MQTT ? "mqtt" :
                  resolvedOrigin == OutputOrigin::Restore ? "restore" :
                  resolvedOrigin == OutputOrigin::System ? "system" : "manual");
    
    if (g_cb) g_cb(t,idx,on,byRule,L_manual[idx],resolvedOrigin);
    return true;

  } else {
    if (!O[idx].present || OUTLET_MCP[idx].isInput) return false;
    if (O[idx].state==on) return true;
    bool prevState = O[idx].state;
    O[idx].state=on; driveOutlet(idx,on); save_one(t,idx,on);
    
    // Log the IO change with full details
    OutputOrigin resolvedOrigin = normalize_origin(byRule, origin);
    Events::logIO(Events::Severity::INFO,
                  String("outlet:") + String(idx),
                  idx,
                  outlet_name(idx),
                  prevState ? "ON" : "OFF",
                  on ? "ON" : "OFF",
                  resolvedOrigin == OutputOrigin::WebUI ? "web" :
                  resolvedOrigin == OutputOrigin::Button ? "button" :
                  resolvedOrigin == OutputOrigin::PIR ? "pir" :
                  resolvedOrigin == OutputOrigin::Rule ? "rule" :
                  resolvedOrigin == OutputOrigin::MQTT ? "mqtt" :
                  resolvedOrigin == OutputOrigin::Restore ? "restore" :
                  resolvedOrigin == OutputOrigin::System ? "system" : "manual");
    
    if (g_cb) g_cb(t,idx,on,byRule,false,resolvedOrigin);
    return true;
  }
}

// Special toggle to support manual latch when ON due to PIR (auto)
bool output_toggle(ChanType t, int idx, bool byRule, OutputOrigin origin){
  bool on,pres;
  if (!output_get(t,idx,on,pres) || !pres) return false;

  if (t==ChanType::LIGHT){
    // If user pressed while light is ON but in auto mode (due to PIR), latch to manual and keep ON
    if (!byRule && on && !L_manual[idx]){
      L_manual[idx]=true; save_manual_flag(idx, true);
      pir_cancel_timer_for_light(idx);
      // no state change
      if (g_cb) g_cb(t,idx,on,byRule,L_manual[idx],normalize_origin(byRule, origin));
      return true;
    }
  }

  return output_set(t, idx, !on, byRule, origin);
}

void outputs_on_change(StateChangeCb cb){ g_cb=cb; }



