#include "persist.h"
#include <Preferences.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

static Preferences prefs;
static bool g_ready = false;
static SemaphoreHandle_t s_prefsMutex = nullptr;

bool persist_begin() {
  if (!s_prefsMutex) s_prefsMutex = xSemaphoreCreateMutex();
  g_ready = prefs.begin("esp32home", false);
  return g_ready;
}

bool persist_is_ready() { return g_ready; }

bool persist_get(const String& key, String& out) {
  if (!g_ready || !s_prefsMutex) return false;
  if (xSemaphoreTake(s_prefsMutex, pdMS_TO_TICKS(50)) != pdTRUE) return false;
  // Use String() default to bind the correct overload: getString(const char*, String)
  out = prefs.getString(key.c_str(), String());
  xSemaphoreGive(s_prefsMutex);
  return out.length() > 0;
}

bool persist_set(const String& key, const String& val) {
  if (!g_ready || !s_prefsMutex) return false;
  if (xSemaphoreTake(s_prefsMutex, pdMS_TO_TICKS(50)) != pdTRUE) return false;
  prefs.putString(key.c_str(), val);
  xSemaphoreGive(s_prefsMutex);
  return true;
}

bool persist_get_bool(const String& key, bool& out) {
  if (!g_ready || !s_prefsMutex) return false;
  if (xSemaphoreTake(s_prefsMutex, pdMS_TO_TICKS(50)) != pdTRUE) return false;
  out = prefs.getBool(key.c_str(), false);
  xSemaphoreGive(s_prefsMutex);
  return true;
}

bool persist_set_bool(const String& key, bool val) {
  if (!g_ready || !s_prefsMutex) return false;
  if (xSemaphoreTake(s_prefsMutex, pdMS_TO_TICKS(50)) != pdTRUE) return false;
  prefs.putBool(key.c_str(), val);
  xSemaphoreGive(s_prefsMutex);
  return true;
}

void persist_clear_all() {
  if (g_ready && s_prefsMutex) {
    if (xSemaphoreTake(s_prefsMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
      prefs.clear();
      xSemaphoreGive(s_prefsMutex);
    }
  }
}
