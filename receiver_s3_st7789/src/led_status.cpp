// =============================================================================
//  led_status.cpp  -  onboard WS2812 RGB status indicator (GPIO48)
// -----------------------------------------------------------------------------
//  Uses the ESP32 Arduino core's built-in rgbLedWrite() (RMT driver) so we add
//  no external library. Colour reflects the receiver state; REC pulses in an
//  elegant magenta. Compiles to no-ops when ENABLE_RGB_STATUS_LED == 0, so the
//  firmware runs unchanged on boards without the LED.
//    blue   = connecting / connected (no stream yet)
//    green  = stream live
//    red    = error / reconnecting
//    magenta= recording (pulsing)
//    dim white = idle/boot
// =============================================================================
#include "led_status.h"
#include "config.h"
#include "pins.h"
#include "app_state.h"
#include <Arduino.h>

#if ENABLE_RGB_STATUS_LED

static inline uint8_t scale(uint8_t v) { return (uint16_t)v * RGB_BRIGHTNESS / 255; }

static void setColor(uint8_t r, uint8_t g, uint8_t b) {
  rgbLedWrite(PIN_RGB_LED, scale(r), scale(g), scale(b));
}

void led_begin() {
  setColor(60, 60, 60);   // dim white during boot
}

void led_loop() {
  static uint32_t last = 0;
  uint32_t now = millis();
  if (now - last < 50) return;            // 20 Hz update is plenty
  last = now;

  // Recording overrides everything with a slow magenta pulse.
  if (g_rx.cam_recording) {
    float phase = (now % 1400) / 1400.0f;             // 0..1
    float k = 0.4f + 0.6f * (0.5f * (1 + sinf(phase * 2 * PI)));
    setColor((uint8_t)(255 * k), 0, (uint8_t)(150 * k));
    return;
  }

  switch (g_rx.state) {
    case RxState::LIVE:               setColor(0, 230, 90);  break;  // green
    case RxState::ERROR_RECONNECTING: setColor(255, 30, 30); break;  // red
    case RxState::WIFI_CONNECTING:
    case RxState::CAMERA_DISCOVERY:
    case RxState::STREAM_CONNECTING:  setColor(0, 80, 255);  break;  // blue
    case RxState::MENU:               setColor(0, 200, 255); break;  // cyan
    default:                          setColor(40, 40, 40);  break;  // idle
  }
}

#else  // ENABLE_RGB_STATUS_LED == 0

void led_begin() {}
void led_loop()  {}

#endif
