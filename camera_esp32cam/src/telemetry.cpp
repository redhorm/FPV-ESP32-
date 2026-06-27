// =============================================================================
//  telemetry.cpp  -  FPS estimator, frame-header & status builders
// =============================================================================
#include "telemetry.h"
#include "config.h"
#include "app_state.h"
#include <Arduino.h>
#include <esp_heap_caps.h>

// --- FPS estimator: exponential moving average over inter-frame periods ------
static uint32_t s_last_frame_ms = 0;
static float    s_fps_ema       = 0.0f;

void telemetry_on_frame() {
  uint32_t now = millis();
  if (s_last_frame_ms != 0) {
    uint32_t dt = now - s_last_frame_ms;
    if (dt > 0) {
      float inst = 1000.0f / (float)dt;
      s_fps_ema = (s_fps_ema == 0.0f) ? inst : (s_fps_ema * 0.9f + inst * 0.1f);
      g_status.fps_camera = s_fps_ema;
    }
  }
  s_last_frame_ms = now;
}

// --- Periodic system sampling ------------------------------------------------
void telemetry_loop() {
  static uint32_t last = 0;
  uint32_t now = millis();
  if (now - last < TELEMETRY_PERIOD_MS) return;
  last = now;

  g_status.free_heap  = ESP.getFreeHeap();
  g_status.free_psram = ESP.getPsramSize() ? ESP.getFreePsram() : 0;
  g_status.uptime_s   = now / 1000;

  // Low-heap warning flag (cleared automatically when heap recovers).
  if ((g_status.free_heap / 1024) < LOW_HEAP_WARN_KB)
    g_status.error_flags |=  FPV_ERR_LOW_HEAP;
  else
    g_status.error_flags &= ~FPV_ERR_LOW_HEAP;
}

// --- Header builder ----------------------------------------------------------
void telemetry_fill_header(FrameHeader* h, uint32_t jpeg_len) {
  h->magic[0] = FPV_MAGIC_0; h->magic[1] = FPV_MAGIC_1;
  h->magic[2] = FPV_MAGIC_2; h->magic[3] = FPV_MAGIC_3;
  h->version     = FPV_PROTOCOL_VERSION;
  h->header_size = sizeof(FrameHeader);
  h->frame_id    = g_status.frame_id;
  h->timestamp_ms= millis();
  h->jpeg_length = jpeg_len;

  uint8_t flags = 0;
  if (g_status.recording)     flags |= FPV_FLAG_RECORDING;
  if (g_status.sd_available)  flags |= FPV_FLAG_SD_AVAILABLE;
  if (g_status.led_on)        flags |= FPV_FLAG_LED_ON;
  if (g_status.mpu_available) flags |= FPV_FLAG_MPU_AVAILABLE;
  flags |= FPV_FLAG_KEYFRAME; // every JPEG is independently decodable
  h->flags = flags;

  float fps2 = g_status.fps_camera * 2.0f;
  if (fps2 < 0) fps2 = 0; if (fps2 > 255) fps2 = 255;
  h->fps_camera_x2 = (uint8_t)fps2;
  h->jpeg_quality  = g_status.jpeg_quality;
  h->roll_ddeg     = (int16_t)(g_status.roll  * 10.0f);
  h->pitch_ddeg    = (int16_t)(g_status.pitch * 10.0f);
  uint32_t heap_kb = g_status.free_heap / 1024;
  h->free_heap_kb  = (heap_kb > 0xFFFF) ? 0xFFFF : (uint16_t)heap_kb;
  h->error_flags   = g_status.error_flags;
  h->reserved      = 0;
}

// --- Status line builder -----------------------------------------------------
int telemetry_build_status(char* out, int out_size) {
  return snprintf(out, out_size,
    STATUS_REPLY_PREFIX
    " rec=%d sd=%d led=%d mpu=%d fps=%.1f fid=%lu q=%d"
    " roll=%.1f pitch=%.1f heap=%lu psram=%lu up=%lu err=%d\n",
    g_status.recording ? 1 : 0,
    g_status.sd_available ? 1 : 0,
    g_status.led_on ? 1 : 0,
    g_status.mpu_available ? 1 : 0,
    g_status.fps_camera,
    (unsigned long)g_status.frame_id,
    g_status.jpeg_quality,
    g_status.roll, g_status.pitch,
    (unsigned long)(g_status.free_heap / 1024),
    (unsigned long)(g_status.free_psram / 1024),
    (unsigned long)g_status.uptime_s,
    g_status.error_flags);
}
