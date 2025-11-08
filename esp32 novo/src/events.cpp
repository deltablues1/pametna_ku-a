#include "events.h"
#include <Arduino.h>
#include <ArduinoJson.h>
#include <vector>

namespace Events {
static std::vector<Event> g_events;
static uint64_t g_nextId = 0;
static uint32_t g_bootSession = 0;
static EventCallback g_onNew = nullptr;

void begin(uint16_t capacity, uint32_t retentionDays) {
  g_events.clear();
  g_events.reserve(capacity);
  g_bootSession = millis() + random(100000);
  g_nextId = 0;
}

uint32_t bootSessionId() { return g_bootSession; }
uint64_t nextId() { return g_nextId; }

uint64_t log(const Event& e) {
  Event ev = e;
  ev.id = g_nextId++;
  ev.bootSession = g_bootSession;
  ev.ts_ms = millis();
  
  // Use static buffer to avoid heap fragmentation
  static char timeBuffer[20];
  snprintf(timeBuffer, sizeof(timeBuffer), "%lu", ev.ts_ms);
  
  if (g_events.size() >= g_events.capacity()) {
    g_events.erase(g_events.begin());
  }
  g_events.push_back(ev);
  if (g_onNew) g_onNew(ev);
  
  // Feed watchdog during potentially heavy operations
  yield();
  return ev.id;
}

uint64_t logBasic(Type type, Severity sev, const String& src, const String& desc, const String& deviceId) {
  Event e;
  e.type = type;
  e.severity = sev;
  e.source = src;
  e.description = desc;
  e.deviceId = deviceId;
  return log(e);
}

uint64_t logNetwork(Type type, Severity sev, const String& desc, const String& meta) {
  Event e;
  e.type = type;
  e.severity = sev;
  e.source = "network";
  e.description = desc;
  e.meta = meta;
  return log(e);
}

uint64_t logIO(Type type, Severity sev, int channel, const String& name, const String& prev, const String& next, const String& src, const String& meta, OutputOrigin origin) {
  Event e;
  e.type = type;
  e.severity = sev;
  e.ioChannel = channel;
  e.ioName = name;
  e.ioPrev = prev;
  e.ioNew = next;
  e.source = src;
  e.meta = meta;
  return log(e);
}

uint64_t logCommand(Type type, Severity sev, const String& desc, const String& meta) {
  Event e;
  e.type = type;
  e.severity = sev;
  e.source = "cmd";
  e.description = desc;
  e.meta = meta;
  return log(e);
}

size_t query(const Query& q, Event* out, size_t maxOut, Summary* summaryOut) {
  size_t count = 0;
  for (const auto& e : g_events) {
    if (q.deviceId.length() && e.deviceId != q.deviceId) continue;
    if (q.type != Type::INIT && e.type != q.type) continue;
    if (q.severity != Severity::INFO && e.severity != q.severity) continue;
    if (q.source.length() && e.source != q.source) continue;
  if (q.search.length() && e.description.indexOf(q.search) == -1) continue;
    if (q.afterMs && e.ts_ms < q.afterMs) continue;
    if (q.beforeMs && e.ts_ms > q.beforeMs) continue;
    if (count < maxOut) out[count] = e;
    ++count;
  }
  if (summaryOut) {
    summaryOut->total = count;
    summaryOut->infoCount = 0;
    summaryOut->warnCount = 0;
    summaryOut->errorCount = 0;
    for (const auto& e : g_events) {
      if (e.severity == Severity::INFO) summaryOut->infoCount++;
      if (e.severity == Severity::WARN) summaryOut->warnCount++;
      if (e.severity == Severity::ERROR) summaryOut->errorCount++;
      if (e.ts_epoch_s > summaryOut->latestEpoch) summaryOut->latestEpoch = e.ts_epoch_s;
      if (e.id > summaryOut->latestId) summaryOut->latestId = e.id;
    }
  }
  return count;
}

uint16_t size() { return g_events.size(); }
uint16_t capacity() { return g_events.capacity(); }

void toJson(const Event& e, JsonObject obj, bool includeLocalTime) {
  obj["id"] = e.id;
  obj["ts_ms"] = e.ts_ms;
  obj["type"] = typeName(e.type);
  obj["sev"] = severityName(e.severity);
  obj["source"] = e.source;
  obj["deviceId"] = e.deviceId;
  obj["desc"] = e.description;
  obj["ioChannel"] = e.ioChannel;
  obj["ioName"] = e.ioName;
  obj["ioPrev"] = e.ioPrev;
  obj["ioNew"] = e.ioNew;
  obj["meta"] = e.meta;
  if (includeLocalTime) obj["ts_epoch_s"] = e.ts_epoch_s;
}

String csvHeader() {
  return "id,ts_ms,type,sev,source,deviceId,desc,ioChannel,ioName,ioPrev,ioNew,meta";
}
String toCsvRow(const Event& e) {
  return String(e.id)+","+String(e.ts_ms)+","+typeName(e.type)+","+severityName(e.severity)+","+e.source+","+e.deviceId+","+e.description+","+String(e.ioChannel)+","+e.ioName+","+e.ioPrev+","+e.ioNew+","+e.meta;
}
void onNew(const EventCallback& cb) { g_onNew = cb; }
void maintain() {}
const char* typeName(Type t) {
  switch(t) {
    case Type::INIT: return "init";
    case Type::DISCOVERY: return "discovery";
    case Type::LINK_CHANGE: return "link";
    case Type::IP_CHANGE: return "ip";
    case Type::IO_CHANGE: return "io";
    case Type::CMD_SENT: return "cmd_sent";
    case Type::CMD_EXECUTED: return "cmd_exec";
    case Type::CMD_FAILED: return "cmd_fail";
    case Type::ERROR_EVT: return "error";
    case Type::WARNING_EVT: return "warn";
    case Type::METRIC: return "metric";
    case Type::HEARTBEAT: return "heartbeat";
    default: return "other";
  }
}
const char* severityName(Severity s) {
  switch(s) {
    case Severity::INFO: return "info";
    case Severity::WARN: return "warn";
    case Severity::ERROR: return "error";
    default: return "other";
  }
}
} // namespace Events
