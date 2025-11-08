#pragma once
#include <Arduino.h>
#include <ArduinoJson.h>
#include <functional>

// Forward declaration to resolve OutputOrigin for logIO signature

#include "outputs.h"

namespace Events {

enum class Severity : uint8_t { INFO=0, WARN=1, ERROR=2 };
enum class Type : uint8_t {
    INIT=0, DISCOVERY=1, LINK_CHANGE=2, IP_CHANGE=3, IO_CHANGE=4,
    CMD_SENT=5, CMD_EXECUTED=6, CMD_FAILED=7, ERROR_EVT=8, WARNING_EVT=9,
    METRIC=10, HEARTBEAT=11
};

struct Event {
    uint16_t schema = 1;
    uint64_t id = 0;
    uint32_t bootSession = 0;
    String correlationId;
    uint64_t ts_ms = 0;
    time_t ts_epoch_s = 0;
    String deviceId, deviceName, model, serial;
    Type type;
    Severity severity;
    String source;
    String description;
    int ioChannel = -1;
    String ioName, ioPrev, ioNew;
    String meta;
};

struct Query {
    String deviceId;
    Type type;
    Severity severity;
    String source;
    String search;
    uint64_t afterMs = 0, beforeMs = 0;
    uint32_t offset = 0, limit = 50;
};

struct Summary {
    size_t total = 0;
    size_t infoCount = 0, warnCount = 0, errorCount = 0;
    time_t latestEpoch = 0;
    uint64_t latestId = 0;
};

using EventCallback = std::function<void(const Event&)>;

void begin(uint16_t capacity = 1024, uint32_t retentionDays = 30);
uint32_t bootSessionId();
uint64_t nextId();
uint64_t log(const Event& e);
uint64_t logBasic(Type type, Severity sev, const String& src, const String& desc, const String& deviceId = "");
uint64_t logNetwork(Type type, Severity sev, const String& desc, const String& meta = "");
uint64_t logIO(Type type, Severity sev, int channel, const String& name, const String& prev, const String& next, const String& src = "", const String& meta = "", OutputOrigin origin = OutputOrigin::Unknown);
uint64_t logCommand(Type type, Severity sev, const String& desc, const String& meta = "");
size_t query(const Query& q, Event* out, size_t maxOut, Summary* summaryOut);
uint16_t size();
uint16_t capacity();
void toJson(const Event& e, JsonObject obj, bool includeLocalTime);
String csvHeader();
String toCsvRow(const Event& e);
void onNew(const EventCallback& cb);
void maintain();
const char* typeName(Type t);
const char* severityName(Severity s);
// Controller identity helpers
}
