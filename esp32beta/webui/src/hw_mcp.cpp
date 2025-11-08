#include "hw_mcp.h"
#include "config.h"
#include <Wire.h>

static Adafruit_MCP23X17 mcpLights;
static Adafruit_MCP23X17 mcpOutPir;
static Adafruit_MCP23X17 mcpBtns;

static bool g_lightsReady = false;
static bool g_outletReady = false;
static bool g_buttonsReady = false;
static bool g_warnLights = false;
static bool g_warnOutlets = false;
static bool g_warnButtons = false;

Adafruit_MCP23X17& MCP_Lights(){ return mcpLights; }
Adafruit_MCP23X17& MCP_OutletsPIR(){ return mcpOutPir; }
Adafruit_MCP23X17& MCP_Buttons(){ return mcpBtns; }

bool mcp_lights_available(){ return g_lightsReady; }
bool mcp_outlet_pir_available(){ return g_outletReady; }
bool mcp_buttons_available(){ return g_buttonsReady; }

static Adafruit_MCP23X17* ensureDevice(uint8_t addr, const char*& label){
  switch(addr){
    case MCP_LIGHTS_ADDR:
      label = "Lights MCP";
      if (!g_lightsReady){
        if (!g_warnLights){
          Serial.println("[MCP] WARN: Lights MCP unavailable, operating virtually");
          g_warnLights = true;
        }
        return nullptr;
      }
      return &mcpLights;
    case MCP_OUTLET_PIR:
      label = "Outlets/PIR MCP";
      if (!g_outletReady){
        if (!g_warnOutlets){
          Serial.println("[MCP] WARN: Outlets/PIR MCP unavailable, operating virtually");
          g_warnOutlets = true;
        }
        return nullptr;
      }
      return &mcpOutPir;
    case MCP_BUTTONS_ADDR:
      label = "Buttons MCP";
      if (!g_buttonsReady){
        if (!g_warnButtons){
          Serial.println("[MCP] WARN: Buttons MCP unavailable, operating virtually");
          g_warnButtons = true;
        }
        return nullptr;
      }
      return &mcpBtns;
    default:
      label = "Unknown MCP";
      Serial.printf("[MCP] ERROR: Unknown MCP address 0x%02X\n", addr);
      return nullptr;
  }
}

void mcp_begin_all(){
  g_lightsReady = g_outletReady = g_buttonsReady = false;
  g_warnLights = g_warnOutlets = g_warnButtons = false;

  Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN, I2C_FREQ_HZ);
  Wire.setTimeOut(I2C_TIMEOUTMS);

  Serial.println("[MCP] Testing I2C communication with MCP23017 chips...");
  for (int attempt = 1; attempt <= I2C_RETRIES; ++attempt){
    Serial.printf("[MCP] Initialization attempt %d/%d\n", attempt, I2C_RETRIES);

    Wire.beginTransmission(MCP_LIGHTS_ADDR);
    uint8_t errLights = Wire.endTransmission();
    Serial.printf("[MCP] Lights MCP (0x%02X) I2C test: %s\n", MCP_LIGHTS_ADDR, errLights == 0 ? "OK" : "FAILED");
    if (errLights == 0 && !g_lightsReady){
      g_lightsReady = mcpLights.begin_I2C(MCP_LIGHTS_ADDR);
    }
    Wire.beginTransmission(MCP_OUTLET_PIR);
    uint8_t errOut = Wire.endTransmission();
    Serial.printf("[MCP] Outlet/PIR MCP (0x%02X) I2C test: %s\n", MCP_OUTLET_PIR, errOut == 0 ? "OK" : "FAILED");
    if (errOut == 0 && !g_outletReady){
      g_outletReady = mcpOutPir.begin_I2C(MCP_OUTLET_PIR);
    }
    Wire.beginTransmission(MCP_BUTTONS_ADDR);
    uint8_t errBtn = Wire.endTransmission();
    Serial.printf("[MCP] Buttons MCP (0x%02X) I2C test: %s\n", MCP_BUTTONS_ADDR, errBtn == 0 ? "OK" : "FAILED");
    if (errBtn == 0 && !g_buttonsReady){
      g_buttonsReady = mcpBtns.begin_I2C(MCP_BUTTONS_ADDR);
    }
    if (g_lightsReady && g_outletReady && g_buttonsReady){
      Serial.println("[MCP] All MCP23017 chips responding on I2C, proceeding with initialization...");
      break;
    }

    delay(100);
  }
  if (!g_lightsReady || !g_outletReady || !g_buttonsReady){
    Serial.println("[MCP] Warning: some MCP expanders were not detected. Missing devices will be simulated.");
    if (!g_lightsReady) {
      Serial.println("[MCP] -> Lights MCP missing (0x20)");
    }
    if (!g_outletReady) {
      Serial.println("[MCP] -> Outlets/PIR MCP missing (0x22)");
    }
    if (!g_buttonsReady) {
      Serial.println("[MCP] -> Buttons MCP missing (0x27)");
    }
  }
  if (g_lightsReady){
    for (int i=0;i<16;i++){
      const auto& p = LIGHTS_MCP[i];
      uint8_t pin = p.pin + (p.portB ? 8 : 0);
      mcpLights.pinMode(pin, OUTPUT);
      mcpLights.digitalWrite(pin, LOW);
    }
  }
  if (g_outletReady){
    for (int i=0;i<16;i++){
      const auto& op = OUTLET_MCP[i];
      uint8_t pin = op.pin.pin + (op.pin.portB ? 8 : 0);
      if (op.isInput){
        mcpOutPir.pinMode(pin, INPUT_PULLUP);
      } else {
        mcpOutPir.pinMode(pin, OUTPUT);
        mcpOutPir.digitalWrite(pin, LOW);
      }
    }
    mcpOutPir.setupInterrupts(true, false, LOW);
    for (int idx : {PIR_VANI_IDX, PIR_ULAZ_IDX}){
      const auto& op = OUTLET_MCP[idx];
      uint8_t pin = op.pin.pin + (op.pin.portB ? 8 : 0);
      mcpOutPir.setupInterruptPin(pin, CHANGE);
    }
    pinMode(INT_PIR_GPIO, INPUT);
  } else {
    pinMode(INT_PIR_GPIO, INPUT_PULLUP);
  }
  if (g_buttonsReady){
    for (int i=0;i<16;i++){
      const auto& p = BUTTONS_MCP[i];
      uint8_t pin = p.pin + (p.portB ? 8 : 0);
      mcpBtns.pinMode(pin, INPUT_PULLUP);
    }
    mcpBtns.setupInterrupts(true, false, LOW);
    for (int i=0;i<16;i++){
      const auto& p = BUTTONS_MCP[i];
      uint8_t pin = p.pin + (p.portB ? 8 : 0);
      mcpBtns.setupInterruptPin(pin, CHANGE);
    }
    pinMode(INT_BTN_A_GPIO, INPUT);
    pinMode(INT_BTN_B_GPIO, INPUT);
  } else {
    pinMode(INT_BTN_A_GPIO, INPUT_PULLUP);
    pinMode(INT_BTN_B_GPIO, INPUT_PULLUP);
  }
}

void mcp_write(const McpPin& mp, bool high){
  const char* label = "";
  Adafruit_MCP23X17* dev = ensureDevice(mp.addr, label);
  if (!dev) return;

  uint8_t pin = mp.pin + (mp.portB ? 8 : 0);
  Serial.printf("[MCP] Writing %s -> addr 0x%02X pin %d (%c) = %s\n",
                label, mp.addr, pin, mp.portB ? 'B' : 'A', high ? "HIGH" : "LOW");
  dev->digitalWrite(pin, high);
}

bool mcp_read(const McpPin& mp){
  const char* label = "";
  Adafruit_MCP23X17* dev = ensureDevice(mp.addr, label);
  if (!dev) return false;

  uint8_t pin = mp.pin + (mp.portB ? 8 : 0);
  return dev->digitalRead(pin);
}

