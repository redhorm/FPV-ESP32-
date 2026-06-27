// =============================================================================
//  RC_FPV_SCALER_PRO  -  ESP32-S3 receiver firmware (ST7789 FPV monitor)
// -----------------------------------------------------------------------------
//  Orchestrates the receiver state machine and the per-loop pipeline:
//    buttons -> wifi -> telemetry/LED -> (menu | error | live video+overlay)
//  Everything is non-blocking and millis()-based. The hot path is:
//    video_loop() gives us the freshest JPEG -> jpeg_draw() -> overlay_draw().
//
//  Button mapping (per spec):
//    LIVE: short = open Quick Menu | long = REC toggle | very-long = LED toggle
//    MENU: short = next item | long = select | very-long = back/close
// =============================================================================
#include <Arduino.h>

#include "config.h"
#include "app_state.h"
#include "display.h"
#include "wifi_client.h"
#include "video_client.h"
#include "jpeg_renderer.h"
#include "ui_overlay.h"
#include "ui_menu.h"
#include "button_handler.h"
#include "led_status.h"
#include "telemetry_client.h"
#include "protocol.h"

RxStatus g_rx;   // single shared state instance

static RxState s_prev_state = RxState::BOOT;

// -----------------------------------------------------------------------------
static void handleButtons() {
  ButtonEvent ev = button_poll();
  if (ev == ButtonEvent::NONE) return;

  if (menu_is_active()) {
    menu_handle_button(ev);
    return;
  }

  // Live-view shortcuts.
  switch (ev) {
    case ButtonEvent::SHORT:     menu_open(); break;
    case ButtonEvent::LONG:      telemetry_send(CMD_REC_TOGGLE); break;
    case ButtonEvent::VERY_LONG: telemetry_send(CMD_LED_TOGGLE); break;
    default: break;
  }
}

// -----------------------------------------------------------------------------
void setup() {
  Serial.begin(SERIAL_BAUD);
  delay(50);
  LOGI("\n=== %s v%s :: ESP32-S3 receiver boot ===", FW_NAME, FW_VERSION);

  display_begin();
  led_begin();
  button_begin();

  g_rx.state = RxState::BOOT;
  menu_boot_screen(SPLASH_MS);   // animated neon-skull splash (short in TURBO)

  wifi_begin();
  video_begin();
  telemetry_begin();

  g_rx.state = RxState::WIFI_CONNECTING;
}

// -----------------------------------------------------------------------------
void loop() {
  uint32_t now = millis();
  g_rx.uptime_s = now / 1000;

  // --- always-on services ---
  handleButtons();
  wifi_loop();
  telemetry_loop();
  led_loop();

  // Pump the video socket every loop (even in MENU) so backlog is drained and
  // reconnection logic keeps running; we just don't render while in the menu.
  bool new_frame = false;
  if (g_rx.wifi_connected) new_frame = video_loop();

  // ----------------- state resolution -----------------
  if (menu_is_active()) {
    g_rx.state = RxState::MENU;
  } else if (!g_rx.wifi_connected) {
    g_rx.state = RxState::ERROR_RECONNECTING;
    g_rx.error = RxError::WIFI_LOST;
  } else if (!video_is_connected()) {
    g_rx.state = RxState::ERROR_RECONNECTING;
    g_rx.error = RxError::STREAM_LOST;
  } else {
    if (s_prev_state != RxState::LIVE) overlay_invalidate();  // fresh repaint
    g_rx.state = RxState::LIVE;
    g_rx.error = RxError::NONE;
  }

  // ----------------- per-state rendering -----------------
  switch (g_rx.state) {
    case RxState::MENU:
      menu_draw();
      break;

    case RxState::ERROR_RECONNECTING:
      menu_draw_error();
      break;

    case RxState::LIVE:
      if (new_frame) {
        const uint8_t* d = video_frame_data();
        uint32_t       n = video_frame_len();

        uint32_t t0 = micros();
        bool ok = jpeg_draw(d, n);          // LovyanGFX decodes + DMA-blits in one
        uint32_t dt = micros() - t0;

        if (ok) {
          g_rx.decode_draw_us = dt;
          overlay_ingest(video_frame_header(), now);

          // Honest frame age: capture instant (camera clock) -> now, where
          // now is mapped into the camera clock via the TSYNC offset.
          uint32_t cap     = video_frame_capture_ms();
          uint32_t cam_now = (uint32_t)((int32_t)millis() + g_rx.clock_offset);
          int32_t  age     = (int32_t)(cam_now - cap);
          g_rx.frame_age_ms = (age > 0 && age < 60000) ? (uint32_t)age : 0;
          g_rx.latency_ms   = g_rx.frame_age_ms;

          // drawn-FPS over a real 1 s window
          static uint32_t draw_win = 0, draw_cnt = 0;
          if (draw_win == 0) draw_win = now;
          draw_cnt++;
          uint32_t wd = now - draw_win;
          if (wd >= 1000) { g_rx.fps_video = draw_cnt * 1000.0f / (float)wd;
                            draw_win = now; draw_cnt = 0; }
          g_rx.frames_drawn++;
        } else {
          g_rx.jpeg_err++; g_rx.frames_dropped++;
          g_rx.error = RxError::JPEG_DECODE;   // transient; we stay LIVE
        }
      }
      // Always refresh the overlay (cheap, DMA) so REC blink etc. animate even
      // between video frames.
      overlay_draw(false);
      break;

    default:
      break;
  }

  s_prev_state = g_rx.state;

  // No fixed delay: the loop is paced by the Wi-Fi/video I/O above. A tiny
  // yield keeps the IDLE/WDT tasks fed without adding latency.
  yield();
}
