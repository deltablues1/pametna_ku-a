#include "network.h"
#include "config.h"
#include "events.h"
#include <WiFi.h>
#include <SPI.h>

#ifdef W5500_ETHERNET
#include <WebServer_ESP32_W5500.h>
#include <AsyncWebServer_ESP32_W5500.h>
#endif

#include "secrets.h"

static bool g_haveIP = false, g_isAP = false;
static String g_mode = "", g_lastIP = "";
static uint32_t g_wifiLastReconnectAttempt = 0;
static uint32_t g_wifiNextDelayMs = 5000;

bool net_begin_eth_w5500_async() {
#ifdef W5500_ETHERNET
  if (!ETH.begin(ETH_W5500_MISO, ETH_W5500_MOSI, ETH_W5500_SCK, ETH_W5500_CS, ETH_W5500_INT)) {
    Events::logNetwork(Events::Type::LINK_CHANGE, Events::Severity::ERROR, "W5500 init fail");
    return false;
  }
  Events::logNetwork(Events::Type::LINK_CHANGE, Events::Severity::INFO, "Ethernet init started (async)");
  return true;
#else
  return false;
#endif
}

bool net_begin_ap() {
  WiFi.mode(WIFI_AP);
  WiFi.softAP(AP_SSID, AP_PASS);
  g_haveIP = true; g_isAP = true; g_mode = "AP";
  g_lastIP = WiFi.softAPIP().toString();
  Events::logNetwork(Events::Type::LINK_CHANGE, Events::Severity::WARN, "AP mode active");
  return true;
}

// Non-blocking STA connect start (no delay/loops)
bool net_begin_wifi_sta_async() {
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  g_haveIP = false;
  g_isAP = false;
  g_mode = "Wi-Fi";
  g_wifiLastReconnectAttempt = millis();
  g_wifiNextDelayMs = 5000;
  Events::logNetwork(Events::Type::LINK_CHANGE, Events::Severity::INFO, "Wi-Fi connect start (async)");
  return true;
}

void net_loop() {
  static uint32_t lastCheck = 0;
  uint32_t now = millis();

  if (now - lastCheck > 1000) {
    lastCheck = now;

#ifdef W5500_ETHERNET
    // Check Ethernet status and update globals if connected
    if (ETH.linkUp() && !g_haveIP) {
      g_haveIP = true;
      g_isAP = false;
      g_mode = "Ethernet";
      g_lastIP = ETH.localIP().toString();
      Events::logNetwork(Events::Type::LINK_CHANGE, Events::Severity::INFO, "Ethernet W5500 up");
      Events::logNetwork(Events::Type::IP_CHANGE, Events::Severity::INFO, "Ethernet IP", g_lastIP);
    }
#endif

    // Wi-Fi connectivity handling + exponential backoff reconnect
    if (WiFi.status() == WL_CONNECTED) {
      String ip = WiFi.localIP().toString();
      if (ip != g_lastIP) {
        g_lastIP = ip;
        g_haveIP = true;
        g_isAP = false;
        g_mode = "Wi-Fi";
        Events::logNetwork(Events::Type::IP_CHANGE, Events::Severity::INFO, "IP changed", ip);
      }
      // Reset backoff after connectivity is restored
      g_wifiNextDelayMs = 5000;
    } else if (!g_isAP) {
      // Non-blocking reconnect with exponential backoff, no delay() or blocking loops
      if (now - g_wifiLastReconnectAttempt >= g_wifiNextDelayMs) {
        g_wifiLastReconnectAttempt = now;
        WiFi.reconnect();
        Events::logNetwork(Events::Type::LINK_CHANGE, Events::Severity::WARN, "Wi-Fi disconnected, reconnecting");
        // Backoff up to 60s
        if (g_wifiNextDelayMs < 60000) g_wifiNextDelayMs = g_wifiNextDelayMs * 2;
        if (g_wifiNextDelayMs > 60000) g_wifiNextDelayMs = 60000;
      }
    }
  }
}

bool net_have_ip() { return g_haveIP; }
String net_ip_str() { return g_lastIP; }
bool net_is_ap() { return g_isAP; }
String net_mode_str() { return g_mode; }
