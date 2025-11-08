#pragma once
#include <Arduino.h>
#include <Adafruit_MCP23X17.h>
#include "io_map.h"

Adafruit_MCP23X17& MCP_Lights();
Adafruit_MCP23X17& MCP_OutletsPIR();
Adafruit_MCP23X17& MCP_Buttons();
bool mcp_lights_available();
bool mcp_outlet_pir_available();
bool mcp_buttons_available();
void mcp_begin_all();
void mcp_write(const McpPin& mp, bool high);
bool mcp_read(const McpPin& mp);
void i2c_init_mutex();
