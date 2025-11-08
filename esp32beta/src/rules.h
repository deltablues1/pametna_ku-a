#pragma once
#include <Arduino.h>
#include "outputs.h"
void rules_begin();
void rules_on_button(int idx, bool longPress);
void rules_on_output_change(ChanType t, int idx, bool on, bool byRule, bool manual, OutputOrigin origin);