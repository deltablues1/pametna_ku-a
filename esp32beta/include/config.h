#pragma once
#include <Arduino.h>

// ===== Firmware =====
#define FW_VERSION "2.1.0"

// ===== I2C =====
#define I2C_SDA_PIN   21
#define I2C_SCL_PIN   22
#define I2C_FREQ_HZ   100000     // 100 kHz (može 400 kHz)
#define I2C_TIMEOUTMS 50
#define I2C_RETRIES   3

// ===== WiFi Configuration =====
#define WIFI_SSID "your-ssid"
#define WIFI_PASSWORD "your-password"

// ===== Status LED (active LOW) =====
#define LED_STATUS 2

// ===== MCP23017 adrese =====
#define MCP_LIGHTS_ADDR   0x20  // #1 - Lights
#define MCP_OUTLET_PIR    0x22  // #2 - Outlets + PIR
#define MCP_BUTTONS_ADDR  0x27  // #3 - Buttons

// ===== MCP INT linije (ESP32 GPIO) =====
#define INT_PIR_GPIO     33  // MCP 0x22 INTA (PIR)
#define INT_BTN_A_GPIO   34  // MCP 0x27 INTA (tipke 0..7)
#define INT_BTN_B_GPIO   35  // MCP 0x27 INTB (tipke 8..15)

// ===== Logika kanala =====
static const int BATHROOM_LIGHT_ID = 13; // svjetlo_kupaona
static const int BOILER_OUTLET_ID  = 4;  // uticnica_bojler
static const int PIR_VANI_LIGHT_ID = 0;  // svjetlo_vani
static const int PIR_ULAZ_LIGHT_ID = 3;  // svjetlo_ulaz

// ===== PIR tajmeri =====
#define PIR_VANI_TIMEOUT_MS  (180000UL) // 3 min
#define PIR_ULAZ_TIMEOUT_MS  (120000UL) // 2 min

// ===== Tipkala (INPUT_PULLUP) =====
#define BTN_DEBOUNCE_MS   40
#define BTN_SHORT_MS      1000
#define BTN_LONG_MS       3000

// ===== MQTT (opcija) =====
struct MqttConfig {
  bool enabled = false; // TEMPORARILY DISABLED - set to true when MQTT server is available
  String host = "192.168.100.1";
  uint16_t port = 1883;
  String clientId = ""; // auto generirano
  String base = "home/esp32/esp32_home/";
  String user = "";
  String pass = "";
};
extern MqttConfig g_mqtt;

// ===== Web =====
struct WebAuth {
  bool enabled = false;
  String user = "admin";
  String pass = "admin";
};
extern WebAuth g_webAuth;

// ===== API Auth (token + rate limit) =====
struct ApiAuth {
  bool token_enabled = false;
  String token = "";                // Bearer token when enabled
  uint16_t rate_limit_per_min = 120; // simple global limit
};
extern ApiAuth g_apiAuth;

// ===== OTA =====
struct OtaConfig {
  bool signature_required = false;
  String pubkey_pem = "";
};
extern OtaConfig g_otaCfg;