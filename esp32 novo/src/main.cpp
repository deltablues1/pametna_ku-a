#include "config.h"
#include "outputs.h"
#include "hw_mcp.h"
#include "buttons.h"
#include "rules.h"
#include "pir.h"
#include "network.h"
#include "network_fsm.h"
#include "web.h"
#include "mqtt_if.h"
#include "persist.h"
#include "events.h"
#include "events_extras.h"
#include <Arduino.h>
#include <Wire.h>
#include <esp_system.h>

MqttConfig g_mqtt;
WebAuth g_webAuth;
static void onBtn(int idx, bool longP) { rules_on_button(idx, longP); }

void setup() {
  // 1) HARDWARE
  Serial.begin(115200);
  Serial.println("\n[BOOT] HW init start");
  Events::begin(/*capacity=*/256, /*retentionDays=*/3);

  bool hw_ok = true;
  if (!persist_begin()) {
    hw_ok = false;
    Serial.println("[HW] NVS init failed");
    Events::logBasic(Events::Type::INIT, Events::Severity::ERROR, F("SYS"), F("NVS init fail"));
  }

  // I2C
  Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN);
  Wire.setClock(I2C_FREQ_HZ);
  delay(50);

  // MCP, outputs, buttons, PIR
  mcp_begin_all();
  // Check if at least one MCP is available
  if (!mcp_lights_available() && !mcp_outlet_pir_available() && !mcp_buttons_available()) {
    hw_ok = false;
    Events::logBasic(Events::Type::ERROR_EVT, Events::Severity::ERROR, F("SYS"), F("MCP init fail"));
  }
  outputs_begin();
  buttons_begin(onBtn);
  pir_begin();

  if (hw_ok) {
    Serial.println("[HW] OK");
    Events::logBasic(Events::Type::INIT, Events::Severity::INFO, F("SYS"), F("HW OK"));
  } else {
    Serial.println("[HW] PARTIAL/ERROR");
    Events::logBasic(Events::Type::WARNING_EVT, Events::Severity::WARN, F("SYS"), F("HW partial/error"));
  }

  // 2-4) NETWORK (FSM)
  netfsm_begin();

  // WEB server can be started immediately (listens on 0.0.0.0) or after IP - recommendation: immediately.
  web_begin();

  Serial.printf("Free heap: %u, Min free: %u\n", ESP.getFreeHeap(), ESP.getMinFreeHeap());
}

void loop() {
  uint32_t now = millis();
  buttons_loop(now);
  outputs_loop(now);
  pir_loop(now);

  netfsm_loop();  // includes net_loop()

  web_loop();
  if (g_mqtt.enabled) mqtt_loop();

  yield();
  delay(5);
}
