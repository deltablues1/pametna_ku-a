#include "persist.h"
#include <Preferences.h>

static Preferences pref;
static bool g_prefReady = false;

bool persist_begin(){
  // RW namespace "smarthome"
  g_prefReady = pref.begin("smarthome", false);
  return g_prefReady;
}

bool persist_is_ready(){
  return g_prefReady;
}

// Return true if key exists; out is set accordingly. If missing/corrupt -> false and out=""
bool persist_get(const String& k, String& out){
  if (!g_prefReady){ out = ""; return false; }
  // Use getString with default; treat as successful read even if missing
  out = pref.getString(k.c_str(), "");
  return true;
}

// Return true if key exists; out is set to stored value. If missing/corrupt -> false and out=false
bool persist_get_bool(const String& k, bool& out){
  if (!g_prefReady){ out = false; return false; }
  // Use getBool with default; treat as successful read even if missing
  out = pref.getBool(k.c_str(), false);
  return true;
}

// Store helpers
bool persist_set(const String& k, const String& v){
  if (!g_prefReady) return false;
  return pref.putString(k.c_str(), v);
}
bool persist_set_bool(const String& k, bool v){
  if (!g_prefReady) return false;
  return pref.putBool(k.c_str(), v);
}