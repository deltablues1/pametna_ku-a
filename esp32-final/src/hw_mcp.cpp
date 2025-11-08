#include "hw_mcp.h"
#include <Wire.h>
#include "config.h"

static Adafruit_MCP23X17 mcpLights;
static Adafruit_MCP23X17 mcpOutPir;
static Adafruit_MCP23X17 mcpBtns;
static bool g_lightsReady = false, g_outletReady = false, g_buttonsReady = false;

Adafruit_MCP23X17& MCP_Lights() { return mcpLights; }
Adafruit_MCP23X17& MCP_OutletsPIR() { return mcpOutPir; }
Adafruit_MCP23X17& MCP_Buttons() { return mcpBtns; }
bool mcp_lights_available() { return g_lightsReady; }
bool mcp_outlet_pir_available() { return g_outletReady; }
bool mcp_buttons_available() { return g_buttonsReady; }

void i2c_init_mutex() {}

void mcp_begin_all() {
  // I2C is already initialized in main.cpp
  Serial.println("Initializing MCP23017 devices...");
  
  for (int i = 0; i < 3; ++i) {
    if (!g_lightsReady) {
      Wire.beginTransmission(MCP_LIGHTS_ADDR);
      if (Wire.endTransmission() == 0) {
        Serial.println("Found Lights MCP");
        if (mcpLights.begin_I2C(MCP_LIGHTS_ADDR)) {
          g_lightsReady = true;
          Serial.println("Lights MCP initialized");
        } else {
          Serial.println("Lights MCP begin failed");
        }
      }
    }
    if (!g_outletReady) {
      Wire.beginTransmission(MCP_OUTLET_PIR);
      if (Wire.endTransmission() == 0) {
        Serial.println("Found Outlet/PIR MCP");
        if (mcpOutPir.begin_I2C(MCP_OUTLET_PIR)) {
          g_outletReady = true;
          Serial.println("Outlet/PIR MCP initialized");
        } else {
          Serial.println("Outlet/PIR MCP begin failed");
        }
      }
    }
    if (!g_buttonsReady) {
      Wire.beginTransmission(MCP_BUTTONS_ADDR);
      if (Wire.endTransmission() == 0) {
        Serial.println("Found Buttons MCP");
        if (mcpBtns.begin_I2C(MCP_BUTTONS_ADDR)) {
          g_buttonsReady = true;
          Serial.println("Buttons MCP initialized");
        } else {
          Serial.println("Buttons MCP begin failed");
        }
      }
    }
    delay(100);
  }
  
  // Lights MCP
  if (g_lightsReady) {
    Serial.println("Configuring Lights MCP pins...");
    for (int i = 0; i < 16; ++i) {
      const McpPin& mp = LIGHTS_MCP[i];
      mcpLights.pinMode(mp.pin + (mp.portB ? 8 : 0), OUTPUT);
      mcpLights.digitalWrite(mp.pin + (mp.portB ? 8 : 0), LOW);
    }
  } else {
    Serial.println("Lights MCP not available");
  }
  
  // Outlets/PIR MCP
  if (g_outletReady) {
    Serial.println("Configuring Outlet/PIR MCP pins...");
    for (int i = 0; i < 16; ++i) {
      const OutletPin& op = OUTLET_MCP[i];
      int p = op.pin.pin + (op.pin.portB ? 8 : 0);
      if (op.isInput) {
        mcpOutPir.pinMode(p, INPUT_PULLUP);
      } else {
        mcpOutPir.pinMode(p, OUTPUT);
        mcpOutPir.digitalWrite(p, LOW);
      }
    }
    // PIR interrupts
    mcpOutPir.setupInterrupts(true, false, LOW);
    mcpOutPir.setupInterruptPin(OUTLET_MCP[PIR_VANI_IDX].pin.pin, RISING);
    mcpOutPir.setupInterruptPin(OUTLET_MCP[PIR_ULAZ_IDX].pin.pin, RISING);
  } else {
    Serial.println("Outlet/PIR MCP not available");
  }
  
  // Buttons MCP
  if (g_buttonsReady) {
    Serial.println("Configuring Buttons MCP pins...");
    for (int i = 0; i < 16; ++i) {
      const McpPin& mp = BUTTONS_MCP[i];
      mcpBtns.pinMode(mp.pin + (mp.portB ? 8 : 0), INPUT_PULLUP);
      mcpBtns.setupInterruptPin(mp.pin + (mp.portB ? 8 : 0), CHANGE);
    }
  } else {
    Serial.println("Buttons MCP not available");
  }
  
  Serial.println("MCP initialization complete");
}

void mcp_write(const McpPin& mp, bool high) {
  // Add null pointer checks and additional safety
  if ((mp.addr == MCP_LIGHTS_ADDR && g_lightsReady) ||
      (mp.addr == MCP_OUTLET_PIR && g_outletReady) ||
      (mp.addr == MCP_BUTTONS_ADDR && g_buttonsReady)) {
    Adafruit_MCP23X17* mcp = nullptr;
    if (mp.addr == MCP_LIGHTS_ADDR && g_lightsReady) mcp = &mcpLights;
    else if (mp.addr == MCP_OUTLET_PIR && g_outletReady) mcp = &mcpOutPir;
    else if (mp.addr == MCP_BUTTONS_ADDR && g_buttonsReady) mcp = &mcpBtns;
    
    if (mcp) {
      mcp->digitalWrite(mp.pin + (mp.portB ? 8 : 0), high ? HIGH : LOW);
    }
  }
}

bool mcp_read(const McpPin& mp) {
  // Add null pointer checks and additional safety
  if ((mp.addr == MCP_LIGHTS_ADDR && g_lightsReady) ||
      (mp.addr == MCP_OUTLET_PIR && g_outletReady) ||
      (mp.addr == MCP_BUTTONS_ADDR && g_buttonsReady)) {
    Adafruit_MCP23X17* mcp = nullptr;
    if (mp.addr == MCP_LIGHTS_ADDR && g_lightsReady) mcp = &mcpLights;
    else if (mp.addr == MCP_OUTLET_PIR && g_outletReady) mcp = &mcpOutPir;
    else if (mp.addr == MCP_BUTTONS_ADDR && g_buttonsReady) mcp = &mcpBtns;
    
    if (mcp) {
      return mcp->digitalRead(mp.pin + (mp.portB ? 8 : 0));
    }
  }
  return false;
}
