#include "buttons.h"
#include "io_map.h"
#include "hw_mcp.h"
#include "config.h"

static ButtonHandler g_cb = nullptr;
static volatile bool g_intA = false, g_intB = false;
struct Btn { bool stable = 1; uint32_t lastEdge = 0, pressStart = 0; } B[16];

void IRAM_ATTR isrBtnA() { g_intA = true; }
void IRAM_ATTR isrBtnB() { g_intB = true; }

void buttons_begin(ButtonHandler cb) {
  g_cb = cb;
  if (!mcp_buttons_available()) return;
  attachInterrupt(digitalPinToInterrupt(INT_BTN_A_GPIO), isrBtnA, FALLING);
  attachInterrupt(digitalPinToInterrupt(INT_BTN_B_GPIO), isrBtnB, FALLING);
}

void buttons_loop(uint32_t nowMs) {
  if (g_intA || g_intB) {
    g_intA = g_intB = false;
    for (int i = 0; i < 16; ++i) {
      bool level = mcp_read(BUTTONS_MCP[i]);
      if (level != B[i].stable && nowMs - B[i].lastEdge > BTN_DEBOUNCE_MS) {
        B[i].lastEdge = nowMs;
        B[i].stable = level;
        if (!level) B[i].pressStart = nowMs;
        else {
          uint32_t dur = nowMs - B[i].pressStart;
          if (dur >= BTN_LONG_MS) {
            if (g_cb) g_cb(-1, true); // All lights OFF
          } else if (dur >= BTN_DEBOUNCE_MS) {
            if (g_cb) g_cb(i, false); // Toggle light
          }
        }
      }
    }
  }
}
