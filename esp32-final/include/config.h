#pragma once
#include <Arduino.h>

#define FW_VERSION "4.0.0-final"

// I2C config - OPTIMIZED: 400kHz for fast button response
#define I2C_SDA_PIN 21
#define I2C_SCL_PIN 22
#define I2C_FREQ_HZ 400000  // 400kHz (4x faster than 100kHz)
#define I2C_TIMEOUTMS 5     // 5ms timeout (reduced from 50ms)

// Status LED (active LOW)
#define LED_STATUS 2

// MCP23017 addresses
#define MCP_LIGHTS_ADDR 0x20
#define MCP_OUTLET_PIR 0x22
#define MCP_BUTTONS_ADDR 0x27

// MCP interrupt pins
#define INT_PIR_GPIO 33
#define INT_BTN_A_GPIO 34
#define INT_BTN_B_GPIO 35

// W5500 Ethernet pins (VSPI, 3.3V)
#ifndef ETH_W5500_CS
#define ETH_W5500_CS 5
#endif
#ifndef ETH_W5500_INT
#define ETH_W5500_INT -1  // Disable INT to avoid ISR service conflict (use polling)
#endif
#ifndef ETH_W5500_RST
#define ETH_W5500_RST 4
#endif
#ifndef ETH_W5500_MISO
#define ETH_W5500_MISO 19
#endif
#ifndef ETH_W5500_MOSI
#define ETH_W5500_MOSI 23
#endif
#ifndef ETH_W5500_SCK
#define ETH_W5500_SCK 18
#endif
#define ETH_LINK_TIMEOUT_MS 8000

// Wi-Fi config - credentials are in secrets.h
#define WIFI_CONNECT_TIMEOUT_MS 8000
#define AP_SSID "ESP32-Setup"
#define AP_PASS "12345678"

// Network FSM timeouts
#define ETH_FSM_TIMEOUT_MS 5000   // 5s za Ethernet pokušaj
#define WIFI_FSM_TIMEOUT_MS 60000 // 60s za WiFi pokušaj

// PIR timeouts
#define PIR_VANI_TIMEOUT_MS 180000
#define PIR_ULAZ_TIMEOUT_MS 120000

// Button debounce - OPTIMIZED
#define BTN_DEBOUNCE_MS 40
#define BTN_SHORT_MS 1000
#define BTN_LONG_MS 3000

// Logic constants
#define BATHROOM_LIGHT_ID 13
#define BOILER_OUTLET_ID 4
#define PIR_VANI_LIGHT_ID 0
#define PIR_ULAZ_LIGHT_ID 3

// MQTT config struct
struct MqttConfig {
    bool enabled = false;
    String host;
    int port = 1883;
    String base = "home/esp32/";
    String user;
    String pass;
};
extern MqttConfig g_mqtt;

// Web auth struct
struct WebAuth {
    bool enabled = false;
    String user = "admin";
    String pass = "admin";
};
extern WebAuth g_webAuth;

// Safe-restore config
#define SAFE_RESTORE_INTERVAL_MS 1000
