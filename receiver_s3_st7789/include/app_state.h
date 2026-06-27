// =============================================================================
//  app_state.h  -  shared runtime state for the receiver firmware
// =============================================================================
#pragma once
#include <stdint.h>
#include "config.h"

// High-level receiver state machine (see main.cpp).
enum class RxState : uint8_t {
  BOOT,
  WIFI_CONNECTING,
  CAMERA_DISCOVERY,
  STREAM_CONNECTING,
  LIVE,
  MENU,
  ERROR_RECONNECTING,
};

// Which error to show on the ERROR screen.
enum class RxError : uint8_t {
  NONE,
  WIFI_LOST,
  STREAM_LOST,
  CAMERA_NOT_FOUND,
  JPEG_DECODE,
  SD_MISSING,
};

struct RxStatus {
  RxState  state        = RxState::BOOT;
  RxError  error        = RxError::NONE;

  // --- link / video stats (measured locally) ---
  bool     wifi_connected = false;
  int8_t   rssi           = 0;       // dBm
  float    fps_video      = 0.0f;    // frames actually drawn / s
  uint32_t latency_ms     = 0;       // estimated (now - frame timestamp - offset)
  uint32_t frames_drawn   = 0;
  uint32_t frames_dropped = 0;       // newer frame arrived before old drawn / decode fail

  // --- telemetry mirrored from the camera (frame header + UDP status) ---
  bool     cam_recording  = false;
  bool     cam_sd_ok      = false;
  bool     cam_led_on     = false;
  bool     cam_mpu_ok     = false;
  uint8_t  cam_quality    = 0;
  float    cam_fps        = 0.0f;
  uint32_t cam_frame_id   = 0;
  float    roll           = 0.0f;    // degrees
  float    pitch          = 0.0f;
  uint16_t cam_heap_kb    = 0;
  uint16_t cam_psram_kb   = 0;
  uint32_t cam_uptime_s   = 0;
  uint8_t  cam_error      = 0;

  // --- local UI ---
  uint8_t  overlay_mode   = OVERLAY_MODE_DEFAULT;
  uint8_t  quality_pref   = 1;       // 0=low 1=med 2=high (maps to SET_QUALITY)
  uint32_t uptime_s       = 0;
};

extern RxStatus g_rx;
