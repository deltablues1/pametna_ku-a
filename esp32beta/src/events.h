#pragma once
#include <Arduino.h>
#include <ArduinoJson.h>
#include <functional>

namespace Events {

enum class Severity : uint8_t {
  INFO = 0,
  WARN = 1,
  ERROR = 2
};

enum class Type : uint8_t {
  INIT = 0,
  DISCOVERY = 1,
  LINK_CHANGE = 2,
  IP_CHANGE = 3,
  IO_CHANGE = 4,
  CMD_SENT = 5,
  CMD_EXECUTED = 6,
  CMD_FAILED = 7,
  ERROR_EVT = 8,
  WARNING_EVT = 9,
  METRIC = 10,
  HEARTBEAT = 11
};

struct Event {
  // Versioned event schema
  uint16_t schema = 1;

  // Identifiers
  uint64_t id = 0;            // monotonically increasing
  uint32_t bootSession = 0;   // session id for this boot
  String correlationId;       // link related events (commands & IO)

  // Timing
  uint64_t ts_ms = 0;         // millis-based monotonic timestamp
  time_t   ts_epoch_s = 0;    // epoch seconds if RTC/time set (0 if unknown)

  // Device identity (origin/subject)
  String deviceId;            // e.g. "controller" or "io:light:3"
  String deviceName;          // human-friendly
  String model;               // controller/device model
  String serial;              // MAC/serial

  // Classification
  Type type = Type::INIT;
  Severity severity = Severity::INFO;
  String source;              // ui|schedule|rule|api|watchdog|system|mqtt|network|unknown

  // Description
  String description;

  // IO specific extras
  int ioChannel = -1;         // channel index
  String ioName;              // channel name
  String ioPrev;              // previous state/value
  String ioNew;               // new state/value

  // Free-form metadata (pre-serialized JSON string, size-limited)
  String meta;                // e.g. "{"ip":"1.2.3.4"}"
};

// Query filters for listing
struct Query {
  // time range (epoch seconds) and/or millis
  time_t from_epoch_s = 0;
  time_t to_epoch_s = 0;
  uint64_t since_id = 0;         // fetch events with id > since_id
  uint64_t from_ts_ms = 0;       // optional millis range
  uint64_t to_ts_ms = 0;

  // classification filters (empty = any)
  String deviceId;
  String type;                   // string type name
  String severity;               // info|warn|error
  String source;

  // full text search (description + meta)
  String search;

  // pagination
  size_t offset = 0;
  size_t limit = 100;            // bounded by implementation
};

// Stats / summary
struct Summary {
  uint32_t total = 0;
  uint32_t infoCount = 0;
  uint32_t warnCount = 0;
  uint32_t errorCount = 0;
  time_t latestEpoch = 0;
  uint64_t latestId = 0;
};

using EventCallback = std::function<void(const Event&)>;

void begin(uint16_t capacity = 1024, uint32_t retentionDays = 30);
uint32_t bootSessionId();
uint64_t nextId();

// Logging helpers (metaJson must be a compact JSON string or empty)
uint64_t log(const Event& e);
uint64_t logBasic(Type type, Severity sev, const String& deviceId, const String& description, const String& source = "system", const String& metaJson = "");
uint64_t logNetwork(Type type, Severity sev, const String& description, const String& metaJson = "");
uint64_t logIO(Severity sev, const String& deviceId, int channel, const String& name, const String& prevVal, const String& newVal, const String& source = "system", const String& correlationId = String(), const String& metaJson = "");
uint64_t logCommand(const String& action, const String& target, bool success, const String& source = "ui", const String& correlationId = String(), const String& errorMsg = String(), const String& metaJson = "");

// Export and querying
// Returns number of events written into out array (up to maxOut)
size_t query(const Query& q, Event* out, size_t maxOut, Summary* summaryOut = nullptr);

// Ring buffer stats
uint16_t size();
uint16_t capacity();

// Serialize single event to ArduinoJson object
void toJson(const Event& e, JsonObject obj, bool includeLocalTime = false);

// CSV header and row
String csvHeader();
String toCsvRow(const Event& e);

// Subscribe to new events (SSE bridge)
void onNew(const EventCallback& cb);

// Maintenance (retention / compaction / dedup)
void maintain();

// Utility converters
const char* typeName(Type t);
const char* severityName(Severity s);

// Controller identity helpers (populated internally)
String controllerDeviceId();
String controllerName();
String controllerModel();
String controllerSerial();

} // namespace Events