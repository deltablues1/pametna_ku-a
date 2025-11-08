#include <Arduino.h>
#include "config.h"
#include "outputs.h"
#include "hw_mcp.h"
#include "buttons.h"
#include "rules.h"
#include "pir.h"
#include "network.h"
#include "web.h"
#include "mqtt_if.h"
#include "persist.h"
#include "events.h"

extern MqttConfig g_mqtt;
extern WebAuth    g_webAuth;
extern OtaConfig  g_otaCfg;

static void onBtn(int idx, bool longP){ rules_on_button(idx, longP); }

void setup(){
  Serial.begin(115200);
  Serial.println("=== ESP32 Smart Home Controller Starting ===");
  Serial.printf("Firmware Version: %s\n", FW_VERSION);
  delay(200);

  pinMode(LED_STATUS, OUTPUT);
  digitalWrite(LED_STATUS, HIGH); // LED off initially

  Serial.println("Initializing persistent storage...");
  if (!persist_begin()){ Serial.println("[PERSIST] WARNING: Preferences init failed; states will not persist across reboots"); }

  Serial.println("Initializing outputs...");
  outputs_begin();
  Serial.printf("[MCP] Lights expander: %s\n", mcp_lights_available() ? "OK" : "MISSING (virtual mode)");
  Serial.printf("[MCP] Outlets/PIR expander: %s\n", mcp_outlet_pir_available() ? "OK" : "MISSING (virtual mode)");
  Serial.printf("[MCP] Buttons expander: %s\n", mcp_buttons_available() ? "OK" : "MISSING (virtual mode)");

  Serial.println("Loading output states from NVS...");
  outputs_load_from_nvs();
 
  constexpr uint16_t EVENTS_BUFFER_CAP = 128;

  // Inicijalizacija sustava doga??aja
  Serial.printf("Initializing event buffer (%u entries)\n", EVENTS_BUFFER_CAP);
  Events::begin(EVENTS_BUFFER_CAP, 30);
  Events::logBasic(Events::Type::INIT, Events::Severity::INFO, "controller", "Inicijalizacija sustava", "system");
 
  // Log initial output states
  Serial.println("=== Initial Output States ===");
  for(int i = 0; i < 16; i++) {
    bool on, present;
    if (output_get(ChanType::LIGHT, i, on, present) && present) {
      Serial.printf("Light %d (%s): %s\n", i, light_name(i), on ? "ON" : "OFF");
    }
    if (output_get(ChanType::OUTLET, i, on, present) && present) {
      Serial.printf("Outlet %d (%s): %s\n", i, outlet_name(i), on ? "ON" : "OFF");
    }
  }
  Serial.println("============================");

  Serial.println("Initializing rules engine...");
  rules_begin();

  Serial.println("Initializing button handler...");
  buttons_begin(onBtn);

  Serial.println("Initializing PIR sensors...");
  pir_begin();

  Serial.println("Initializing network...");
  if (!net_begin()){
    Serial.println("Network init failed - running in AP mode");
  }

  Serial.println("Starting web server...");
  web_begin();

  if (g_mqtt.enabled) {
    Serial.println("Initializing MQTT...");
    mqtt_begin();
  }

  Serial.println("=== System Ready ===");
  Serial.println("Web interface available at:");
  Serial.printf("  WiFi mode: http://%s\n", net_ip_str().c_str());
  Serial.printf("  AP mode: http://192.168.4.1\n");
  Serial.println("====================");
}

void loop(){
  uint32_t now = millis();
  buttons_loop(now);
  // Safe-start and deferred output operations
  outputs_loop(now);
  // PIR processing (gated during safe-start inside pir_loop)
  pir_loop(now);
  net_loop();
  web_loop();
  // Heartbeat svaka 10s
  static uint32_t lastHb = 0;
  if (now - lastHb > 10000) {
    Events::logBasic(Events::Type::HEARTBEAT, Events::Severity::INFO, "controller", "heartbeat", "system");
    lastHb = now;
  }
  if (g_mqtt.enabled) mqtt_loop();
  delay(2);
}
