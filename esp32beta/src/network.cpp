#include "network.h"
#include "config.h"
#include <WiFi.h>
#include <ETH.h>
#include <SPI.h>
#include <ArduinoJson.h>
#include "events.h"

static bool haveIP=false, isAP=false;
static unsigned long lastConnectionAttempt = 0;
static const unsigned long CONNECTION_RETRY_INTERVAL = 30000; // 30 seconds
static int connectionAttempts = 0;
static const int MAX_CONNECTION_ATTEMPTS = 10;
static unsigned long connectionRetryInterval = 5000; // Start with 5 seconds
static bool prevConnected = false;
static String lastIpStr = "";

bool net_begin(){
  Serial.println("=== Network Initialization ===");
  pinMode(LED_STATUS, OUTPUT);
  digitalWrite(LED_STATUS, HIGH); // LED off (active LOW)

  Serial.println("Setting up WiFi station mode...");
  WiFi.mode(WIFI_STA);

  // Use credentials from config.h
  Serial.printf("Attempting to connect to WiFi: %s\n", WIFI_SSID);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  Serial.println("Waiting for WiFi connection with exponential backoff...");
  unsigned long startTime = millis();
  int attempts = 0;
  connectionAttempts = 0;
  connectionRetryInterval = 5000; // Reset to 5 seconds

  while (WiFi.status() != WL_CONNECTED && millis() - startTime < 60000) { // 60 second timeout
    delay(500);
    attempts++;

    switch(WiFi.status()) {
      case WL_NO_SSID_AVAIL:
        Serial.printf("Attempt %d: SSID not available\n", attempts);
        break;
      case WL_CONNECT_FAILED:
        Serial.printf("Attempt %d: Connection failed\n", attempts);
        break;
      case WL_CONNECTION_LOST:
        Serial.printf("Attempt %d: Connection lost\n", attempts);
        break;
      case WL_DISCONNECTED:
        Serial.printf("Attempt %d: Disconnected\n", attempts);
        break;
      case WL_IDLE_STATUS:
        Serial.printf("Attempt %d: Idle status\n", attempts);
        break;
      case WL_SCAN_COMPLETED:
        Serial.printf("Attempt %d: Scan completed\n", attempts);
        break;
      default:
        Serial.printf("Attempt %d: Connecting... (status: %d)\n", attempts, WiFi.status());
        break;
    }

    // Check if we need to retry with exponential backoff
    if (attempts % 6 == 0 && attempts > 0) { // Every 6 attempts (30 seconds)
      connectionAttempts++;
      Serial.printf("Connection attempt %d failed, retrying with %d second delay...\n",
                   connectionAttempts, connectionRetryInterval / 1000);

      if (connectionAttempts < MAX_CONNECTION_ATTEMPTS) {
        Serial.printf("Waiting %d seconds before next attempt...\n", connectionRetryInterval / 1000);
        delay(connectionRetryInterval);

        // Exponential backoff: double the interval, max 60 seconds
        connectionRetryInterval = min(connectionRetryInterval * 2, 60000UL);
        Serial.println("Retrying WiFi connection...");
        WiFi.reconnect();
      } else {
        Serial.printf("Maximum connection attempts (%d) reached, giving up\n", MAX_CONNECTION_ATTEMPTS);
        break;
      }
    }
  }

  if (WiFi.status() == WL_CONNECTED) {
    haveIP = true;
    isAP = false;
    Serial.println("\n=== WiFi Connected Successfully! ===");
    Serial.printf("IP Address: %s\n", WiFi.localIP().toString().c_str());
    Serial.printf("Subnet Mask: %s\n", WiFi.subnetMask().toString().c_str());
    Serial.printf("Gateway: %s\n", WiFi.gatewayIP().toString().c_str());
    Serial.printf("DNS Server: %s\n", WiFi.dnsIP().toString().c_str());
    Serial.printf("MAC Address: %s\n", WiFi.macAddress().c_str());
    Serial.printf("Signal Strength: %d dBm\n", WiFi.RSSI());
    Serial.println("=====================================");

    // Events: link up + IP info
    lastIpStr = WiFi.localIP().toString();
    Events::logNetwork(Events::Type::LINK_CHANGE, Events::Severity::INFO, "Wi-Fi povezan");
    {
      JsonDocument meta;
      meta["ip"] = WiFi.localIP().toString();
      meta["subnet"] = WiFi.subnetMask().toString();
      meta["gateway"] = WiFi.gatewayIP().toString();
      meta["mac"] = WiFi.macAddress();
      meta["iface"] = "Wi-Fi";
      meta["method"] = "Wi‑Fi";
      String m; serializeJson(meta, m);
      Events::logNetwork(Events::Type::IP_CHANGE, Events::Severity::INFO, "Dodijeljena IP adresa", m);
    }
    prevConnected = true;
  } else {
    Serial.println("\n=== WiFi Connection Failed ===");
    Serial.printf("Final status: %d\n", WiFi.status());
    Serial.println("Switching to Access Point mode...");

    // AP fallback with retry
    WiFi.mode(WIFI_AP);
    delay(1000); // Give WiFi time to switch modes

    if (WiFi.softAP("ESP32-SMART-HOME", "12345678")) {
      haveIP = true;
      isAP = true;
      Serial.println("=== Access Point Mode Active ===");
      Serial.printf("AP IP Address: %s\n", WiFi.softAPIP().toString().c_str());
      Serial.println("SSID: ESP32-SMART-HOME");
      Serial.println("Password: 12345678");
      Serial.println("Connect to this AP to access web interface");
      Serial.println("=================================");

      // Events: AP mode link up + IP info
      lastIpStr = WiFi.softAPIP().toString();
      Events::logNetwork(Events::Type::LINK_CHANGE, Events::Severity::WARN, "AP način aktivan");
      {
        JsonDocument meta;
        meta["ip"] = WiFi.softAPIP().toString();
        meta["subnet"] = "255.255.255.0";
        meta["gateway"] = "0.0.0.0";
        meta["mac"] = WiFi.softAPmacAddress();
        meta["iface"] = "AP";
        meta["method"] = "AP";
        String m; serializeJson(meta, m);
        Events::logNetwork(Events::Type::IP_CHANGE, Events::Severity::INFO, "AP IP adresa", m);
      }
      prevConnected = true;
    } else {
      haveIP = false;
      isAP = false;
      Serial.println("Failed to start Access Point mode!");
    }
  }

  digitalWrite(LED_STATUS, haveIP ? LOW : HIGH); // ON if we have IP
  lastConnectionAttempt = millis();
  return haveIP;
}

void net_loop(){
  static unsigned long lastCheck = 0;
  unsigned long now = millis();

  // Check network status every 5 seconds
  if (now - lastCheck > 5000) {
    lastCheck = now;

    bool connectedNow = isAP || (WiFi.status() == WL_CONNECTED);

    // Link state transitions
    if (connectedNow && !prevConnected) {
      Events::logNetwork(Events::Type::LINK_CHANGE, Events::Severity::INFO, isAP ? "AP aktivan" : "Wi-Fi povezan");
    } else if (!connectedNow && prevConnected) {
      Events::logNetwork(Events::Type::LINK_CHANGE, Events::Severity::WARN, "Veza izgubljena");
    }

    // IP change detection
    String curIp = isAP ? WiFi.softAPIP().toString() : (WiFi.status() == WL_CONNECTED ? WiFi.localIP().toString() : String());
    if (curIp.length() && curIp != lastIpStr) {
      lastIpStr = curIp;
      JsonDocument meta;
      meta["ip"] = curIp;
      meta["iface"] = isAP ? "AP" : "Wi-Fi";
      meta["gateway"] = isAP ? "0.0.0.0" : WiFi.gatewayIP().toString();
      meta["subnet"] = isAP ? "255.255.255.0" : WiFi.subnetMask().toString();
      meta["mac"] = isAP ? WiFi.softAPmacAddress() : WiFi.macAddress();
      String m; serializeJson(meta, m);
      Events::logNetwork(Events::Type::IP_CHANGE, Events::Severity::INFO, "Promjena IP adrese", m);
    }

    if (!isAP && WiFi.status() != WL_CONNECTED) {
      // WiFi disconnected, try to reconnect with exponential backoff
      if (now - lastConnectionAttempt > connectionRetryInterval) {
        Serial.printf("WiFi connection lost, attempting to reconnect (attempt %d/%d)...\n",
                     connectionAttempts + 1, MAX_CONNECTION_ATTEMPTS);
        Serial.printf("Waiting %d seconds before reconnect attempt...\n", connectionRetryInterval / 1000);
        delay(1000); // Give some time before reconnect
        WiFi.reconnect();
        lastConnectionAttempt = now;

        // Exponential backoff for reconnection attempts
        connectionAttempts++;
        if (connectionAttempts < MAX_CONNECTION_ATTEMPTS) {
          connectionRetryInterval = min(connectionRetryInterval * 2, 60000UL);
        } else {
          Serial.printf("Maximum reconnection attempts (%d) reached\n", MAX_CONNECTION_ATTEMPTS);
          // Reset for next cycle
          connectionAttempts = 0;
          connectionRetryInterval = 5000;
        }
      } else {
        // Show countdown to next attempt
        unsigned long remaining = (lastConnectionAttempt + connectionRetryInterval - now) / 1000;
        if (remaining % 10 == 0 && remaining > 0) { // Log every 10 seconds
          Serial.printf("Next reconnection attempt in %d seconds...\n", remaining);
        }
      }
    } else if (WiFi.status() == WL_CONNECTED) {
      // Reset connection attempts on successful connection
      if (connectionAttempts > 0) {
        Serial.println("WiFi reconnected successfully, resetting retry counters");
        connectionAttempts = 0;
        connectionRetryInterval = 5000;
      }

      // Log current status
      static int lastRSSI = 0;
      int currentRSSI = WiFi.RSSI();
      if (abs(currentRSSI - lastRSSI) > 5) { // Only log if RSSI changed significantly
        Serial.printf("WiFi Status - IP: %s, RSSI: %d dBm\n",
                     WiFi.localIP().toString().c_str(), currentRSSI);
        lastRSSI = currentRSSI;
      }
    }

    // Log current status
    if (isAP) {
      Serial.printf("AP Mode - IP: %s, Stations connected: %d\n",
                   WiFi.softAPIP().toString().c_str(), WiFi.softAPgetStationNum());
    } else if (WiFi.status() != WL_CONNECTED) {
      Serial.printf("WiFi Status: %d (Disconnected)\n", WiFi.status());
    }
    prevConnected = connectedNow;
  }
}

bool net_have_ip(){ return haveIP; }
bool net_is_ap(){ return isAP; }
String net_ip_str(){
  if (isAP) {
    return WiFi.softAPIP().toString();
  }
  if (WiFi.status() == WL_CONNECTED) {
    return WiFi.localIP().toString();
  }
  return String();
}