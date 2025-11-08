#include "mqtt_if.h"
#include "config.h"
#include "outputs.h"
#include <WiFi.h>
#include <ETH.h>
#include <PubSubClient.h>

MqttConfig g_mqtt;

static WiFiClient wifiClient;
static PubSubClient mqtt(wifiClient);
static unsigned long lastMqttAttempt = 0;
static const unsigned long MQTT_RETRY_INTERVAL = 30000; // 30 seconds

static String topicL(int i){ return g_mqtt.base + "light/" + String(i) + "/state"; }
static String topicLSet(int i){ return g_mqtt.base + "light/" + String(i) + "/set"; }
static String topicO(int i){ return g_mqtt.base + "outlet/" + String(i) + "/state"; }
static String topicOSet(int i){ return g_mqtt.base + "outlet/" + String(i) + "/set"; }

static void onMsg(char* t, byte* p, unsigned int l){
  String tp(t); String pl; pl.reserve(l);
  for (unsigned i=0;i<l;i++) pl += char(p[i]);
  bool on = (pl=="ON" || pl=="on" || pl=="1");
  if (tp.indexOf("/light/")>0 && tp.endsWith("/set")){
    int idx = tp.substring(tp.indexOf("/light/")+7).toInt();
    output_set(ChanType::LIGHT, idx, on, true, OutputOrigin::MQTT);
  } else if (tp.indexOf("/outlet/")>0 && tp.endsWith("/set")){
    int idx = tp.substring(tp.indexOf("/outlet/")+8).toInt();
    output_set(ChanType::OUTLET, idx, on, true, OutputOrigin::MQTT);
  }
}

void mqtt_begin(){
  if (g_mqtt.clientId==""){ g_mqtt.clientId = "esp32_home_" + String((uint32_t)ESP.getEfuseMac(), HEX); }
  mqtt.setServer(g_mqtt.host.c_str(), g_mqtt.port);
  mqtt.setCallback(onMsg);
  for (int i=0;i<16;i++){ mqtt.subscribe(topicLSet(i).c_str()); mqtt.subscribe(topicOSet(i).c_str()); }
}

static void ensureConn(){
  if (!mqtt.connected()){
    unsigned long now = millis();
    if (now - lastMqttAttempt > MQTT_RETRY_INTERVAL) {
      Serial.printf("[MQTT] Attempting connection to %s:%d\n", g_mqtt.host.c_str(), g_mqtt.port);
      bool success;
      if (g_mqtt.user.length())
        success = mqtt.connect(g_mqtt.clientId.c_str(), g_mqtt.user.c_str(), g_mqtt.pass.c_str());
      else
        success = mqtt.connect(g_mqtt.clientId.c_str());

      if (success) {
        Serial.println("[MQTT] Connected successfully");
        lastMqttAttempt = 0; // Reset retry timer on success
      } else {
        Serial.printf("[MQTT] Connection failed, state: %d. Retrying in %d seconds...\n",
                     mqtt.state(), MQTT_RETRY_INTERVAL / 1000);
        lastMqttAttempt = now;
      }
    }
  }
}

void mqtt_loop(){ ensureConn(); mqtt.loop(); }
void mqtt_pub_state_light(int i, bool on){ mqtt.publish(topicL(i).c_str(), on?"ON":"OFF", true); }
void mqtt_pub_state_outlet(int i, bool on){ mqtt.publish(topicO(i).c_str(), on?"ON":"OFF", true); }