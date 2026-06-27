// =============================================================================
//  button_handler.cpp  -  debounced BOOT button, event based, non-blocking
// -----------------------------------------------------------------------------
//  BOOT (GPIO0) is active-LOW with an on-board pull-up. We classify presses by
//  held duration:
//    SHORT      : tap (>= debounce, < LONG)              -> released
//    LONG       : held >= BTN_LONG_MS (< VERY_LONG)      -> released
//    VERY_LONG  : held >= BTN_VLONG_MS                   -> fires WHILE held so
//                 the user gets immediate feedback; the following release is
//                 then swallowed.
//  Everything is millis()-based; the loop is never blocked.
// =============================================================================
#include "button_handler.h"
#include "config.h"
#include "pins.h"
#include <Arduino.h>

static bool     s_down       = false;   // debounced logical state
static bool     s_raw_last   = false;
static uint32_t s_edge_ms    = 0;       // last raw change time (debounce)
static uint32_t s_press_ms   = 0;       // when the (debounced) press began
static bool     s_fired_long = false;   // VERY_LONG already emitted this press

void button_begin() {
  pinMode(PIN_BOOT_BUTTON, INPUT_PULLUP);
  s_down = s_raw_last = false;
}

ButtonEvent button_poll() {
  uint32_t now = millis();
  bool raw_pressed = (digitalRead(PIN_BOOT_BUTTON) == LOW);

  // Debounce: only accept a level that has been stable for BTN_DEBOUNCE_MS.
  if (raw_pressed != s_raw_last) {
    s_raw_last = raw_pressed;
    s_edge_ms  = now;
  }
  bool stable = (now - s_edge_ms) >= BTN_DEBOUNCE_MS;

  ButtonEvent ev = ButtonEvent::NONE;

  if (stable && raw_pressed && !s_down) {
    // Press begins.
    s_down       = true;
    s_press_ms   = now;
    s_fired_long = false;
  } else if (s_down && raw_pressed) {
    // Held: emit VERY_LONG once threshold crossed.
    if (!s_fired_long && (now - s_press_ms) >= BTN_VLONG_MS) {
      s_fired_long = true;
      ev = ButtonEvent::VERY_LONG;
    }
  } else if (stable && !raw_pressed && s_down) {
    // Release.
    uint32_t dur = now - s_press_ms;
    s_down = false;
    if (s_fired_long) {
      ev = ButtonEvent::NONE;            // VERY_LONG already delivered
    } else if (dur >= BTN_LONG_MS) {
      ev = ButtonEvent::LONG;
    } else if (dur >= BTN_DEBOUNCE_MS) {
      ev = ButtonEvent::SHORT;
    }
  }
  return ev;
}
