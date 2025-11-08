#include "rules.h"
#include "config.h"
#include "events.h"

static bool boilerWasOnBeforeBathroom = false;

void rules_begin() {
  outputs_on_change(rules_on_output_change);
}

void rules_on_button(int idx, bool longPress) {
  if (longPress && idx == -1) {
    for (int i = 0; i < 16; ++i) output_set(ChanType::LIGHT, i, false, true, OutputOrigin::Button);
  } else if (idx >= 0 && idx < 16) {
    output_toggle(ChanType::LIGHT, idx, false, OutputOrigin::Button);
  }
}

void rules_on_output_change(ChanType t, int idx, bool on, bool byRule, bool manual, OutputOrigin origin) {
  if (t == ChanType::LIGHT && idx == BATHROOM_LIGHT_ID) {
    if (on) {
      bool boilerOn, present;
      output_get(ChanType::OUTLET, BOILER_OUTLET_ID, boilerOn, present);
      boilerWasOnBeforeBathroom = boilerOn;
      output_set(ChanType::OUTLET, BOILER_OUTLET_ID, false, true, OutputOrigin::Rule);
    } else {
      if (boilerWasOnBeforeBathroom) output_set(ChanType::OUTLET, BOILER_OUTLET_ID, true, true, OutputOrigin::Rule);
    }
  }
}
