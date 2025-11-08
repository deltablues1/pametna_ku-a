#pragma once
#include <Arduino.h>
#include "config.h"

// Jednostavan "descriptor" MCP pina
struct McpPin {
  uint8_t addr;
  uint8_t pin;
  bool portB;
};

// Nazivi kanala (HR) za svjetla
constexpr const char* LIGHT_LABELS[16] = {
  "svjetlo_vani",        "svjetlo_terasa1",   "svjetlo_terasa2",   "svjetlo_ulaz",
  "svjetlo_hidrofor",    "svjetlo_tv",        "svjetlo_fotelja",   "svjetlo_stup",
  "svjetlo_boravak",     "svjetlo_blagavaona","svjetlo_kuhinja",   "svjetlo_sank",
  "svjetlo_hodnik",      "svjetlo_kupaona",   "svjetlo_soba1",     "svjetlo_soba2"
};

inline const char* io_light_label(int idx){
  return (idx >= 0 && idx < 16) ? LIGHT_LABELS[idx] : "";
}

// ===== Lights - MCP #1 @0x20 =====
// Index -> hrvatski -> engleski u komentarima
static const McpPin LIGHTS_MCP[16] = {
  {MCP_LIGHTS_ADDR, 0, false}, // 0 svjetlo_vani         / Outdoor Light
  {MCP_LIGHTS_ADDR, 1, false}, // 1 svjetlo_terasa1      / Terrace Light 1
  {MCP_LIGHTS_ADDR, 2, false}, // 2 svjetlo_terasa2      / Terrace Light 2
  {MCP_LIGHTS_ADDR, 3, false}, // 3 svjetlo_ulaz         / Entrance Light
  {MCP_LIGHTS_ADDR, 4, false}, // 4 svjetlo_hidrofor     / Water Pump Light
  {MCP_LIGHTS_ADDR, 5, false}, // 5 svjetlo_tv           / TV Area Light
  {MCP_LIGHTS_ADDR, 6, false}, // 6 svjetlo_fotelja      / Armchair Light
  {MCP_LIGHTS_ADDR, 7, false}, // 7 svjetlo_stup         / Pillar Light
  {MCP_LIGHTS_ADDR, 0, true }, // 8 svjetlo_boravak      / Living Room Light
  {MCP_LIGHTS_ADDR, 1, true }, // 9 svjetlo_blagavaona   / Dining Room Light
  {MCP_LIGHTS_ADDR, 2, true }, //10 svjetlo_kuhinja      / Kitchen Light
  {MCP_LIGHTS_ADDR, 3, true }, //11 svjetlo_sank         / Bathroom Sink Light
  {MCP_LIGHTS_ADDR, 4, true }, //12 svjetlo_hodnik       / Hallway Light
  {MCP_LIGHTS_ADDR, 5, true }, //13 svjetlo_kupaona      / Bathroom Light
  {MCP_LIGHTS_ADDR, 6, true }, //14 svjetlo_soba1        / Room 1 Light
  {MCP_LIGHTS_ADDR, 7, true }  //15 svjetlo_soba2        / Room 2 Light
};

// ===== Outlets & PIR - MCP #2 @0x22 =====
struct OutletPin {
  McpPin pin;
  const char* nameHR;
  bool isInput; // isInput samo za PIR
};

static const OutletPin OUTLET_MCP[16] = {
  {{MCP_OUTLET_PIR, 0,false}, "uticnica_ulaz",      false}, // 0
  {{MCP_OUTLET_PIR, 1,false}, "uticnica_kuhinja",   false}, // 1
  {{MCP_OUTLET_PIR, 2,false}, "uticnica_frizider",  false}, // 2
  {{MCP_OUTLET_PIR, 3,false}, "uticnica_kupaona",   false}, // 3
  {{MCP_OUTLET_PIR, 4,false}, "uticnica_bojler",    false}, // 4
  {{MCP_OUTLET_PIR, 5,false}, "uticnica_terasa",    false}, // 5
  {{MCP_OUTLET_PIR, 6,false}, "pir_vani",           true }, // 6  (INPUT)
  {{MCP_OUTLET_PIR, 7,false}, "pir_ulaz",           true }, // 7  (INPUT)
  {{MCP_OUTLET_PIR, 0,true }, "uticnica_tv",        false}, // 8
  {{MCP_OUTLET_PIR, 1,true }, "uticnica_boravak",   false}, // 9
  {{MCP_OUTLET_PIR, 2,true }, "uticnica_blagavaona",false}, //10
  {{MCP_OUTLET_PIR, 3,true }, "pecnica",            false}, //11
  {{MCP_OUTLET_PIR, 4,true }, "uticnica_soba1",     false}, //12
  {{MCP_OUTLET_PIR, 5,true }, "uticnica_soba2",     false}, //13
  {{MCP_OUTLET_PIR, 6,true }, "slobodno1",          false}, //14
  {{MCP_OUTLET_PIR, 7,true }, "slobodno2",          false}  //15
};

inline const char* io_outlet_label(int idx){
  return (idx >= 0 && idx < 16) ? OUTLET_MCP[idx].nameHR : "";
}

// Indeksi PIR-a u gornjoj tablici:
#define PIR_VANI_IDX 6  // PA6
#define PIR_ULAZ_IDX 7  // PA7

// ===== Buttons - MCP #3 @0x27 =====
static const McpPin BUTTONS_MCP[16] = {
  {MCP_BUTTONS_ADDR, 0,false}, {MCP_BUTTONS_ADDR, 1,false},
  {MCP_BUTTONS_ADDR, 2,false}, {MCP_BUTTONS_ADDR, 3,false},
  {MCP_BUTTONS_ADDR, 4,false}, {MCP_BUTTONS_ADDR, 5,false},
  {MCP_BUTTONS_ADDR, 6,false}, {MCP_BUTTONS_ADDR, 7,false},
  {MCP_BUTTONS_ADDR, 0,true }, {MCP_BUTTONS_ADDR, 1,true },
  {MCP_BUTTONS_ADDR, 2,true }, {MCP_BUTTONS_ADDR, 3,true },
  {MCP_BUTTONS_ADDR, 4,true }, {MCP_BUTTONS_ADDR, 5,true },
  {MCP_BUTTONS_ADDR, 6,true }, {MCP_BUTTONS_ADDR, 7,true }
};

