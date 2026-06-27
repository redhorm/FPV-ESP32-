// button_handler.h - debounced BOOT button -> short/long/very-long events
#pragma once
#include <stdint.h>

enum class ButtonEvent : uint8_t {
  NONE,
  SHORT,        // quick tap
  LONG,         // held >= BTN_LONG_MS  (and released before VLONG)
  VERY_LONG,    // held >= BTN_VLONG_MS
};

void        button_begin();
// Poll the button; returns at most one event per call. Fully non-blocking and
// edge/timer based - never delays the loop.
ButtonEvent button_poll();
