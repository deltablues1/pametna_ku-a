#include "pir.h"
#include "io_map.h"
#include "hw_mcp.h"
#include "config.h"
#include "outputs.h"
#include "events.h"

static volatile bool g_pirInt = false;
static uint32_t offAtVani = 0, offAtUlaz = 0;
static bool stateVani = false, stateUlaz = false;
static bool validVani = false, validUlaz = false;

void IRAM_ATTR isrPIR() { g_pirInt = true; }

void pir_begin() {
  if (!mcp_outlet_pir_available()) return;
  attachInterrupt(digitalPinToInterrupt(INT_PIR_GPIO), isrPIR, FALLING);
  stateVani = mcp_read(OUTLET_MCP[PIR_VANI_IDX].pin);
  stateUlaz = mcp_read(OUTLET_MCP[PIR_ULAZ_IDX].pin);
  validVani = !stateVani;
  validUlaz = !stateUlaz;
  if (!validVani) Events::logBasic(Events::Type::WARNING_EVT, Events::Severity::WARN, "pir", "PIR vani not detected");
  if (!validUlaz) Events::logBasic(Events::Type::WARNING_EVT, Events::Severity::WARN, "pir", "PIR ulaz not detected");
}

void pir_loop(uint32_t nowMs) {
  if (outputs_safe_active()) return;
  if (g_pirInt) {
    g_pirInt = false;
    int pin = MCP_OutletsPIR().getLastInterruptPin();
    bool level = MCP_OutletsPIR().digitalRead(pin);
    if (pin == OUTLET_MCP[PIR_VANI_IDX].pin.pin) {
      if (!level) validVani = true;
      else if (validVani) {
        output_set(ChanType::LIGHT, PIR_VANI_LIGHT_ID, true, true, OutputOrigin::PIR);
        offAtVani = nowMs + PIR_VANI_TIMEOUT_MS;
      }
    } else if (pin == OUTLET_MCP[PIR_ULAZ_IDX].pin.pin) {
      if (!level) validUlaz = true;
      else if (validUlaz) {
        output_set(ChanType::LIGHT, PIR_ULAZ_LIGHT_ID, true, true, OutputOrigin::PIR);
        offAtUlaz = nowMs + PIR_ULAZ_TIMEOUT_MS;
      }
    }
  }
  if (offAtVani && nowMs > offAtVani) {
    output_set(ChanType::LIGHT, PIR_VANI_LIGHT_ID, false, true, OutputOrigin::PIR);
    offAtVani = 0;
  }
  if (offAtUlaz && nowMs > offAtUlaz) {
    output_set(ChanType::LIGHT, PIR_ULAZ_LIGHT_ID, false, true, OutputOrigin::PIR);
    offAtUlaz = 0;
  }
}

void pir_cancel_timer_for_light(int lightIdx) {
  if (lightIdx == PIR_VANI_LIGHT_ID) offAtVani = 0;
  if (lightIdx == PIR_ULAZ_LIGHT_ID) offAtUlaz = 0;
}
void pir_start_full_timer_for_light(int lightIdx, uint32_t nowMs) {
  if (lightIdx == PIR_VANI_LIGHT_ID) offAtVani = nowMs + PIR_VANI_TIMEOUT_MS;
  if (lightIdx == PIR_ULAZ_LIGHT_ID) offAtUlaz = nowMs + PIR_ULAZ_TIMEOUT_MS;
}
