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
  // Status LED setup (active LOW)
  pinMode(LED_STATUS, OUTPUT);
  digitalWrite(LED_STATUS, HIGH); // LED off initially

  // Serial init
  Serial.begin(115200);
  Serial.println("\n=== ESP32 Smart Home Controller v4.0 FINAL ===");
  Serial.printf("Firmware: %s\n", FW_VERSION);
  Serial.println("Optimized: Fast I2C (400kHz), Fast buttons, Stable web");
  delay(100);

  // LED blink to indicate boot
  digitalWrite(LED_STATUS, LOW);  // LED on
  delay(100);
  digitalWrite(LED_STATUS, HIGH); // LED off

  // 1) HARDWARE
  Serial.println("[BOOT] Hardware initialization...");

  // Events system with large buffer for stability
  Events::begin(/*capacity=*/256, /*retentionDays=*/3);
  Events::logSimple(Events::Type::INIT, Events::Severity::INFO, "System boot");

  bool hw_ok = true;

  // Persistent storage
  if (!persist_begin()) {
    hw_ok = false;
    Serial.println("[HW] NVS init failed");
    Events::logBasic(Events::Type::INIT, Events::Severity::ERROR, F("SYS"), F("NVS init fail"));
  } else {
    Serial.println("[HW] NVS OK");
  }

  // I2C - OPTIMIZED to 400kHz
  Serial.printf("[I2C] Initializing at %d Hz\n", I2C_FREQ_HZ);
  Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN);
  Wire.setClock(I2C_FREQ_HZ);
  delay(50);

  // MCP expanders
  Serial.println("[MCP] Initializing expanders...");
  mcp_begin_all();

  // Check MCP availability
  Serial.printf("[MCP] Lights (0x%02X): %s\n", MCP_LIGHTS_ADDR,
                mcp_lights_available() ? "OK" : "MISSING");
  Serial.printf("[MCP] Outlets/PIR (0x%02X): %s\n", MCP_OUTLET_PIR,
                mcp_outlet_pir_available() ? "OK" : "MISSING");
  Serial.printf("[MCP] Buttons (0x%02X): %s\n", MCP_BUTTONS_ADDR,
                mcp_buttons_available() ? "OK" : "MISSING");

  if (!mcp_lights_available() && !mcp_outlet_pir_available() && !mcp_buttons_available()) {
    hw_ok = false;
    Events::logBasic(Events::Type::ERROR_EVT, Events::Severity::ERROR, F("SYS"), F("MCP init fail"));
  }

  // 2) NETWORK - Initialize FIRST to install ISR service!
  // W5500 will install ISR service, then PIR and buttons can use it
  Serial.println("[NET] Initializing network (Ethernet + WiFi fallback)...");
  netfsm_begin();
  delay(500); // Give Ethernet time to fully initialize and install ISR

  // Outputs, PIR, buttons - after network
  Serial.println("[HW] Initializing outputs...");
  outputs_begin();

  Serial.println("[HW] Initializing PIR sensors...");
  pir_begin();

  Serial.println("[HW] Initializing buttons (fast mode)...");
  buttons_begin(onBtn);

  if (hw_ok) {
    Serial.println("[HW] Hardware initialization: OK");
    Events::logBasic(Events::Type::INIT, Events::Severity::INFO, F("SYS"), F("HW OK"));
    // LED blink to indicate success
    for (int i = 0; i < 3; i++) {
      digitalWrite(LED_STATUS, LOW);
      delay(50);
      digitalWrite(LED_STATUS, HIGH);
      delay(50);
    }
  } else {
    Serial.println("[HW] Hardware initialization: PARTIAL/ERROR");
    Events::logBasic(Events::Type::WARNING_EVT, Events::Severity::WARN, F("SYS"), F("HW partial"));
    // Long LED blink to indicate error
    digitalWrite(LED_STATUS, LOW);
    delay(500);
    digitalWrite(LED_STATUS, HIGH);
  }

  // 3) WEB SERVER
  Serial.println("[WEB] Starting web server (optimized)...");
  web_begin();

  // 4) MQTT (optional)
  if (g_mqtt.enabled) {
    Serial.println("[MQTT] Initializing MQTT...");
  } else {
    Serial.println("[MQTT] Disabled");
  }

  Serial.printf("\n[SYSTEM] Free heap: %u bytes, Min free: %u bytes\n",
                ESP.getFreeHeap(), ESP.getMinFreeHeap());
  Serial.println("=== System Ready ===");
  Serial.println("Web interface:");
  Serial.println("  - Ethernet: http://<eth-ip>");
  Serial.println("  - WiFi: http://<wifi-ip>");
  Serial.println("  - AP fallback: http://192.168.4.1");
  Serial.println("===================\n");

  Events::logSimple(Events::Type::INIT, Events::Severity::INFO, "System ready");
}

void loop() {
  uint32_t now = millis();

  // Fast button processing (optimized I2C @ 400kHz)
  buttons_loop(now);

  // Output management
  outputs_loop(now);

  // PIR sensors
  pir_loop(now);

  // Network FSM (handles Ethernet + WiFi)
  netfsm_loop();

  // Web server maintenance
  web_loop();

  // MQTT (if enabled)
  if (g_mqtt.enabled) mqtt_loop();

  // LED heartbeat (every 5 seconds, no event logging to save memory)
  static uint32_t lastBlink = 0;
  if (now - lastBlink > 5000) {
    digitalWrite(LED_STATUS, LOW);
    delay(10);
    digitalWrite(LED_STATUS, HIGH);
    lastBlink = now;
  }

  yield();
  delay(5); // Small delay for stability, faster than 10ms
}
