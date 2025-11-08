#include "pir.h"
#include "io_map.h"
#include "hw_mcp.h"
#include "config.h"
#include "outputs.h"

static volatile bool g_pirInt=false;
static void IRAM_ATTR isrPIR(){ g_pirInt=true; }

static uint32_t offAtVani=0, offAtUlaz=0;

static inline uint32_t timeoutForLight(int lightIdx){
  if (lightIdx==PIR_VANI_LIGHT_ID) return PIR_VANI_TIMEOUT_MS;
  if (lightIdx==PIR_ULAZ_LIGHT_ID) return PIR_ULAZ_TIMEOUT_MS;
  return 0;
}

void pir_cancel_timer_for_light(int lightIdx){
  if (lightIdx==PIR_VANI_LIGHT_ID) offAtVani = 0;
  if (lightIdx==PIR_ULAZ_LIGHT_ID) offAtUlaz = 0;
}

void pir_start_full_timer_for_light(int lightIdx, uint32_t nowMs){
  if (lightIdx==PIR_VANI_LIGHT_ID) offAtVani = nowMs + PIR_VANI_TIMEOUT_MS;
  if (lightIdx==PIR_ULAZ_LIGHT_ID) offAtUlaz = nowMs + PIR_ULAZ_TIMEOUT_MS;
}

void pir_begin(){
  // INT line from MCP (0x22) already configured in hw_mcp; use FALLING to catch low-active INT
  if (!mcp_outlet_pir_available()){
    Serial.println("[PIR] MCP outlet expander not available; PIR sensors disabled");
    return;
  }
  attachInterrupt(digitalPinToInterrupt(INT_PIR_GPIO), isrPIR, FALLING);
  // No need to preload states; timers start on detection or via safe-start hook
}

static void handleExpiry(uint32_t now){
  // VANI
  if (offAtVani && (int32_t)(now - offAtVani) >= 0){
    // If manual ON occurred in the meantime, do not turn off
    if (!outputs_get_manual_light(PIR_VANI_LIGHT_ID)){
      output_set(ChanType::LIGHT, PIR_VANI_LIGHT_ID, false, true, OutputOrigin::PIR);
    }
    offAtVani = 0;
  }
  // ULAZ
  if (offAtUlaz && (int32_t)(now - offAtUlaz) >= 0){
    if (!outputs_get_manual_light(PIR_ULAZ_LIGHT_ID)){
      output_set(ChanType::LIGHT, PIR_ULAZ_LIGHT_ID, false, true, OutputOrigin::PIR);
    }
    offAtUlaz = 0;
  }
}

void pir_loop(uint32_t now){
  if (!mcp_outlet_pir_available()) { g_pirInt = false; handleExpiry(now); return; }
  // During safe-start: ignore new PIR detections, but still allow expiry processing
  if (outputs_safe_active()){
    handleExpiry(now);
    return;
  }

  if (g_pirInt){
    // Read both PIR inputs (HIGH = motion)
    bool sV = mcp_read(OUTLET_MCP[PIR_VANI_IDX].pin);
    bool sU = mcp_read(OUTLET_MCP[PIR_ULAZ_IDX].pin);

    // VANI handling (auto mode only)
    if (sV && !outputs_get_manual_light(PIR_VANI_LIGHT_ID)){
      output_set(ChanType::LIGHT, PIR_VANI_LIGHT_ID, true, true, OutputOrigin::PIR);
      offAtVani = now + PIR_VANI_TIMEOUT_MS; // retrigger extends to full duration
    }
    // ULAZ handling (auto mode only)
    if (sU && !outputs_get_manual_light(PIR_ULAZ_LIGHT_ID)){
      output_set(ChanType::LIGHT, PIR_ULAZ_LIGHT_ID, true, true, OutputOrigin::PIR);
      offAtUlaz = now + PIR_ULAZ_TIMEOUT_MS; // retrigger extends to full duration
    }

    g_pirInt = false;
  }

  handleExpiry(now);
}
