#pragma once
#include <Arduino.h>

enum class NetInitState : uint8_t {
  HW_INIT,
  ETH_TRY,
  WIFI_TRY,
  AP_ON,
  DONE
};

void netfsm_begin();      // call from setup after HW initialization
void netfsm_loop();       // call from loop
NetInitState netfsm_state();