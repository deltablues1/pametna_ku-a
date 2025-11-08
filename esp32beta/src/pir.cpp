#include "pir.h"
#include "io_map.h"
#include "hw_mcp.h"
#include "config.h"
#include "outputs.h"
#include "events.h"

static volatile bool g_pirInt=false;
static void IRAM_ATTR isrPIR(){ g_pirInt=true; }

static uint32_t offAtVani=0, offAtUlaz=0;
static bool stateVani=false, stateUlaz=false;
static bool validVani=false, validUlaz=false;
static bool warnedVaniMissing=false, warnedUlazMissing=false;
static uint32_t lastLowVani=0, lastLowUlaz=0;

static constexpr uint32_t PIR_GLITCH_MS = 30;
static constexpr uint32_t PIR_VALID_LOW_MS = 200;

static inline uint8_t hwPinForIdx(int idx){
  const auto& op = OUTLET_MCP[idx];
  return op.pin.pin + (op.pin.portB ? 8 : 0);
}

static inline const char* pirDeviceId(bool vani){
  return vani ? "pir:svjetlo_vani" : "pir:svjetlo_ulaz";
}

static inline const char* pirHumanName(bool vani){
  return vani ? "PIR vani" : "PIR ulaz";
}

static void ensureMissingLogged(bool vani){
  bool valid = vani ? validVani : validUlaz;
  bool &warned = vani ? warnedVaniMissing : warnedUlazMissing;
  if (!valid && !warned){
    Events::logBasic(Events::Type::WARNING_EVT,
                     Events::Severity::WARN,
                     pirDeviceId(vani),
                     String(pirHumanName(vani)) + " nije detektiran; ignoriram lazne okidace dok se senzor ne spoji.",
                     "system");
    warned = true;
  }
}

static void clearMissingWarning(bool vani){
  bool &warned = vani ? warnedVaniMissing : warnedUlazMissing;
  if (warned){
    warned = false;
    Events::logBasic(Events::Type::DISCOVERY,
                     Events::Severity::INFO,
                     pirDeviceId(vani),
                     String(pirHumanName(vani)) + " ponovno aktivan.",
                     "system");
  }
}

static void notePresent(bool vani){
  bool &valid = vani ? validVani : validUlaz;
  if (!valid){
    valid = true;
    clearMissingWarning(vani);
  }
}

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
  if (!mcp_outlet_pir_available()){
    Serial.println("[PIR] MCP outlet expander not available; PIR sensors disabled");
    return;
  }
  attachInterrupt(digitalPinToInterrupt(INT_PIR_GPIO), isrPIR, FALLING);

  uint32_t now = millis();
  stateVani = mcp_read(OUTLET_MCP[PIR_VANI_IDX].pin);
  stateUlaz = mcp_read(OUTLET_MCP[PIR_ULAZ_IDX].pin);
  if (!stateVani){
    validVani = true;
    lastLowVani = now;
  } else {
    validVani = false;
    lastLowVani = 0;
  }
  if (!stateUlaz){
    validUlaz = true;
    lastLowUlaz = now;
  } else {
    validUlaz = false;
    lastLowUlaz = 0;
  }
  ensureMissingLogged(true);
  ensureMissingLogged(false);
}

static void handleExpiry(uint32_t now){
  if (offAtVani && (int32_t)(now - offAtVani) >= 0){
    if (!outputs_get_manual_light(PIR_VANI_LIGHT_ID)){
      output_set(ChanType::LIGHT, PIR_VANI_LIGHT_ID, false, true, OutputOrigin::PIR);
    }
    offAtVani = 0;
  }
  if (offAtUlaz && (int32_t)(now - offAtUlaz) >= 0){
    if (!outputs_get_manual_light(PIR_ULAZ_LIGHT_ID)){
      output_set(ChanType::LIGHT, PIR_ULAZ_LIGHT_ID, false, true, OutputOrigin::PIR);
    }
    offAtUlaz = 0;
  }
}

static void processPirInterrupt(uint8_t hwPin, bool level, uint32_t now){
  const uint8_t pinVani = hwPinForIdx(PIR_VANI_IDX);
  const uint8_t pinUlaz = hwPinForIdx(PIR_ULAZ_IDX);

  if (hwPin == pinVani){
    bool prev = stateVani;
    stateVani = level;
    if (!level){
      lastLowVani = now;
      notePresent(true);
      return;
    }
    uint32_t lowAt = lastLowVani;
    if (lowAt == 0){
      ensureMissingLogged(true);
      return;
    }
    uint32_t elapsed = now - lowAt;
    if (elapsed < PIR_GLITCH_MS){
      return;
    }
    if (!validVani){
      if (elapsed >= PIR_VALID_LOW_MS){
        notePresent(true);
      } else {
        ensureMissingLogged(true);
        return;
      }
    }
    if (!prev && !outputs_get_manual_light(PIR_VANI_LIGHT_ID)){
      output_set(ChanType::LIGHT, PIR_VANI_LIGHT_ID, true, true, OutputOrigin::PIR);
      offAtVani = now + PIR_VANI_TIMEOUT_MS;
    }
    return;
  }
  if (hwPin == pinUlaz){
    bool prev = stateUlaz;
    stateUlaz = level;
    if (!level){
      lastLowUlaz = now;
      notePresent(false);
      return;
    }
    uint32_t lowAt = lastLowUlaz;
    if (lowAt == 0){
      ensureMissingLogged(false);
      return;
    }
    uint32_t elapsed = now - lowAt;
    if (elapsed < PIR_GLITCH_MS){
      return;
    }
    if (!validUlaz){
      if (elapsed >= PIR_VALID_LOW_MS){
        notePresent(false);
      } else {
        ensureMissingLogged(false);
        return;
      }
    }
    if (!prev && !outputs_get_manual_light(PIR_ULAZ_LIGHT_ID)){
      output_set(ChanType::LIGHT, PIR_ULAZ_LIGHT_ID, true, true, OutputOrigin::PIR);
      offAtUlaz = now + PIR_ULAZ_TIMEOUT_MS;
    }
    return;
  }
}

void pir_loop(uint32_t now){
  if (!mcp_outlet_pir_available()) { g_pirInt = false; handleExpiry(now); return; }
  if (outputs_safe_active()){
    handleExpiry(now);
    return;
  }

  if (g_pirInt){
    g_pirInt = false;
    Adafruit_MCP23X17 &dev = MCP_OutletsPIR();
    int pin = dev.getLastInterruptPin();
    if (pin >= 0 && pin <= 15){
      bool level = dev.digitalRead(pin);
      processPirInterrupt((uint8_t)pin, level, now);
    }
  }

  handleExpiry(now);
}

