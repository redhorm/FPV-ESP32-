// =============================================================================
//  config.h  -  RC_FPV_SCALER_PRO :: ESP32-S3 receiver configuration
// -----------------------------------------------------------------------------
//  All user-tunable parameters for the receiver live here.
//
//  ANTI-LAG PROFILE: set FPV_MODE_TURBO the SAME value as the camera.
//    1 = TURBO  : minimal overlay, fast splash, 60 MHz SPI, 55 ms frame timeout.
//    0 = QUALITY: full overlay, full splash, 40 MHz SPI.
//
//  DISPLAY VARIANT: two build envs share this code (see platformio.ini):
//    receiver_s3      -> 240x240 panel (DISPLAY_PANEL_280 = 0)
//    receiver_s3_280  -> 240x280 panel (DISPLAY_PANEL_280 = 1, 20px GRAM offset)
// =============================================================================
#pragma once
#include <stdint.h>

// ===========================================================================
//  ANTI-LAG MODE
// ===========================================================================
#define FPV_MODE_TURBO      1          // 1 = TURBO (min latency), 0 = QUALITY

// ===========================================================================
//  DISPLAY VARIANT  (overridable from platformio.ini with -DDISPLAY_PANEL_280=1)
// ===========================================================================
#ifndef DISPLAY_PANEL_280
  #define DISPLAY_PANEL_280  1         // 0 = 240x240, 1 = 240x280
#endif

// ---------------------------------------------------------------------------
//  Wi-Fi (the receiver is a STATION joining the camera's AP)
// ---------------------------------------------------------------------------
#define WIFI_SSID           "RC-FPV-CAM"
#define WIFI_PASSWORD       "12345678"
#define CAMERA_IP           "192.168.4.1"   // fixed AP IP of the camera
#define VIDEO_TCP_PORT      81              // raw-TCP video  (when VIDEO_USE_UDP=0)
#define VIDEO_UDP_PORT      81              // UDP chunked video (when VIDEO_USE_UDP=1)
#define CMD_UDP_PORT        82

// ---------------------------------------------------------------------------
//  Video transport (protocol v2)  -  MUST match the camera's config.h
// ---------------------------------------------------------------------------
#define VIDEO_USE_UDP       1
// MUST equal the camera's UDP_PAYLOAD_SIZE (used for reassembly offsets).
#define UDP_PAYLOAD_SIZE    1200
// Largest JPEG we accept (sizes the reassembly buffers). Match the camera.
#define MAX_JPEG_SIZE       32768
// How often the receiver re-subscribes so the camera keeps unicasting to it.
#define VIDEO_SUB_INTERVAL_MS 500
// Abandon (count as dropped) a partial frame older than this. Short = fresh.
#if FPV_MODE_TURBO
  #define FRAME_TIMEOUT_MS  55
#else
  #define FRAME_TIMEOUT_MS  80
#endif
// Clock-sync (TSYNC) cadence: how often we probe the camera's millis().
#define TSYNC_INTERVAL_MS   1500

// ---------------------------------------------------------------------------
//  Display geometry
// ---------------------------------------------------------------------------
#define DISPLAY_W           240
#if DISPLAY_PANEL_280
  #define DISPLAY_H         280
  #define PANEL_GRAM_H      320      // 240x280 panels live in a 240x320 GRAM
  #define PANEL_OFFSET_Y    20       // ...offset 20px down (common 1.69" module)
#else
  #define DISPLAY_H         240
  #define PANEL_GRAM_H      240
  #define PANEL_OFFSET_Y    0
#endif
#define DISPLAY_ROTATION    0
// SPI clock. TURBO pushes 60 MHz for shorter blit time; QUALITY stays at 40 MHz.
#if FPV_MODE_TURBO
  #define DISPLAY_SPI_HZ    60000000
#else
  #define DISPLAY_SPI_HZ    40000000
#endif

// The live video is always 240x240, centred vertically on the panel.
#define VIDEO_W             240
#define VIDEO_H             240
#define VIDEO_Y0            ((DISPLAY_H - VIDEO_H) / 2)   // 0 on 240, 20 on 280

// Camera source frame size (MUST match CAM_FRAMESIZE). 240x240 default.
#define VIDEO_SRC_W         240
#define VIDEO_SRC_H         240

// ---------------------------------------------------------------------------
//  Video client / latency policy
// ---------------------------------------------------------------------------
#define VIDEO_RX_BUFFER     MAX_JPEG_SIZE  // reassembly buffer size
#define VIDEO_CONNECT_TMO   2000           // TCP connect timeout (ms, TCP mode)
#define VIDEO_STALL_TMO     1200           // no frame for this long -> reconnect
#define WIFI_CONNECT_TMO    15000          // join AP within this long, else retry

// ---------------------------------------------------------------------------
//  Overlay / UI
// ---------------------------------------------------------------------------
#define OVERLAY_MINIMAL     0
#define OVERLAY_FULL        1
#define OVERLAY_OFFROAD     2
// TURBO defaults to a minimal HUD (less to draw per frame); QUALITY to full.
#if FPV_MODE_TURBO
  #define OVERLAY_MODE_DEFAULT OVERLAY_MINIMAL
#else
  #define OVERLAY_MODE_DEFAULT OVERLAY_FULL
#endif

// Overlay text refresh period (ms). Drawing every video frame flickers and
// wastes the bus; ~5-10 Hz is smooth and readable.
#define OVERLAY_REFRESH_MS  150            // ~6.7 Hz
#define REC_BLINK_MS        700

// Boot splash duration. TURBO shows a short splash to get to video fast.
#if FPV_MODE_TURBO
  #define SPLASH_MS         300
#else
  #define SPLASH_MS         3000
#endif

// ---------------------------------------------------------------------------
//  Button timing (ms) - see button_handler.cpp
// ---------------------------------------------------------------------------
#define BTN_DEBOUNCE_MS     25
#define BTN_LONG_MS         500
#define BTN_VLONG_MS        1500

// ---------------------------------------------------------------------------
//  RGB status LED (GPIO48). Set to 0 if your board lacks it or it misbehaves.
// ---------------------------------------------------------------------------
#define ENABLE_RGB_LED      1
// (FPV_RGB_LEVEL avoids colliding with the ESP32 core's RGB_BRIGHTNESS macro.)
#define FPV_RGB_LEVEL       40             // 0..255, keep low - these LEDs are bright

// ---------------------------------------------------------------------------
//  Optional MPU6050 mirror flag (attitude comes from the camera telemetry).
// ---------------------------------------------------------------------------
#define ENABLE_MPU6050      0

// ---------------------------------------------------------------------------
//  Telemetry polling (System Info / PERF). The live overlay uses the telemetry
//  piggybacked on each video frame; this UDP poll is only for richer info.
// ---------------------------------------------------------------------------
#define STATUS_POLL_MS      1000

// ---------------------------------------------------------------------------
//  Firmware identity (shown on boot screen)
// ---------------------------------------------------------------------------
#define FW_NAME             "RC FPV SCALER PRO"
#define FW_VERSION          "2.0.0"

// ---------------------------------------------------------------------------
//  Serial debug. ENABLE_DEBUG_SERIAL=0 compiles out ALL logging.
// ---------------------------------------------------------------------------
#define ENABLE_DEBUG_SERIAL 1
#define DEBUG_LEVEL         2
#define SERIAL_BAUD         115200

#if ENABLE_DEBUG_SERIAL
  #define LOGE(fmt, ...) do { if (DEBUG_LEVEL >= 1) Serial.printf("[E] " fmt "\n", ##__VA_ARGS__); } while (0)
  #define LOGI(fmt, ...) do { if (DEBUG_LEVEL >= 2) Serial.printf("[I] " fmt "\n", ##__VA_ARGS__); } while (0)
  #define LOGV(fmt, ...) do { if (DEBUG_LEVEL >= 3) Serial.printf("[V] " fmt "\n", ##__VA_ARGS__); } while (0)
#else
  #define LOGE(fmt, ...) do {} while (0)
  #define LOGI(fmt, ...) do {} while (0)
  #define LOGV(fmt, ...) do {} while (0)
#endif
