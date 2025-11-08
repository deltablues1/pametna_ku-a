#include "web.h"
#include <ESPAsyncWebServer.h>
#include <AsyncEventSource.h>
#include <Update.h>
#include <ArduinoJson.h>
#include <Wire.h>
#include <esp_task_wdt.h>
#include <WiFi.h>
#include "network.h"
#include "outputs.h"
#include "config.h"
#include "events.h"
#include "hw_mcp.h"
#include <vector>

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"

// Ensure MCP presence helpers are visible to this TU
extern bool mcp_lights_available();
extern bool mcp_outlet_pir_available();


// I2C scan helper (0x03..0x77)
static void i2c_scan(std::vector<uint8_t>& out) {
  out.clear();
  for (uint8_t addr = 0x03; addr <= 0x77; ++addr) {
    Wire.beginTransmission(addr);
    if (Wire.endTransmission() == 0) out.push_back(addr);
  }
}

 // Helper: optional BasicAuth using existing g_webAuth
static bool ensureAuth(AsyncWebServerRequest* req){
  if (!g_webAuth.enabled) return true;
  if (req->authenticate(g_webAuth.user.c_str(), g_webAuth.pass.c_str())) return true;
  req->requestAuthentication();
  return false;
}

// Optional token auth + simple rate limiting for API
static bool checkApiToken(AsyncWebServerRequest* req){
  if (!g_apiAuth.token_enabled) return true;
  String tok;
  if (req->hasHeader("Authorization")){
    AsyncWebHeader* h = req->getHeader("Authorization");
    String v = h->value();
    if (v.startsWith("Bearer ")) tok = v.substring(7);
  }
  if (!tok.length() && req->hasParam("token")){
    tok = req->getParam("token")->value();
  }
  if (tok.length() && tok == g_apiAuth.token) return true;
  req->send(401, "application/json", "{\"error\":\"unauthorized\"}");
  return false;
}

static bool checkRateLimit(AsyncWebServerRequest* req){
  if (g_apiAuth.rate_limit_per_min == 0) return true;
  static uint32_t windowStart = 0;
  static uint16_t count = 0;
  uint32_t now = millis();
  if (now - windowStart >= 60000) { windowStart = now; count = 0; }
  count++;
  if (count > g_apiAuth.rate_limit_per_min){
    req->send(429, "application/json", "{\"error\":\"rate_limit\"}");
    return false;
  }
  return true;
}

// Helper function to send JSON response with memory monitoring
template<typename TDoc>
static void sendJson(AsyncWebServerRequest* req, const TDoc& doc, int code = 200) {
  size_t freeHeap = ESP.getFreeHeap();
  Serial.printf("[WEB] Free heap before JSON processing: %d bytes\n", freeHeap);

  if (freeHeap < 8192) {
    Serial.println("[WEB] WARNING: Low memory detected, forcing small delay");
    delay(10);
  }

  String buf;
  size_t jsonSize = serializeJson(doc, buf);
  Serial.printf("[WEB] JSON serialized, size: %d bytes\n", jsonSize);

  freeHeap = ESP.getFreeHeap();
  Serial.printf("[WEB] Free heap after JSON processing: %d bytes\n", freeHeap);

  req->send(code, "application/json", buf);
  Serial.printf("[WEB] Free heap after response sent: %d bytes\n", ESP.getFreeHeap());
}

// ESP32 watchdog and stability monitoring
static unsigned long lastToggleTime = 0;
static int toggleCount = 0;
static const unsigned long TOGGLE_TIMEOUT_MS = 10000; // 10 second timeout

static bool checkSystemStability() {
  size_t freeHeap = ESP.getFreeHeap();
  size_t minHeap = ESP.getMinFreeHeap();
  Serial.printf("[WEB] System check - Free: %d, Min: %d bytes\n", freeHeap, minHeap);

  if (freeHeap < 4096) {
    Serial.println("[WEB] ERROR: Critical low memory!");
    return false;
  }
  if (minHeap < 2048) {
    Serial.println("[WEB] WARNING: Memory fragmentation detected");
  }
  return true;
}

// Toggle channel function with ESP32 stability checks
static bool toggleChannel(const char* type, int index, String& err) {
  if (!checkSystemStability()) {
    err = "System unstable - low memory";
    return false;
  }

  unsigned long now = millis();
  if (now - lastToggleTime < 100) {
    err = "Toggle rate limit exceeded";
    Serial.println("[WEB] WARNING: Toggle rate limit exceeded");
    return false;
  }

  #ifdef CONFIG_ESP_TASK_WDT
  esp_task_wdt_reset();
  #endif

  lastToggleTime = now;
  toggleCount++;

  Serial.printf("[WEB] Toggle #%d starting...\n", toggleCount);
  if (!type || (strcmp(type, "light") != 0 && strcmp(type, "outlet") != 0)) {
    err = "type must be light|outlet";
    return false;
  }
  if (index < 0 || index > 15) {
    err = "index out of range [0..15]";
    return false;
  }

  ChanType channelType = (strcmp(type, "light") == 0) ? ChanType::LIGHT : ChanType::OUTLET;
  bool on, present;
  if (!output_get(channelType, index, on, present)) {
    err = "Device not found";
    return false;
  }
  if (!present) {
    err = "Device not present";
    return false;
  }

  Serial.printf("[WEB] Calling output_toggle for %s %d\n", type, index);
  Serial.flush();

  unsigned long toggleStart = millis();
  bool success = output_toggle(channelType, index, false, OutputOrigin::WebUI);
  unsigned long toggleDuration = millis() - toggleStart;

  Serial.printf("[WEB] output_toggle result: %s (took %lu ms)\n",
               success ? "SUCCESS" : "FAILED", toggleDuration);
  Serial.flush();

  if (toggleDuration > TOGGLE_TIMEOUT_MS) {
    err = "Toggle operation timed out";
    Serial.printf("[WEB] WARNING: Toggle took %lu ms (timeout: %lu ms)\n",
                 toggleDuration, TOGGLE_TIMEOUT_MS);
    return false;
  }
  if (!success) {
    err = "Toggle operation failed";
    Serial.printf("[WEB] Toggle error: %s\n", err.c_str());
    Serial.flush();
    return false;
  }

  Serial.printf("[WEB] Toggle #%d completed successfully in %lu ms\n", toggleCount, toggleDuration);
  return true;
}

static AsyncWebServer server(80);

// Event stream (SSE)
#include <AsyncTCP.h>
#include <vector>
static AsyncEventSource sse("/api/events/stream");

// In-memory device snapshot for the UI devices panel
struct DeviceRow {
  String id;
  String name;
  String type; // light|outlet|controller
  int index = -1;
  bool present = false;
  bool state = false;
  String ip;
  int rssi = 0;
};
static void fillDevices(JsonArray arr) {
  // Controller
  {
    JsonObject o = arr.add<JsonObject>();
    o["id"] = "controller";
    o["name"] = "Kontroler";
    o["type"] = "controller";
    o["present"] = true;
    o["state"] = net_have_ip();
    o["ip"] = net_ip_str();
    o["rssi"] = 0; // optional
  }
  // Lights
  for (int i=0;i<16;i++) {
    bool on, pres;
    output_get(ChanType::LIGHT, i, on, pres);
    if (!pres) continue;
    JsonObject o = arr.add<JsonObject>();
    o["id"] = String("light:")+String(i);
    o["name"] = light_name(i);
    o["type"] = "light";
    o["index"] = i;
    o["present"] = pres;
    o["state"] = on;
  }
  // Outlets
  for (int i=0;i<16;i++) {
    bool on, pres;
    output_get(ChanType::OUTLET, i, on, pres);
    if (!pres) continue;
    JsonObject o = arr.add<JsonObject>();
    o["id"] = String("outlet:")+String(i);
    o["name"] = outlet_name(i);
    o["type"] = "outlet";
    o["index"] = i;
    o["present"] = pres;
    o["state"] = on;
  }
}

static String indexHtml(){
  return R"HTML(
<!doctype html><html lang="hr"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>ESP32 Smart Home</title>
<style>
:root{color-scheme:light dark}
body{font-family:system-ui,-apple-system,Segoe UI,Roboto,Ubuntu,Cantarell,Noto Sans,sans-serif;margin:0;padding:0;background:Canvas;color:CanvasText}
header{display:flex;align-items:center;justify-content:space-between;padding:12px 16px;border-bottom:1px solid color-mix(in oklab, Canvas, CanvasText 15%)}
h1{margin:0;font-size:18px}
nav a{padding:8px 12px;border-radius:8px;margin-right:6px;text-decoration:none;color:inherit;border:1px solid transparent}
nav a.active{border-color:color-mix(in oklab, CanvasText, Canvas 70%);background:color-mix(in oklab, CanvasText, Canvas 90%)}
.container{padding:16px}
.grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(210px,1fr));gap:10px}
.card{border:1px solid color-mix(in oklab, CanvasText, Canvas 80%);border-radius:10px;padding:12px;background:Canvas}
button{padding:8px 10px;border-radius:8px;border:1px solid color-mix(in oklab, CanvasText, Canvas 80%);background:color-mix(in oklab, CanvasText, Canvas 94%);cursor:pointer}
button.on{background:color-mix(in oklab, #0a0, Canvas 85%);color:#041}
.badge{display:inline-block;padding:2px 6px;border-radius:999px;font-size:12px;border:1px solid color-mix(in oklab, CanvasText, Canvas 80%)}
.row{display:flex;align-items:center;gap:8px;justify-content:space-between}
.small{font-size:12px;opacity:.8}
input,select{padding:6px 8px;border-radius:8px;border:1px solid color-mix(in oklab, CanvasText, Canvas 80%);background:Canvas;color:CanvasText}
.section-title{margin:12px 0 8px 0}
.timeline{max-height:50vh;overflow:auto;border:1px solid color-mix(in oklab, CanvasText, Canvas 80%);border-radius:10px;background:Canvas}
.ev{display:grid;grid-template-columns:160px 90px 110px 1fr;gap:10px;padding:8px 10px;border-bottom:1px solid color-mix(in oklab, CanvasText, Canvas 92%);cursor:pointer}
.ev:hover{background:color-mix(in oklab, CanvasText, Canvas 96%)}
.ev .sev-info{color:#036}
.ev .sev-warn{color:#a60}
.ev .sev-error{color:#a00}
.sticky{position:sticky;top:0;background:Canvas;padding:6px 8px;border-bottom:1px solid color-mix(in oklab, CanvasText, Canvas 85%)}
.flex{display:flex;gap:8px;flex-wrap:wrap}
.kbd{font-family:ui-monospace,"SFMono-Regular",Menlo,Monaco,Consolas,monospace;padding:2px 6px;border-radius:6px;background:color-mix(in oklab, CanvasText, Canvas 93%);border:1px solid color-mix(in oklab, CanvasText, Canvas 80%)}
.hidden{display:none}
footer{padding:10px 16px;border-top:1px solid color-mix(in oklab, CanvasText, Canvas 15%);font-size:12px;opacity:.8}
</style></head>
<body>
<header>
  <h1>ESP32 Upravljačka ploča</h1>
  <nav>
    <a href="#home" id="tab-home" class="active">Upravljačka ploča</a>
    <a href="#events" id="tab-events">Događaji</a>
  </nav>
</header>

<div id="view-home" class="container">
  <div id="sys" class="grid">
    <div class="card">
      <div class="row"><b>Status sustava</b><span id="sys-status" class="badge">—</span></div>
      <div class="small" id="sys-fw">FW: —</div>
      <div class="small" id="sys-ip">IP: —</div>
      <div class="small" id="sys-mode">Način: —</div>
    </div>
    <div class="card">
      <div class="row"><b>Mreža</b><span id="sys-mac" class="badge">MAC —</span></div>
      <div class="small" id="sys-net">—</div>
    </div>
    <div class="card">
      <div class="row"><b>Sažetak događaja</b><span id="ev-summary" class="badge">0</span></div>
      <div class="small"><span id="ev-warn">0</span> upozorenja, <span id="ev-err">0</span> grešaka</div>
    </div>
  </div>

  <h3 class="section-title">Svjetla</h3>
  <div id="lights" class="grid"></div>

  <h3 class="section-title">Utičnice</h3>
  <div id="outlets" class="grid"></div>
</div>

<div id="view-events" class="container hidden">
  <div class="card">
    <div class="row" style="align-items:flex-end">
      <div class="flex">
        <label>Raspon:
          <select id="flt-range">
            <option value="15m">Zadnjih 15 min</option>
            <option value="1h">Zadnjih 1 h</option>
            <option value="24h" selected>Zadnjih 24 h</option>
            <option value="custom">Prilagođeno</option>
          </select>
        </label>
        <label>Uređaj:
          <input id="flt-device" placeholder="npr. light:3">
        </label>
        <label>Tip:
          <select id="flt-type">
            <option value="">bilo koji</option>
            <option>init</option><option>discovery</option><option>link_change</option>
            <option>ip_change</option><option>io_change</option>
            <option>cmd_sent</option><option>cmd_executed</option><option>cmd_failed</option>
            <option>error</option><option>warning</option><option>metric</option><option>heartbeat</option>
          </select>
        </label>
        <label>Težina:
          <select id="flt-sev">
            <option value="">bilo koja</option>
            <option>info</option><option>warn</option><option>error</option>
          </select>
        </label>
        <label>Izvor:
          <select id="flt-src">
            <option value="">bilo koji</option>
            <option>ui</option><option>schedule</option><option>rule</option>
            <option>api</option><option>watchdog</option><option>system</option>
            <option>mqtt</option><option>network</option>
          </select>
        </label>
        <label>Pretraga:
          <input id="flt-q" placeholder="opis ili meta">
        </label>
      </div>
      <div class="flex">
        <label><input type="checkbox" id="opt-live" checked> uživo</label>
        <label><input type="checkbox" id="opt-utc"> UTC</label>
        <button id="btn-export-json">Izvoz JSON</button>
        <button id="btn-export-csv">Izvoz CSV</button>
      </div>
    </div>
  </div>

  <div class="timeline" id="timeline">
    <div class="sticky row">
      <div><b>Kronologija</b> <span class="small" id="tl-count">0 stavki</span></div>
      <div class="flex">
        <button id="btn-pause">Pauza</button>
        <span class="small">Zadrži poziciju: tipka <span class="kbd">Space</span></span>
      </div>
    </div>
    <div id="tl-list"></div>
    <div id="tl-empty" class="small" style="padding:10px;display:none">Nema događaja za prikaz.</div>
    <div id="tl-error" class="small" style="padding:10px;display:none;color:#a00">Greška pri učitavanju.</div>
  </div>

  <div class="card" id="detail" style="margin-top:12px;display:none">
    <div class="row"><b>Detalji događaja</b><button id="btn-copy">Kopiraj JSON</button></div>
    <pre id="detail-pre" class="kbd" style="white-space:pre-wrap"></pre>
  </div>

  <h3 class="section-title">Detalji uređaja</h3>
  <div id="devices" class="grid"></div>
</div>

<footer class="row">
  <div>ESP32 Smart Home — lokalizirano (hr), 24h format</div>
  <div class="small">Tema: ovisno o sustavu</div>
</footer>

<script>
const $ = (id) => document.getElementById(id);
const fmtTime = (epoch, utc) => {
  if (!epoch) return '—';
  const d = new Date(epoch*1000);
  return d.toLocaleString('hr-HR', {hour12:false, timeZone: utc ? 'UTC' : undefined});
};

function setActive(tab){
  document.querySelectorAll('nav a').forEach(a=>a.classList.remove('active'));
  document.querySelectorAll('[id^=view-]').forEach(v=>v.classList.add('hidden'));
  if (tab==='events'){ $('tab-events').classList.add('active'); $('view-events').classList.remove('hidden'); }
  else { $('tab-home').classList.add('active'); $('view-home').classList.remove('hidden'); }
  const url = new URL(window.location);
  url.hash = tab==='events' ? '#events' : '#home';
  history.replaceState(null, '', url);
}

$('tab-home').onclick = () => setActive('home');
$('tab-events').onclick = () => { setActive('events'); ensureLoaded(); };

let stateTimer = null;
async function j(p, opts){
  const r = await fetch(p, opts);
  if (!r.ok) throw new Error(r.status+' '+r.statusText);
  const ct = r.headers.get('content-type')||'';
  if (ct.includes('application/json')) return r.json();
  return r.text();
}

async function loadHome(){
  try{
    const s = await j('/api/state');
    $('sys-fw').textContent = 'FW: '+(s.fw||'—');
    $('sys-ip').textContent = 'IP: ' + (s.ip || '—');
    $('sys-mode').textContent = s.ap ? 'AP' : 'Wi‑Fi';
    $('sys-status').textContent = s.ap || s.ip ? 'Online' : 'Offline';
    $('sys-net').textContent = `${s.iface||''} ${s.method||''} | GW: ${s.gateway||'—'} | Subnet: ${s.subnet||'—'} | MAC: ${s.mac||'—'}`;
 
    const L=$('lights'); L.innerHTML='';
    s.lights.forEach((ch,i)=>{
      const d=document.createElement('div'); d.className='card';
      const disabled = !ch.present;
      const label = disabled ? 'N/A' : (ch.state?'ON':'OFF');
      const extraCls = (!disabled && ch.state)?'on':'';
      const btnAttr = disabled ? 'disabled' : `onclick="tog('light',${i})"`;
      d.innerHTML = `<div class="row"><div>${ch.name}</div>
        <button class="${extraCls}" ${btnAttr} id="btn-light-${i}">${label}</button></div>`;
      L.appendChild(d);
    });
    const O=$('outlets'); O.innerHTML='';
    s.outlets.forEach((ch,i)=>{
      if (i>=16) return;
      const d=document.createElement('div'); d.className='card';
      const disabled = !ch.present;
      const label = disabled ? 'N/A' : (ch.state?'ON':'OFF');
      const extraCls = (!disabled && ch.state)?'on':'';
      const btnAttr = disabled ? 'disabled' : `onclick="tog('outlet',${i})"`;
      d.innerHTML = `<div class="row"><div>${ch.name}</div>
        <button class="${extraCls}" ${btnAttr} id="btn-outlet-${i}">${label}</button></div>`;
      O.appendChild(d);
    });

    // MCP presence banner
    try{
      const diag = await j('/api/diag');
      const msgs = [];
      if (!diag.mcp.lights_present) msgs.push('MCP 0x20 not detected');
      if (!diag.mcp.outlet_pir_present) msgs.push('MCP 0x22 not detected');
      let banner = document.getElementById('mcp-banner');
      if (msgs.length){
        if (!banner){
          banner = document.createElement('div');
          banner.id = 'mcp-banner';
          banner.className = 'card';
          banner.style.borderColor = '#a60';
          banner.style.color = '#a60';
          document.getElementById('view-home').prepend(banner);
        }
        banner.textContent = msgs.join(' • ');
      } else if (banner){
        banner.remove();
      }
    }catch(_){}

    // Summary counts
    const sum = await j('/api/events?limit=0'); // only summary
    $('ev-summary').textContent = sum.summary.total;
    $('ev-warn').textContent = sum.summary.warn;
    $('ev-err').textContent = sum.summary.error;
  } catch(e){
    console.error(e);
  }
}
async function tog(type, index){
  try{
    const r = await j('/api/toggle', {
      method:'POST', headers:{'Content-Type':'application/json'},
      body: JSON.stringify({type, index})
    });
    setTimeout(loadHome, 300);
  }catch(e){
    alert('Greška: '+e.message);
  }
}
function startHome(){
  loadHome();
  if (stateTimer) clearInterval(stateTimer);
  stateTimer = setInterval(loadHome, 2000);
}

// Events view
let ev = {
  list: [],
  since: 0,
  live: true,
  utc: false,
  sse: null,
  paused: false
};

function applyFiltersToUrl(){
  const url = new URL(window.location);
  url.searchParams.set('range', $('flt-range').value);
  url.searchParams.set('device', $('flt-device').value);
  url.searchParams.set('type', $('flt-type').value);
  url.searchParams.set('sev', $('flt-sev').value);
  url.searchParams.set('src', $('flt-src').value);
  url.searchParams.set('q', $('flt-q').value);
  url.searchParams.set('utc', $('opt-utc').checked ? '1':'0');
  url.searchParams.set('live', $('opt-live').checked ? '1':'0');
  history.replaceState(null,'',url);
}
function readFiltersFromUrl(){
  const url = new URL(window.location);
  $('flt-range').value = url.searchParams.get('range') || '24h';
  $('flt-device').value = url.searchParams.get('device') || '';
  $('flt-type').value = url.searchParams.get('type') || '';
  $('flt-sev').value = url.searchParams.get('sev') || '';
  $('flt-src').value = url.searchParams.get('src') || '';
  $('flt-q').value = url.searchParams.get('q') || '';
  const utc = url.searchParams.get('utc');
  if (utc !== null) $('opt-utc').checked = (utc === '1' || utc === 'true');
  const live = url.searchParams.get('live');
  if (live !== null) { $('opt-live').checked = (live === '1' || live === 'true'); ev.live = $('opt-live').checked; }
}
['flt-range','flt-device','flt-type','flt-sev','flt-src','flt-q'].forEach(id=>{
  $(id).addEventListener('change', ()=>{ applyFiltersToUrl(); reloadEvents(); });
  $(id).addEventListener('input', ()=>{ applyFiltersToUrl(); debounceReload(); });
});
$('opt-live').addEventListener('change', ()=>{
  ev.live = $('opt-live').checked;
  if (ev.live) connectSSE(); else disconnectSSE();
});
$('opt-utc').addEventListener('change', ()=>{ ev.utc=$('opt-utc').checked; renderTimeline(); });

let debounceT=null;
function debounceReload(){ clearTimeout(debounceT); debounceT=setTimeout(reloadEvents, 350); }

function rowHtml(e){
  const sevCls = e.sev==='error'?'sev-error':(e.sev==='warn'?'sev-warn':'sev-info');
  return `<div class="ev" data-id="${e.id}">
    <div class="small">${fmtTime(e.ts_epoch, ev.utc)}</div>
    <div class="small ${sevCls}">${e.sev}</div>
    <div class="small">${e.type}</div>
    <div class="small">${(e.deviceId||'') + (e.desc?(' — '+e.desc):'')}</div>
  </div>`;
}
function renderTimeline(){
  const list = $('tl-list');
  list.innerHTML = ev.list.map(rowHtml).join('');
  $('tl-count').textContent = ev.list.length+' stavki';
  $('tl-empty').style.display = ev.list.length? 'none':'block';
  Array.from(list.children).forEach(node=>{
    node.onclick = ()=>{
      const id = node.getAttribute('data-id');
      const item = ev.list.find(x=>String(x.id)===String(id));
      if (!item) return;
      $('detail').style.display='block';
      $('detail-pre').textContent = JSON.stringify(item, null, 2);
    };
  });
  if (!ev.paused){
    const tl = $('timeline');
    tl.scrollTop = tl.scrollHeight;
  }
}

async function reloadEvents(){
  readFiltersFromUrl();
  const qp = new URLSearchParams();
  qp.set('limit','200');
  if (ev.since) qp.set('since_id', String(ev.since));
  const map = {
    device:'deviceId', type:'type', sev:'severity', src:'source', q:'q'
  };
  if ($('flt-device').value) qp.set('deviceId',$('flt-device').value);
  if ($('flt-type').value) qp.set('type',$('flt-type').value);
  if ($('flt-sev').value) qp.set('severity',$('flt-sev').value);
  if ($('flt-src').value) qp.set('source',$('flt-src').value);
  if ($('flt-q').value) qp.set('q',$('flt-q').value);
  const range = $('flt-range').value;
  const now = Math.floor(Date.now()/1000);
  let from = 0;
  if (range==='15m') from = now - 15*60;
  else if (range==='1h') from = now - 60*60;
  else if (range==='24h') from = now - 24*60*60;
  if (from) qp.set('from', String(from));
  try{
    const data = await j('/api/events?'+qp.toString());
    // summary
    $('ev-summary').textContent = data.summary.total;
    $('ev-warn').textContent = data.summary.warn;
    $('ev-err').textContent = data.summary.error;
    // merge
    ev.list = data.items;
    if (ev.list.length) ev.since = ev.list[ev.list.length-1].id;
    renderTimeline();
    // devices
    const devs = await j('/api/devices');
    const D = $('devices'); D.innerHTML='';
    devs.items.forEach(d=>{
      const el = document.createElement('div');
      el.className='card';
      el.innerHTML = `<div class="row"><div>${d.name} <span class="small">(${d.id})</span></div>
        <div class="badge">${d.type}</div></div>
        <div class="small">Stanje: ${d.present?(d.state?'ON':'OFF'):'n/p'}</div>
        ${d.ip?('<div class="small">IP: '+d.ip+'</div>'):''}`;
      D.appendChild(el);
    });
  }catch(e){
    $('tl-error').style.display='block';
    console.error(e);
  }
}

$('btn-export-json').onclick = ()=>{
  const url = new URL('/api/events', location.origin);
  url.search = (new URL(location)).search;
  url.searchParams.set('fmt','json');
  window.open(url.toString(), '_blank');
};
$('btn-export-csv').onclick = ()=>{
  const url = new URL('/api/events', location.origin);
  url.search = (new URL(location)).search;
  url.searchParams.set('fmt','csv');
  window.open(url.toString(), '_blank');
};

$('btn-pause').onclick = ()=>{
  ev.paused = !ev.paused;
  $('btn-pause').textContent = ev.paused ? 'Nastavi' : 'Pauza';
};

document.addEventListener('keydown', (e)=>{
  if (e.code==='Space'){
    ev.paused = !ev.paused;
    $('btn-pause').textContent = ev.paused ? 'Nastavi' : 'Pauza';
    e.preventDefault();
  }
});

// SSE
function connectSSE(){
  disconnectSSE();
  if (!$('opt-live').checked) return;
  try{
    ev.sse = new EventSource('/api/events/stream');
    ev.sse.onmessage = (msg)=>{
      try{
        const e = JSON.parse(msg.data);
        const filtersOk =
          (!$('flt-type').value || $('flt-type').value===e.type) &&
          (!$('flt-sev').value || $('flt-sev').value===e.sev) &&
          (!$('flt-src').value || $('flt-src').value===e.source) &&
          (!$('flt-device').value || $('flt-device').value===e.deviceId);
        if (!filtersOk) return;
        ev.list.push(e);
        ev.since = e.id;
        // keep at most 1000 on UI
        if (ev.list.length>1000) ev.list = ev.list.slice(ev.list.length-1000);
        if (!ev.paused) renderTimeline();
      }catch(err){ console.error(err); }
    };
    ev.sse.onerror = ()=>{ console.warn('SSE error, fallback to polling'); disconnectSSE(); setTimeout(reloadEvents, 1500); };
  }catch(e){ console.error(e); }
}
function disconnectSSE(){
  if (ev.sse){ ev.sse.close(); ev.sse = null; }
}

function ensureLoaded(){
  readFiltersFromUrl();
  reloadEvents();
  connectSSE();
}

window.addEventListener('hashchange', ()=>{
  setActive(location.hash==='#events'?'events':'home');
});
setActive(location.hash==='#events'?'events':'home');
startHome();
</script>
</body></html>
)HTML";
}

static void routes(){
  // Health
  server.on("/health", HTTP_GET, [](AsyncWebServerRequest* r){
    if (!ensureAuth(r)) return;
    r->send(200,"text/plain","OK");
  });

  // Diagnostics
  server.on("/diag", HTTP_GET, [](AsyncWebServerRequest* r){
    if (!ensureAuth(r)) return;
    JsonDocument d;
    d["fw"]=FW_VERSION; d["ip"]=net_ip_str(); d["ap"]=net_is_ap();

    Wire.beginTransmission(MCP_LIGHTS_ADDR);
    uint8_t e1 = Wire.endTransmission();
    d["mcp_lights_i2c"] = e1 == 0 ? "OK" : "FAILED";
    Wire.beginTransmission(MCP_OUTLET_PIR);
    uint8_t e2 = Wire.endTransmission();
    d["mcp_outlet_i2c"] = e2 == 0 ? "OK" : "FAILED";
    Wire.beginTransmission(MCP_BUTTONS_ADDR);
    uint8_t e3 = Wire.endTransmission();
    d["mcp_buttons_i2c"] = e3 == 0 ? "OK" : "FAILED";
    String out; serializeJson(d,out); r->send(200,"application/json",out);
  });

  // New diagnostics (unauthenticated) with fixed-size JSON and I2C scan
  server.on("/api/diag", HTTP_GET, [](AsyncWebServerRequest* r){
    StaticJsonDocument<3072> doc;
    doc["uptime_ms"] = millis();
    doc["heap_free"] = ESP.getFreeHeap();
    #ifdef ESP_ARDUINO_VERSION
    doc["heap_min_free"] = ESP.getMinFreeHeap();
    #endif
    doc["sdk_version"] = ESP.getSdkVersion();

    JsonObject wifi = doc["wifi"].to<JsonObject>();
    wifi["connected"] = WiFi.isConnected();
    if (WiFi.isConnected()) {
      wifi["rssi"] = WiFi.RSSI();
      wifi["ip"]   = WiFi.localIP().toString();
    } else {
      wifi["rssi"] = 0;
      wifi["ip"]   = "";
    }

    std::vector<uint8_t> addrs; i2c_scan(addrs);
    JsonArray found = doc["i2c_found"].to<JsonArray>();
    for (auto a : addrs) found.add(a);

    JsonObject mcp = doc["mcp"].to<JsonObject>();
    mcp["lights_present"]      = mcp_lights_available();
    mcp["outlet_pir_present"]  = mcp_outlet_pir_available();

    JsonObject ints = doc["int"].to<JsonObject>();
    ints["pir"]   = digitalRead(INT_PIR_GPIO);
    ints["btn_a"] = digitalRead(INT_BTN_A_GPIO);
    ints["btn_b"] = digitalRead(INT_BTN_B_GPIO);

    JsonObject ev = doc["events"].to<JsonObject>();
    ev["size"]     = Events::size();
    ev["capacity"] = Events::capacity();

    String out; serializeJson(doc, out);
    r->send(200, "application/json", out);
  });

  // Index
  server.on("/", HTTP_GET, [](AsyncWebServerRequest* r){
    if (!ensureAuth(r)) return;
    r->send(200,"text/html",indexHtml());
  });

  // Current system state (enriched)
  server.on("/api/state", HTTP_GET, [](AsyncWebServerRequest* r){
    if (!ensureAuth(r)) return;
    StaticJsonDocument<4096> d;
    d["fw"] = FW_VERSION;
    d["ip"] = net_ip_str();
    d["ap"] = net_is_ap();
    if (net_is_ap()) {
      d["mac"] = WiFi.softAPmacAddress();
      d["gateway"] = "0.0.0.0";
      d["subnet"] = "255.255.255.0";
      d["iface"] = "AP";
      d["method"] = "AP";
    } else if (WiFi.status() == WL_CONNECTED) {
      d["mac"] = WiFi.macAddress();
      d["gateway"] = WiFi.gatewayIP().toString();
      d["subnet"] = WiFi.subnetMask().toString();
      d["iface"] = "Wi‑Fi";
      d["method"] = "Wi‑Fi";
    } else {
      d["mac"] = "";
      d["gateway"] = "";
      d["subnet"] = "";
      d["iface"] = "";
      d["method"] = "";
    }
    auto L = d["lights"].to<JsonArray>();
    for(int i=0;i<16;i++){ bool on,p;
      output_get(ChanType::LIGHT,i,on,p);
      auto o=L.add<JsonObject>();
      o["present"]=p; o["state"]=on; o["name"]=light_name(i);
    }
    auto O = d["outlets"].to<JsonArray>();
    for(int i=0;i<16;i++){ bool on,p;
      output_get(ChanType::OUTLET,i,on,p);
      auto o=O.add<JsonObject>();
      o["present"]=p; o["state"]=on; o["name"]=outlet_name(i);
    }
    sendJson(r, d, 200);
  });

  // Toggle by JSON body
  server.on("/api/toggle", HTTP_POST,
    [](AsyncWebServerRequest* r){ if (!ensureAuth(r)) return; },
    nullptr,
    [](AsyncWebServerRequest* r, uint8_t* data, size_t len, size_t index, size_t total){
      if (!ensureAuth(r)) return;
      if (!data || len==0 || len>1024){ r->send(400,"application/json","{\"error\":\"Invalid body\"}"); return; }
      char* bodyStr = (char*)malloc(len+1); if (!bodyStr){ r->send(500,"application/json","{\"error\":\"OOM\"}"); return; }
      memcpy(bodyStr,data,len); bodyStr[len]='\0';
      JsonDocument doc; auto err = deserializeJson(doc, bodyStr); free(bodyStr);
      if (err){ r->send(400,"application/json","{\"error\":\"Invalid JSON\"}"); return; }
      const char* type = doc["type"]; int idx = doc["index"] | -1;
      if (!type || idx<0 || idx>15){ r->send(400,"application/json","{\"error\":\"Bad params\"}"); return; }

      String corr = String(Events::nextId());
      String target = String(type) + ":" + String(idx);
      Events::logBasic(Events::Type::CMD_SENT, Events::Severity::INFO, "controller", String("toggle -> ")+target, "ui");

      String e;
      if (!toggleChannel(type, idx, e)){
        Events::logCommand("toggle", target, false, "ui", corr, e);
        StaticJsonDocument<256> resp; resp["ok"]=false; resp["error"]=e; sendJson(r,resp,400); return;
      }
      ChanType T = (strcmp(type,"light")==0)?ChanType::LIGHT:ChanType::OUTLET;
      bool on,p; output_get(T, idx, on, p);
      Events::logCommand("toggle", target, true, "ui", corr);
      StaticJsonDocument<256> resp;
      resp["ok"]=true; resp["type"]=type; resp["index"]=idx; resp["state"]=on;
      sendJson(r, resp, 200);
    }
  );

  // Toggle by URL
  server.on("^\\/api\\/toggle\\/([a-z]+)\\/([0-9]+)$", HTTP_GET, [](AsyncWebServerRequest* r){
    if (!ensureAuth(r)) return;
    String type = r->pathArg(0);
    int index = r->pathArg(1).toInt();
    String corr = String(Events::nextId());
    String target = type + ":" + String(index);
    Events::logBasic(Events::Type::CMD_SENT, Events::Severity::INFO, "controller", String("toggle -> ")+target, "ui");
    String err;
    bool ok = toggleChannel(type.c_str(), index, err);
    StaticJsonDocument<256> resp;
    if (!ok){
      Events::logCommand("toggle", target, false, "ui", corr, err);
      resp["ok"]=false; resp["error"]=err; sendJson(r,resp,400); return;
    }
    ChanType T = (type=="light")?ChanType::LIGHT:ChanType::OUTLET;
    bool on,p; output_get(T, index, on, p);
    Events::logCommand("toggle", target, true, "ui", corr);
    resp["ok"]=true; resp["type"]=type; resp["index"]=index; resp["state"]=on;
    sendJson(r, resp, 200);
  });

  // Devices status
  server.on("/api/devices", HTTP_GET, [](AsyncWebServerRequest* r){
    if (!ensureAuth(r)) return;
    if (!checkApiToken(r) || !checkRateLimit(r)) return;
    StaticJsonDocument<4096> d;
    auto items = d["items"].to<JsonArray>();
    fillDevices(items);
    sendJson(r, d, 200);
  });

  // Events list, filters, pagination, export
  server.on("/api/events", HTTP_GET, [](AsyncWebServerRequest* r){
    if (!ensureAuth(r)) return;
    if (!checkApiToken(r) || !checkRateLimit(r)) return;
    Events::Query q;
    auto get = [&](const char* k){ return r->hasParam(k) ? r->getParam(k)->value() : String(); };
    auto toUL = [](const String& s){ return (uint64_t) strtoull(s.c_str(), nullptr, 10); };
    auto toU = [](const String& s){ return (uint32_t) strtoul(s.c_str(), nullptr, 10); };
    q.limit = min((size_t)200, (size_t)(get("limit").length()? toU(get("limit")) : 100));
    q.offset = get("offset").length()? toU(get("offset")) : 0;
    q.since_id = get("since_id").length()? toUL(get("since_id")) : 0;
    q.from_epoch_s = get("from").length()? (time_t)toUL(get("from")) : 0;
    q.to_epoch_s = get("to").length()? (time_t)toUL(get("to")) : 0;
    q.deviceId = get("deviceId");
    q.type = get("type");
    q.severity = get("severity");
    q.source = get("source");
    q.search = get("q");
    String fmt = get("fmt"); // "csv" or "json"

    // If only summary requested (limit=0), skip allocating items
    Events::Summary sum{};
    if (fmt == "csv"){
      // stream CSV
      size_t cap = 256;
      Events::Event* tmp = new Events::Event[cap];
      String csv = Events::csvHeader();
      size_t off=0;
      while(true){
        q.offset = off;
        size_t n = Events::query(q, tmp, cap, &sum);
        for (size_t i=0;i<n;i++){ csv += Events::toCsvRow(tmp[i]); }
        off += n;
        if (n < cap) break;
      }
      delete [] tmp;
      AsyncWebServerResponse* resp = r->beginResponse(200, "text/csv", csv);
      resp->addHeader("Content-Disposition", "attachment; filename=events.csv");
      r->send(resp);
      return;
    }

    StaticJsonDocument<12288> d;
    auto items = d["items"].to<JsonArray>();
    if (q.limit > 0){
      Events::Event* buf = new Events::Event[q.limit];
      size_t n = Events::query(q, buf, q.limit, &sum);
      for (size_t i=0;i<n;i++){
        JsonObject o = items.add<JsonObject>();
        Events::toJson(buf[i], o, false);
      }
      delete [] buf;
    } else {
      // still compute summary
      Events::Event* buf = new Events::Event[64];
      Events::Query qq = q; qq.limit = 64;
      size_t off=0;
      while(true){
        qq.offset = off;
        size_t n = Events::query(qq, buf, 64, &sum);
        off += n;
        if (n < 64) break;
      }
      delete [] buf;
    }
    auto s = d["summary"].to<JsonObject>();
    s["total"]=sum.total; s["info"]=sum.infoCount; s["warn"]=sum.warnCount; s["error"]=sum.errorCount;
    s["latestEpoch"]= (uint64_t) sum.latestEpoch; s["latestId"]= (uint64_t) sum.latestId;
    sendJson(r, d, 200);
  });

  // OTA upload (.bin)
  server.on("/api/ota", HTTP_POST,
    [](AsyncWebServerRequest* r){ if (!ensureAuth(r)) return; r->send(200,"application/json","{\"ok\":true}"); },
    [](AsyncWebServerRequest* r, const String& fn, size_t idx, uint8_t* data, size_t len, bool final){
      if (idx==0){ Update.begin(); }
      Update.write(data,len);
      if (final){ Update.end(true); }
    });

  // SSE handler
  #ifdef ARDUINOJSON_VERSION
  if (g_webAuth.enabled){
    // Best-effort: many builds of AsyncEventSource support setAuthentication
    sse.setAuthentication(g_webAuth.user.c_str(), g_webAuth.pass.c_str());
  }
  server.addHandler(&sse);
  Events::onNew([](const Events::Event& e){
    StaticJsonDocument<512> d; // keep small
    Events::toJson(e, d.to<JsonObject>(), false);
    String payload; serializeJson(d, payload);
    sse.send(payload.c_str(), "event", (uint32_t)e.id, 2000);
  });
  #endif
}

void web_begin(){ routes(); server.begin(); }
void web_loop(){
  // lightweight maintenance
  Events::maintain();
}

#pragma GCC diagnostic pop
#pragma GCC diagnostic pop