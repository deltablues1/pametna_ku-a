#pragma once
#include "events.h"
namespace Events {
  inline void logSimple(Type t, Severity s, const char* msg) {
    Event e;
    e.type = t;
    e.severity = s;
    e.source = F("SYS");
    e.meta = msg;   // short message!
    log(e);
  }
}