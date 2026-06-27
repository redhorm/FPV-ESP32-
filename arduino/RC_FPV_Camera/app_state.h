// =============================================================================
//  app_state.h  -  shared runtime state for the camera firmware
// -----------------------------------------------------------------------------
//  A single, deliberately small struct that every module reads/writes. This is
//  simpler and more predictable than scattering globals across translation
//  units, and keeps the telemetry/header builders trivial. Single-core access
//  pattern: everything runs in loop() on the Arduino core, so no locking is
//  required. The `recording` flag is volatile for clarity / future task use.
// =============================================================================
#pragma once
#include <stdint.h>
#include "protocol.h"   // FPV_ERR_* / FPV_FLAG_* bits used in error_flags below

enum class CamState : uint8_t {
  BOOT,
  AP_READY,
  STREAMING,
  RECORDING,
  SD_ERROR,
  CAMERA_ERROR,
};

struct AppStatus {
  CamState state          = CamState::BOOT;

  volatile bool recording = false;
  bool sd_available       = false;
  bool led_on             = false;
  bool mpu_available      = false;

  uint8_t  jpeg_quality   = 0;     // mirrors current sensor quality
  uint8_t  overlay_mode   = 0;     // forwarded to receiver if it asks
  float    fps_camera     = 0.0f;
  uint32_t frame_id       = 0;
  uint32_t frame_capture_ms = 0;   // millis() captured this frame (TSYNC clock)
  uint32_t last_jpeg_len  = 0;     // size of the most recent frame (bytes)
  uint32_t rec_dropped    = 0;     // recording frames dropped (SD writer busy)

  float    roll           = 0.0f;  // degrees (0 when no MPU)
  float    pitch          = 0.0f;  // degrees

  uint8_t  error_flags    = 0;

  uint32_t rec_index      = 0;     // current REC_xxxx folder index
  uint32_t rec_frame_count= 0;     // frames written in current session

  uint32_t free_heap      = 0;     // bytes
  uint32_t free_psram     = 0;     // bytes
  uint32_t uptime_s       = 0;
};

// Defined in main.cpp
extern AppStatus g_status;
