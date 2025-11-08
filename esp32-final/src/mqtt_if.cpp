#include "mqtt_if.h"
#include "config.h"
#include "outputs.h"
#include <WiFi.h>
#include <PubSubClient.h>
#include "events.h"
#include <cstring>
#include <cstdio>

static WiFiClient wifiClient;
static PubSubClient mqtt(wifiClient);
static unsigned long lastMqttAttempt = 0;
const unsigned long MQTT_RETRY_INTERVAL = 30000;

// Normalize base to always end with '/'
static String normalizeBase(const String& b) {
  if (b.length() == 0) return String("");
  return b.endsWith("/") ? b : b + "/";
}

// Centralized topic builders (unified)
static String tState(const char* cls, int i) { return g_mqtt.base + cls + String(i) + "/state"; }
static String tSet(const char* cls, int i)   { return g_mqtt.base + cls + String(i) + "/set"; }
String topicL(int i)    { return tState("light/", i); }
String topicLSet(int i) { return tSet("light/", i); }
String topicO(int i)    { return tState("outlet/", i); }
String topicOSet(int i) { return tSet("outlet/", i); }

// Stack builder to avoid temporary String allocations
static void buildTopic(char* out, size_t outSize, const char* cls, int i, const char* leaf) {
 // Ensures normalized base is used
 std::snprintf(out, outSize, "%s%s%d/%s", g_mqtt.base.c_str(), cls, i, leaf);
}

static void subscribeAll() {
  char buf[128];
  for (int i = 0; i < 16; ++i) {
    buildTopic(buf, sizeof(buf), "light/", i, "set");
    mqtt.subscribe(buf);
    buildTopic(buf, sizeof(buf), "outlet/", i, "set");
    mqtt.subscribe(buf);
  }
}

static void onMsg(char* topic, byte* payload, unsigned int length) {
  // Interpret ON/OFF without constructing temporary Strings
  bool turnOn = (length == 2 && payload[0] == 'O' && payload[1] == 'N');
  char buf[128];
  for (int i = 0; i < 16; ++i) {
    buildTopic(buf, sizeof(buf), "light/", i, "set");
    if (std::strcmp(topic, buf) == 0) {
      output_set(ChanType::LIGHT, i, turnOn, true, OutputOrigin::MQTT);
      return;
    }
    buildTopic(buf, sizeof(buf), "outlet/", i, "set");
    if (std::strcmp(topic, buf) == 0) {
      output_set(ChanType::OUTLET, i, turnOn, true, OutputOrigin::MQTT);
      return;
    }
  }
}

void mqtt_begin() {
  g_mqtt.base = normalizeBase(g_mqtt.base);
  mqtt.setServer(g_mqtt.host.c_str(), g_mqtt.port);
  mqtt.setCallback(onMsg);
  mqtt.setKeepAlive(30);
  mqtt.setSocketTimeout(15);
  mqtt.setBufferSize(512);
  // Subscriptions are done after a successful connect in mqtt_loop()
}

void mqtt_loop() {
  if (!mqtt.connected() && millis() - lastMqttAttempt > MQTT_RETRY_INTERVAL) {
    lastMqttAttempt = millis();

    String clientId = "esp32_home_" + String((uint32_t)ESP.getEfuseMac(), HEX);
    const char* user = g_mqtt.user.length() ? g_mqtt.user.c_str() : nullptr;
    const char* pass = g_mqtt.pass.length() ? g_mqtt.pass.c_str() : nullptr;

    String lwtTopic = g_mqtt.base + "status";
    bool ok = false;
    if (user || pass) {
      ok = mqtt.connect(clientId.c_str(), user, pass, lwtTopic.c_str(), 0, true, "offline");
    } else {
      ok = mqtt.connect(clientId.c_str(), lwtTopic.c_str(), 0, true, "offline");
    }

    if (ok) {
      mqtt.publish(lwtTopic.c_str(), "online", true);
      subscribeAll();
      Events::logNetwork(Events::Type::LINK_CHANGE, Events::Severity::INFO, "MQTT connected");
    } else {
      Events::logNetwork(Events::Type::LINK_CHANGE, Events::Severity::WARN, "MQTT connect failed");
    }
    
    yield(); // Feed watchdog during MQTT connection attempts
  }
  mqtt.loop();
  yield(); // Feed watchdog during MQTT processing
}

void mqtt_pub_state_light(int i, bool on) {
  char buf[128];
  buildTopic(buf, sizeof(buf), "light/", i, "state");
  mqtt.publish(buf, on ? "ON" : "OFF", true);
}

void mqtt_pub_state_outlet(int i, bool on) {
  char buf[128];
  buildTopic(buf, sizeof(buf), "outlet/", i, "state");
  mqtt.publish(buf, on ? "ON" : "OFF", true);
}
