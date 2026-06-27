// =============================================================================
//  config.h  -  RC_FPV_SCALER_PRO :: ESP32-S3 receiver configuration
// -----------------------------------------------------------------------------
//  All user-tunable parameters for the receiver live here.
// =============================================================================
#pragma once
#include <stdint.h>

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
//  Video transport  -  MUST match the camera's config.h
// ---------------------------------------------------------------------------
//  1 = UDP chunked stream (DEFAULT): reassemble chunks, always render the
//      freshest complete frame, drop partial/late frames -> low latency.
//  0 = original raw-TCP stream (fallback).
#define VIDEO_USE_UDP       1
// MUST equal the camera's UDP_CHUNK_PAYLOAD (used for reassembly offsets).
#define UDP_CHUNK_PAYLOAD   1400
// How often the receiver re-subscribes so the camera keeps unicasting to it.
#define VIDEO_SUB_INTERVAL_MS 500
// Abandon (count as dropped) a partially-received frame older than this, so a
// lost chunk can never wedge the reassembler. Milliseconds.
#define FRAME_ASSEMBLY_TMO  120

// ---------------------------------------------------------------------------
//  Display
// ---------------------------------------------------------------------------
#define DISPLAY_W           240
#define DISPLAY_H           240
// Rotation 0..3. ST7789 240x240 is square; pick the orientation that matches
// your enclosure. Backlight is driven on TFT_BL.
#define DISPLAY_ROTATION    0
#define DISPLAY_SPI_HZ      40000000   // 40 MHz is safe; try 80 MHz if stable

// Source frame size produced by the camera. MUST match CAM_FRAMESIZE on the
// camera. Default is the native square 240x240 (no crop needed). If you switch
// the camera to QVGA, set 320x240 here and the renderer centre-crops to 240.
#define VIDEO_SRC_W         240
#define VIDEO_SRC_H         240

// ---------------------------------------------------------------------------
//  Video client / latency policy
// ---------------------------------------------------------------------------
// If we are still decoding/drawing when a newer frame arrives we DROP the older
// one rather than queueing it. The socket read also discards backlog so we
// always converge to the freshest frame -> lowest latency.
#define VIDEO_RX_BUFFER     32768      // max JPEG size we accept (bytes)
#define VIDEO_CONNECT_TMO   2000       // TCP connect timeout (ms)
#define VIDEO_STALL_TMO     1500       // no frame for this long -> reconnect (ms)
#define WIFI_CONNECT_TMO    15000      // join AP within this long, else retry

// ---------------------------------------------------------------------------
//  Overlay / UI
// ---------------------------------------------------------------------------
// Overlay modes (also requestable from the camera via SET_OVERLAY_MODE).
#define OVERLAY_MINIMAL     0
#define OVERLAY_FULL        1
#define OVERLAY_OFFROAD     2
#define OVERLAY_MODE_DEFAULT OVERLAY_FULL

// Numeric/text overlay refresh rate. Drawing every video frame causes flicker
// and wastes the bus; 5 Hz is smooth and readable.
#define OVERLAY_REFRESH_HZ  5
// REC indicator blink period (ms) - elegant slow blink, not aggressive.
#define REC_BLINK_MS        700

// ---------------------------------------------------------------------------
//  Button timing (ms) - see button_handler.cpp
// ---------------------------------------------------------------------------
#define BTN_DEBOUNCE_MS     25
#define BTN_LONG_MS         500
#define BTN_VLONG_MS        1500

// ---------------------------------------------------------------------------
//  RGB status LED (GPIO48). Set to 0 if your board lacks it or it misbehaves.
// ---------------------------------------------------------------------------
#define ENABLE_RGB_STATUS_LED 1
#define RGB_BRIGHTNESS        40    // 0..255, keep low - these LEDs are bright

// ---------------------------------------------------------------------------
//  Telemetry polling (menu System Info). The live overlay uses the telemetry
//  piggybacked on each video frame; this UDP poll is only for richer info.
// ---------------------------------------------------------------------------
#define STATUS_POLL_MS      1000

// ---------------------------------------------------------------------------
//  Firmware identity (shown on boot screen)
// ---------------------------------------------------------------------------
#define FW_NAME             "RC FPV SCALER PRO"
#define FW_VERSION          "1.0.0"

// ---------------------------------------------------------------------------
//  Serial debug. 0=silent 1=errors 2=info 3=verbose
// ---------------------------------------------------------------------------
#define DEBUG_LEVEL         2
#define SERIAL_BAUD         115200

#define LOGE(fmt, ...) do { if (DEBUG_LEVEL >= 1) Serial.printf("[E] " fmt "\n", ##__VA_ARGS__); } while (0)
#define LOGI(fmt, ...) do { if (DEBUG_LEVEL >= 2) Serial.printf("[I] " fmt "\n", ##__VA_ARGS__); } while (0)
#define LOGV(fmt, ...) do { if (DEBUG_LEVEL >= 3) Serial.printf("[V] " fmt "\n", ##__VA_ARGS__); } while (0)
