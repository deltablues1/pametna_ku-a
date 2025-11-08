#include "events.h"
#include <WiFi.h>
#include <ETH.h>
#include <time.h>
#include <new>
#include <Arduino.h>

namespace Events {

static Event* g_buf = nullptr;
static uint16_t g_cap = 0;
static uint16_t g_size = 0;
static uint16_t g_start = 0;        // index of oldest
static uint64_t g_seq = 0;          // monotonically increasing id
static uint32_t g_bootSession = 0;  // session id
static uint32_t g_retentionDays = 30;

static EventCallback g_cbs[4];
static uint8_t g_cbCount = 0;

// Controller identity cache
static String g_devId = "controller";
static String g_devName = "ESP32 Kontroler";
static String g_model = "ESP32";
static String g_serial;

static inline uint16_t wrap(uint16_t i) { return (g_cap == 0) ? 0 : (i % g_cap); }

static bool str_ieq(const String& a, const String& b) {
  if (a.length() != b.length()) return false;
  for (size_t i=0;i<a.length();++i) {
    char ca = tolower(a[i]);
    char cb = tolower(b[i]);
    if (ca != cb) return false;
  }
  return true;
}

static bool str_icontains(const String& hay, const String& needle) {
  if (needle.length() == 0) return true;
  size_t H = hay.length();
  size_t N = needle.length();
  if (N > H) return false;
  for (size_t i=0;i+N<=H;++i) {
    bool ok = true;
    for (size_t j=0;j<N;++j) {
      if (tolower(hay[i+j]) != tolower(needle[j])) { ok=false; break; }
    }
    if (ok) return true;
  }
  return false;
}

static time_t safe_epoch() {
  time_t now = time(nullptr);
  if (now < 946684800) { // before 2000-01-01 means RTC not set
    return 0;
  }
  return now;
}

static String getMacString() {
  String mac;
  if (ETH.localIP()) {
    mac = ETH.macAddress();
  } else {
    mac = WiFi.macAddress();
  }
  if (mac.length() == 0) mac = "00:00:00:00:00:00";
  return mac;
}

static String csvEscape(const String& s) {
  bool needQuotes = false;
  for (size_t i=0;i<s.length();++i) {
    char c = s[i];
    if (c==',' || c=='"' || c=='\n' || c=='\r') { needQuotes = true; break; }
  }
  if (!needQuotes) return s;
  String out = "\"";
  for (size_t i=0;i<s.length();++i) {
    char c = s[i];
    if (c=='"') out += "\"\"";
    else out += c;
  }
  out += "\"";
  return out;
}

const char* typeName(Type t) {
  switch (t) {
    case Type::INIT: return "init";
    case Type::DISCOVERY: return "discovery";
    case Type::LINK_CHANGE: return "link_change";
    case Type::IP_CHANGE: return "ip_change";
    case Type::IO_CHANGE: return "io_change";
    case Type::CMD_SENT: return "cmd_sent";
    case Type::CMD_EXECUTED: return "cmd_executed";
    case Type::CMD_FAILED: return "cmd_failed";
    case Type::ERROR_EVT: return "error";
    case Type::WARNING_EVT: return "warning";
    case Type::METRIC: return "metric";
    case Type::HEARTBEAT: return "heartbeat";
    default: return "unknown";
  }
}

const char* severityName(Severity s) {
  switch (s) {
    case Severity::INFO: return "info";
    case Severity::WARN: return "warn";
    case Severity::ERROR: return "error";
    default: return "info";
  }
}

void begin(uint16_t capacity, uint32_t retentionDays) {
  if (g_buf) { delete [] g_buf; g_buf = nullptr; }
  g_cap = capacity > 32 ? capacity : 128; // minimum reasonable capacity
  g_buf = new Event[g_cap];
  g_size = 0;
  g_start = 0;
  g_seq = 0;
  g_retentionDays = retentionDays;

  // Boot session: combine millis and MAC
  uint32_t ms = (uint32_t) millis();
  String mac = getMacString(); // format AA:BB:...
  uint32_t macTail = 0;
  for (size_t i=0;i<mac.length();++i) {
    char c = mac[i];
    if (c!=':') macTail = (macTail * 33u) ^ (uint8_t)c;
  }
  g_bootSession = (ms ^ macTail) & 0x7FFFFFFF;

  g_serial = getMacString();
}

uint32_t bootSessionId() { return g_bootSession; }
uint64_t nextId() { return g_seq + 1; }

String controllerDeviceId() { return g_devId; }
String controllerName() { return g_devName; }
String controllerModel() { return g_model; }
String controllerSerial() { return g_serial; }

static void push(const Event& e) {
  uint16_t pos;
  if (g_size < g_cap) {
    pos = wrap(g_start + g_size);
    g_size++;
  } else {
    // overwrite the oldest
    pos = g_start;
    g_start = wrap(g_start + 1);
  }
  g_buf[pos] = e;
}

uint64_t log(const Event& in) {
  Event e = in;
  e.id = ++g_seq;
  e.bootSession = g_bootSession;
  if (e.ts_ms == 0) e.ts_ms = millis();
  if (e.ts_epoch_s == 0) e.ts_epoch_s = safe_epoch();

  // Dedup: if previous equals same type+deviceId+description within 250ms, drop
  if (g_size > 0) {
    uint16_t lastIdx = wrap(g_start + g_size - 1);
    const Event& last = g_buf[lastIdx];
    if (last.type == e.type &&
        last.deviceId == e.deviceId &&
        last.description == e.description &&
        (e.ts_ms >= last.ts_ms) && (e.ts_ms - last.ts_ms <= 250)) {
      // skip duplicate
      return last.id;
    }
  }

  push(e);

  // Notify subscribers
  for (uint8_t i=0;i<g_cbCount;i++) {
    if (g_cbs[i]) g_cbs[i](e);
  }
  return e.id;
}

uint64_t logBasic(Type type, Severity sev, const String& deviceId, const String& description, const String& source, const String& metaJson) {
  Event e;
  e.type = type;
  e.severity = sev;
  e.deviceId = deviceId.length() ? deviceId : g_devId;
  e.deviceName = g_devName;
  e.model = g_model;
  e.serial = g_serial;
  e.source = source;
  e.description = description;
  e.meta = metaJson;
  return log(e);
}

uint64_t logNetwork(Type type, Severity sev, const String& description, const String& metaJson) {
  return logBasic(type, sev, g_devId, description, "network", metaJson);
}

uint64_t logIO(Severity sev, const String& deviceId, int channel, const String& name, const String& prevVal, const String& newVal, const String& source, const String& correlationId, const String& metaJson) {
  Event e;
  e.type = Type::IO_CHANGE;
  e.severity = sev;
  e.deviceId = deviceId;
  e.deviceName = name;
  e.model = g_model;
  e.serial = g_serial;
  e.source = source;
  e.description = String("IO promjena: ") + name + " [" + channel + "] " + prevVal + " -> " + newVal;
  e.ioChannel = channel;
  e.ioName = name;
  e.ioPrev = prevVal;
  e.ioNew = newVal;
  e.correlationId = correlationId;
  e.meta = metaJson;
  return log(e);
}

uint64_t logCommand(const String& action, const String& target, bool success, const String& source, const String& correlationId, const String& errorMsg, const String& metaJson) {
  Event e;
  e.type = success ? Type::CMD_EXECUTED : Type::CMD_FAILED;
  e.severity = success ? Severity::INFO : Severity::ERROR;
  e.deviceId = target;
  e.deviceName = target;
  e.model = g_model;
  e.serial = g_serial;
  e.source = source;
  e.description = String("Komanda '") + action + "' za " + target + (success ? " uspje??na" : " neuspje??na");
  if (!errorMsg.isEmpty()) {
    e.description += String(" (") + errorMsg + ")";
  }
  e.correlationId = correlationId;
  e.meta = metaJson;
  return log(e);
}

void toJson(const Event& e, JsonObject obj, bool includeLocalTime) {
  obj["schema"] = e.schema;
  obj["id"] = e.id;
  obj["bootSession"] = e.bootSession;
  obj["corr"] = e.correlationId;
  obj["ts_ms"] = e.ts_ms;
  obj["ts_epoch"] = (uint64_t)e.ts_epoch_s;
  obj["type"] = typeName(e.type);
  obj["sev"] = severityName(e.severity);
  obj["source"] = e.source;
  obj["deviceId"] = e.deviceId;
  obj["deviceName"] = e.deviceName;
  obj["model"] = e.model;
  obj["serial"] = e.serial;
  obj["desc"] = e.description;
  if (e.ioChannel >= 0) {
    obj["ioCh"] = e.ioChannel;
    obj["ioName"] = e.ioName;
    obj["ioPrev"] = e.ioPrev;
    obj["ioNew"] = e.ioNew;
  }
  if (e.meta.length()) {
    // meta is already JSON string; parse best-effort
    JsonDocument tmp;
    if (deserializeJson(tmp, e.meta) == DeserializationError::Ok) {
      obj["meta"] = tmp.as<JsonVariantConst>();
    } else {
      obj["meta_raw"] = e.meta;
    }
  }
  if (includeLocalTime && e.ts_epoch_s != 0) {
    // leave to frontend to localize; backend provides epoch
  }
}

String csvHeader() {
  return "schema,id,bootSession,ts_ms,ts_epoch,type,sev,source,deviceId,deviceName,model,serial,desc,ioCh,ioName,ioPrev,ioNew,corr,meta\n";
}

String toCsvRow(const Event& e) {
  String row;
  row.reserve(256);
  row += String(e.schema); row += ',';
  row += String(e.id); row += ',';
  row += String(e.bootSession); row += ',';
  row += String((unsigned long long)e.ts_ms); row += ',';
  row += String((unsigned long long)e.ts_epoch_s); row += ',';
  row += csvEscape(typeName(e.type)); row += ',';
  row += csvEscape(severityName(e.severity)); row += ',';
  row += csvEscape(e.source); row += ',';
  row += csvEscape(e.deviceId); row += ',';
  row += csvEscape(e.deviceName); row += ',';
  row += csvEscape(e.model); row += ',';
  row += csvEscape(e.serial); row += ',';
  row += csvEscape(e.description); row += ',';
  row += (e.ioChannel >= 0 ? String(e.ioChannel) : ""); row += ',';
  row += csvEscape(e.ioName); row += ',';
  row += csvEscape(e.ioPrev); row += ',';
  row += csvEscape(e.ioNew); row += ',';
  row += csvEscape(e.correlationId); row += ',';
  row += csvEscape(e.meta);
  row += "\n";
  return row;
}

size_t query(const Query& q, Event* out, size_t maxOut, Summary* summaryOut) {
  size_t written = 0;
  Summary sum{};
  // iterate from oldest to newest
  for (uint16_t i=0;i<g_size;i++) {
    uint16_t idx = wrap(g_start + i);
    const Event& e = g_buf[idx];

    // time filters
    if (q.since_id > 0 && !(e.id > q.since_id)) continue;
    if (q.from_epoch_s > 0 && !(e.ts_epoch_s >= q.from_epoch_s)) continue;
    if (q.to_epoch_s > 0 && !(e.ts_epoch_s <= q.to_epoch_s)) continue;
    if (q.from_ts_ms > 0 && !(e.ts_ms >= q.from_ts_ms)) continue;
    if (q.to_ts_ms > 0 && !(e.ts_ms <= q.to_ts_ms)) continue;

    // classification filters
    if (q.deviceId.length() && !str_ieq(e.deviceId, q.deviceId)) continue;
    if (q.type.length() && !str_ieq(String(typeName(e.type)), q.type)) continue;
    if (q.severity.length() && !str_ieq(String(severityName(e.severity)), q.severity)) continue;
    if (q.source.length() && !str_ieq(e.source, q.source)) continue;

    // search
    if (q.search.length()) {
      bool hit = false;
      if (str_icontains(e.description, q.search)) hit = true;
      else if (str_icontains(e.meta, q.search)) hit = true;
      if (!hit) continue;
    }

    // summary counters
    sum.total++;
    if (e.severity == Severity::INFO) sum.infoCount++;
    else if (e.severity == Severity::WARN) sum.warnCount++;
    else if (e.severity == Severity::ERROR) sum.errorCount++;
    if (e.ts_epoch_s > sum.latestEpoch) sum.latestEpoch = e.ts_epoch_s;
    if (e.id > sum.latestId) sum.latestId = e.id;

    // pagination
    if (sum.total <= q.offset) continue;
    if (written >= maxOut) continue;

    out[written++] = e;
  }

  if (summaryOut) *summaryOut = sum;
  return written;
}

void onNew(const EventCallback& cb) {
  if (g_cbCount < 4) {
    g_cbs[g_cbCount++] = cb;
  }
}

void maintain() {
  if (g_size == 0) return;
  if (g_retentionDays == 0) return;
  time_t now = safe_epoch();
  if (now == 0) return; // no RTC, skip retention by epoch
  time_t cutoff = now - (time_t)g_retentionDays * 24 * 3600;

  // pop from front while older than cutoff
  while (g_size > 0) {
    const Event& e = g_buf[g_start];
    if (e.ts_epoch_s == 0 || e.ts_epoch_s >= cutoff) break;
    // drop oldest
    g_start = wrap(g_start + 1);
    g_size--;
  }
}

// Ring buffer stats
uint16_t size(){ return g_size; }
uint16_t capacity(){ return g_cap; }

} // namespace Events
