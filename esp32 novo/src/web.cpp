#include "web.h"
#include <ESPAsyncWebServer.h>
#include <AsyncEventSource.h>
#include <LittleFS.h>
#include <ArduinoJson.h>
#include <memory>
#include "network.h"
#include "outputs.h"
#include "config.h"
#include "events.h"
#include "events_extras.h"
#include "hw_mcp.h"

static AsyncWebServer server(80);
static AsyncEventSource sse("/api/events/stream");

void web_begin() {
  if (!LittleFS.begin(false)) {
    Serial.println("[WEB] LittleFS mount FAIL");
    Events::logSimple(Events::Type::ERROR_EVT, Events::Severity::ERROR, "FS mount fail");
    return;
  }
  
  // Static gzipped UI
  auto& h = server.serveStatic("/", LittleFS, "/www/")
                 .setDefaultFile("index.html.gz");
  // Optional cache, but modest:
  h.setCacheControl("max-age=3600");

  if (g_webAuth.enabled) {
    h.setAuthentication(g_webAuth.user.c_str(), g_webAuth.pass.c_str());
    sse.setAuthentication(g_webAuth.user.c_str(), g_webAuth.pass.c_str());
  }
  server.addHandler(&sse);
  Events::onNew([](const Events::Event& e) {
    // Use smaller JSON document for SSE events
    StaticJsonDocument<512> doc;
    Events::toJson(e, doc.to<JsonObject>(), true);
    String json;
    serializeJson(doc, json);
    sse.send(json.c_str(), "event", millis());
    yield(); // Feed watchdog during JSON operations
  });
  server.on("/api/events", HTTP_GET, [](AsyncWebServerRequest* req){
    if (g_webAuth.enabled && !req->authenticate(g_webAuth.user.c_str(), g_webAuth.pass.c_str())) {
      return req->requestAuthentication();
    }
    // Use static JSON document with limited size
    StaticJsonDocument<4096> doc;
    Events::Query q;
    q.limit = 20; // Further reduced limit to save memory
    Events::Summary summary;
    // Use dynamic allocation to avoid stack overflow
    std::unique_ptr<Events::Event[]> events(new Events::Event[20]);
    if (!events) {
      req->send(500, "application/json", "{\"error\":\"Memory allocation failed\"}");
      return;
    }
    size_t count = Events::query(q, events.get(), 20, &summary);
    JsonArray items = doc["items"].to<JsonArray>();
    for (size_t i = 0; i < count && i < 20; i++) {
      JsonObject obj = items.add<JsonObject>();
      Events::toJson(events[i], obj, true);
    }
    JsonObject devices = doc["devices"].to<JsonObject>();
    // For now, we'll just send an empty devices object
    // In a real implementation, this would contain device information
    String json;
    serializeJson(doc, json);
    req->send(200, "application/json", json);
    yield(); // Feed watchdog during heavy JSON operations
  });
  server.on("/api/state", HTTP_GET, [](AsyncWebServerRequest* req){
    if (g_webAuth.enabled && !req->authenticate(g_webAuth.user.c_str(), g_webAuth.pass.c_str())) {
      return req->requestAuthentication();
    }
    // Use static JSON document for state endpoint
    StaticJsonDocument<2048> doc;
    doc["firmware"] = FW_VERSION;
    doc["ip"] = net_ip_str();
    doc["mode"] = net_mode_str();
    doc["hw_connected"] = mcp_lights_available() || mcp_outlet_pir_available() || mcp_buttons_available();
    JsonArray lights = doc["lights"].to<JsonArray>();
    for (int i = 0; i < 16; ++i) {
      bool on, present;
      output_get(ChanType::LIGHT, i, on, present);
      JsonObject l = lights.add<JsonObject>();
      l["present"] = present;
      l["state"] = on;
      l["name"] = light_name(i);
    }
    JsonArray outlets = doc["outlets"].to<JsonArray>();
    for (int i = 0; i < 16; ++i) {
      bool on, present;
      output_get(ChanType::OUTLET, i, on, present);
      JsonObject o = outlets.add<JsonObject>();
      o["present"] = present;
      o["state"] = on;
      o["name"] = outlet_name(i);
    }
    String json;
    serializeJson(doc, json);
    req->send(200, "application/json", json);
    yield(); // Feed watchdog during JSON operations
  });
  server.on("/api/outputs/toggle", HTTP_POST,
    [](AsyncWebServerRequest* req){
      if (g_webAuth.enabled && !req->authenticate(g_webAuth.user.c_str(), g_webAuth.pass.c_str())) {
        return req->requestAuthentication();
      }
      // Response is sent from the onBody callback
    },
    nullptr,
    [](AsyncWebServerRequest* req, uint8_t* data, size_t len, size_t index, size_t total) {
      if (g_webAuth.enabled && !req->authenticate(g_webAuth.user.c_str(), g_webAuth.pass.c_str())) {
        return;
      }
      String* bodyAccum = (String*)req->_tempObject;
      if (!bodyAccum) {
        bodyAccum = new String();
        bodyAccum->reserve(total);
        req->_tempObject = bodyAccum;
      }
      bodyAccum->concat((const char*)data, len);
      if (index + len != total) return;
  JsonDocument doc;
      DeserializationError err = deserializeJson(doc, bodyAccum->c_str());
      delete bodyAccum;
      req->_tempObject = nullptr;
      if (err) {
        req->send(400, "application/json", "{\"error\":true}");
        return;
      }
      String type = doc["type"];
      String idx = doc["index"];
      bool ok = false;
      if (type == "light") {
        ok = output_toggle(ChanType::LIGHT, idx.toInt(), false, OutputOrigin::WebUI);
      } else if (type == "outlet") {
        ok = output_toggle(ChanType::OUTLET, idx.toInt(), false, OutputOrigin::WebUI);
      }
  JsonDocument resp;
      resp["ok"] = ok;
      resp["type"] = type;
      resp["index"] = idx;
      String json;
      serializeJson(resp, json);
      req->send(200, "application/json", json);
    }
  );
  // health endpoint
  server.on("/health", HTTP_GET, [](AsyncWebServerRequest* req){
    req->send(200, "text/plain", "ok");
  });

  server.begin();
  Serial.println("[WEB] started");
  Events::logSimple(Events::Type::INIT, Events::Severity::INFO, "WEB started");
}

void web_loop() {
  static uint32_t lastKeep = 0;
  if (millis() - lastKeep > 20000) {
    lastKeep = millis();
    if (sse.count() > 0) sse.send("", "keep", millis());
  }
  Events::maintain();
}

size_t sse_client_count() { return sse.count(); }
void sse_send_keep(uint32_t nowMs) { sse.send("", "keep", nowMs); }
