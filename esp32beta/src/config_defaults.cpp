#include "config.h"

// Provide default definitions to satisfy linker if not defined elsewhere
// g_mqtt is already defined in mqtt_if.cpp
WebAuth g_webAuth;            // disabled by default as per config.h
ApiAuth g_apiAuth;            // token auth disabled by default
OtaConfig g_otaCfg;           // defaults (no signature requirement)