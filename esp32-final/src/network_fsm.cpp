#include "network_fsm.h"
#include "network.h"
#include "events_extras.h"
#include "config.h"
#include "mqtt_if.h"

static NetInitState st = NetInitState::HW_INIT;
static uint32_t tStart = 0;

static constexpr uint32_t ETH_TIMEOUT_MS  = ETH_FSM_TIMEOUT_MS;
static constexpr uint32_t WIFI_TIMEOUT_MS = WIFI_FSM_TIMEOUT_MS;

void netfsm_begin() {
  // TEMPORARY: Skip Ethernet due to ISR service conflict, go straight to WiFi
  Serial.println(F("[NET] Skipping Ethernet (ISR conflict), using WiFi..."));
  st = NetInitState::WIFI_TRY;
  tStart = millis();
  Events::logSimple(Events::Type::INIT, Events::Severity::WARN, "NetFSM: Skip ETH, WiFi only");
  net_begin_wifi_sta_async(); // non-blocking

  // Original code (commented out temporarily):
  // st = NetInitState::ETH_TRY;
  // tStart = millis();
  // Events::logSimple(Events::Type::INIT, Events::Severity::INFO, "NetFSM: ETH_TRY");
  // Serial.println(F("[NET] Try Ethernet (5s)"));
  // net_begin_eth_w5500_async(); // non-blocking
}

void netfsm_loop() {
  net_loop(); // existing network loop

  switch (st) {
    case NetInitState::ETH_TRY: {
      if (net_have_ip()) {
        Events::logSimple(Events::Type::LINK_CHANGE, Events::Severity::INFO, "Ethernet up");
        Serial.printf("[NET] Ethernet up: %s\n", net_ip_str().c_str());
        st = NetInitState::DONE;
        // Pokreni MQTT ako je omogućen
        if (g_mqtt.enabled) mqtt_begin();
        break;
      }
      if (millis() - tStart > ETH_TIMEOUT_MS) {
        Events::logSimple(Events::Type::WARNING_EVT, Events::Severity::WARN, "Ethernet timeout");
        Serial.println(F("[NET] Ethernet timeout, trying WiFi (60s)"));
        st = NetInitState::WIFI_TRY;
        tStart = millis();
        net_begin_wifi_sta_async(); // non-blocking
      }
    } break;

    case NetInitState::WIFI_TRY: {
      if (net_have_ip()) {
        Events::logSimple(Events::Type::LINK_CHANGE, Events::Severity::INFO, "WiFi up");
        Serial.printf("[NET] WiFi up: %s\n", net_ip_str().c_str());
        st = NetInitState::DONE;
        // Pokreni MQTT ako je omogućen
        if (g_mqtt.enabled) mqtt_begin();
        break;
      }
      if (millis() - tStart > WIFI_TIMEOUT_MS) {
        Events::logSimple(Events::Type::WARNING_EVT, Events::Severity::WARN, "WiFi timeout");
        Serial.println(F("[NET] WiFi timeout, enabling AP"));
        st = NetInitState::AP_ON;
        net_begin_ap(); // set up AP
      }
    } break;

    case NetInitState::AP_ON: {
      if (net_is_ap()) {
        Events::logSimple(Events::Type::LINK_CHANGE, Events::Severity::INFO, "AP enabled");
        Serial.printf("[NET] AP mode: %s\n", net_ip_str().c_str());
        st = NetInitState::DONE;
      }
    } break;

    case NetInitState::DONE:
    case NetInitState::HW_INIT:
    default:
      break;
  }
}

NetInitState netfsm_state() { return st; }