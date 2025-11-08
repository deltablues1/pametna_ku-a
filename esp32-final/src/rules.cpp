#include "rules.h"
#include "config.h"
#include "events.h"

static bool boilerWasOnBeforeBathroom=false;

void rules_begin(){
  outputs_on_change([](ChanType t, int idx, bool on, bool byRule, bool manual, OutputOrigin origin) {
    rules_on_output_change(t, idx, on, byRule, manual, origin);
  });
}

void rules_on_button(int idx, bool longPress){
  if (longPress && idx==-1){
    // Emergency: sva svjetla OFF
    for (int i=0;i<16;i++) output_set(ChanType::LIGHT, i, false, true, OutputOrigin::Button);
    return;
  }
  if (idx>=0 && idx<16){
    // Svako tipkalo upravlja svojim svjetlom (0..15)
    output_toggle(ChanType::LIGHT, idx, false, OutputOrigin::Button);
  }
}

void rules_on_output_change(ChanType t, int idx, bool on, bool byRule, bool manual, OutputOrigin origin){
  // Note: Events::logIO is now called directly from outputs.cpp, so we don't duplicate it here

  // Interlock kupaonica ↔ bojler
  if (t==ChanType::LIGHT && idx==BATHROOM_LIGHT_ID){
    bool curBoiler=false, pres=false;
    output_get(ChanType::OUTLET, BOILER_OUTLET_ID, curBoiler, pres);
    if (pres){
      if (on){
        boilerWasOnBeforeBathroom = curBoiler;
        output_set(ChanType::OUTLET, BOILER_OUTLET_ID, false, true, OutputOrigin::Rule);
      } else {
        if (boilerWasOnBeforeBathroom){
          output_set(ChanType::OUTLET, BOILER_OUTLET_ID, true, true, OutputOrigin::Rule);
        }
      }
    }
  }
}