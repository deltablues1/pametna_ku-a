#pragma once
#include <Arduino.h>
#include <Adafruit_MCP23X17.h>
#include "io_map.h"

Adafruit_MCP23X17& MCP_Lights();       // 0x20
Adafruit_MCP23X17& MCP_OutletsPIR();   // 0x22
Adafruit_MCP23X17& MCP_Buttons();      // 0x27

bool mcp_lights_available();
bool mcp_outlet_pir_available();
bool mcp_buttons_available();

void mcp_begin_all();
void mcp_write(const McpPin& mp, bool high);
bool mcp_read (const McpPin& mp);
