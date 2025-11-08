#pragma once
#include <Arduino.h>
bool persist_begin();
bool persist_is_ready();
bool persist_get(const String& key, String& out);
bool persist_set(const String& key, const String& val);
bool persist_get_bool(const String& key, bool& out);
bool persist_set_bool(const String& key, bool val);