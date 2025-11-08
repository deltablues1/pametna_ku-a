#include "buttons.h"
#include "io_map.h"
#include "hw_mcp.h"
#include "config.h"

static ButtonHandler g_cb=nullptr;
static volatile bool g_intA=false, g_intB=false;

static void IRAM_ATTR isrBtnA(){ g_intA=true; }
static void IRAM_ATTR isrBtnB(){ g_intB=true; }

struct Btn { bool stable=1; uint32_t lastEdge=0, pressStart=0; };
static Btn B[16];

void buttons_begin(ButtonHandler cb){
  g_cb = cb;
  if (!mcp_buttons_available()){
    Serial.println("[BUTTONS] MCP buttons expander not available; button input disabled");
    return;
  }
  attachInterrupt(digitalPinToInterrupt(INT_BTN_A_GPIO), isrBtnA, FALLING);
  attachInterrupt(digitalPinToInterrupt(INT_BTN_B_GPIO), isrBtnB, FALLING);
}
static void handleEdge(int idx, bool level, uint32_t now){
  // INPUT_PULLUP -> pritisak=LOW(0), otpust=HIGH(1)
  if (level==0){ B[idx].pressStart=now; }
  else {
    const uint32_t dur = now - B[idx].pressStart;
    if (dur >= BTN_LONG_MS){
      // Long: sva svjetla OFF
      if (g_cb) g_cb(-1, true);
    } else if (dur >= BTN_DEBOUNCE_MS) {
      // Short press: toggle mapped light
      if (g_cb) g_cb(idx, false);
    }
    // < BTN_DEBOUNCE_MS -> ignore (debounce)
  }
}

void buttons_loop(uint32_t now){
  if (!mcp_buttons_available()) return;
  // Ako je do??ao prekid s bilo kojeg INT pina ??? o??itaj koji je pin izazvao
  if (g_intA || g_intB){
    // Pro??itaj MCP interrupt capture registre
    auto &m = MCP_Buttons();
    uint8_t intfA = m.readGPIO(0); // trenutni A
    uint8_t intfB = m.readGPIO(1); // trenutni B
    // Pro??i sve
    for (int i=0;i<16;i++){
      bool level = mcp_read(BUTTONS_MCP[i]); // 1=idle
      if (level!=B[i].stable){
        if ((now - B[i].lastEdge) >= BTN_DEBOUNCE_MS){
          B[i].stable = level;
          B[i].lastEdge = now;
          handleEdge(i, level, now);
        }
      }
    }
    g_intA=false; g_intB=false;
  }
}
