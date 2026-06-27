// =============================================================================
//  config.h  -  RC_FPV_SCALER_PRO :: ESP32-CAM firmware configuration
// -----------------------------------------------------------------------------
//  EVERY user-tunable parameter lives here. Source files must not hardcode
//  network, timing, or quality constants - pull them from this file.
//
//  ANTI-LAG PROFILES (set FPV_MODE_TURBO the SAME on camera and receiver):
//    FPV_MODE_TURBO = 1  -> lowest latency: JPEG q=20, recording blocked,
//                           minimal overlay/splash on the receiver.
//    FPV_MODE_TURBO = 0  -> QUALITY: JPEG q=12, SD recording allowed, full UI.
// =============================================================================
#pragma once
#include <stdint.h>

// ===========================================================================
//  ANTI-LAG MODE  (the one switch that picks the whole profile)
// ===========================================================================
#define FPV_MODE_TURBO      1          // 1 = TURBO (min latency), 0 = QUALITY

// JPEG quality per profile (LOWER number = better image & BIGGER frames).
// Bigger frames = more UDP chunks = more chances a lost packet kills a frame,
// so TURBO uses a higher number (smaller frames, lower latency).
#define JPEG_QUALITY_TURBO    20
#define JPEG_QUALITY_QUALITY  12

// ---------------------------------------------------------------------------
//  Wi-Fi Access Point (the camera is the AP; the receiver is the station)
// ---------------------------------------------------------------------------
#define WIFI_AP_SSID        "RC-FPV-CAM"
#define WIFI_AP_PASSWORD    "12345678"     // >= 8 chars (WPA2 requirement)
#define WIFI_AP_CHANNEL     6              // fixed channel = stabler latency
#define WIFI_AP_MAX_CONN    2              // we only expect one receiver
#define WIFI_AP_HIDDEN      0
// The softAP default gateway/IP for channel-6 AP mode is 192.168.4.1.
#define WIFI_AP_IP_0        192
#define WIFI_AP_IP_1        168
#define WIFI_AP_IP_2        4
#define WIFI_AP_IP_3        1

// ---------------------------------------------------------------------------
//  Network ports
// ---------------------------------------------------------------------------
#define VIDEO_TCP_PORT      81     // raw-TCP JPEG stream (used when VIDEO_USE_UDP=0)
#define VIDEO_UDP_PORT      81     // UDP chunked video  (used when VIDEO_USE_UDP=1)
#define CMD_UDP_PORT        82     // receiver -> camera commands, status, TSYNC
#define HTTP_DEBUG_PORT     80     // optional /status and /snapshot (debug only)

// ---------------------------------------------------------------------------
//  Video transport (protocol v2)  -  see protocol.h for the rationale
// ---------------------------------------------------------------------------
//  1 = UDP chunked stream (DEFAULT): no head-of-line blocking, no retransmit
//      stalls -> higher FPS / lower, steadier latency (targets ~15-18 fps).
//  0 = raw-TCP fallback for very clean links / debugging.
#define VIDEO_USE_UDP        1
// JPEG bytes per UDP datagram. 1200 keeps a safe margin under the Wi-Fi MTU so
// the IP layer never fragments (fragmentation kills loss resilience), and makes
// per-packet loss cost less of a frame.
#define UDP_PAYLOAD_SIZE     1200
// Largest JPEG we will ever send/accept. Sizes the reassembly buffers. A
// 240x240 frame at q=12..20 is a few KB, so 32 KB is generous.
#define MAX_JPEG_SIZE        32768
// Optional pacing between chunks (microseconds). 0 = blast (lowest latency).
// Raise to 50-150 only if a busy channel shows heavy per-frame loss.
#define UDP_CHUNK_GAP_US     0
// Stop transmitting if the receiver has not (re)subscribed within this long.
#define VIDEO_SUB_TIMEOUT_MS 2500

// ---------------------------------------------------------------------------
//  Camera / video
// ---------------------------------------------------------------------------
//   FRAMESIZE_240X240  -> 240x240  (recommended, maps 1:1 to the panel)
//   FRAMESIZE_QVGA     -> 320x240  (receiver centre-crops)
#define CAM_FRAMESIZE       FRAMESIZE_240X240

// JPEG quality is chosen by the active profile (see top of file).
#if FPV_MODE_TURBO
  #define CAM_JPEG_QUALITY  JPEG_QUALITY_TURBO
#else
  #define CAM_JPEG_QUALITY  JPEG_QUALITY_QUALITY
#endif

// 2 buffers + grab "latest" = continuous capture, always the freshest frame.
#define CAM_FB_COUNT        2
// XCLK in Hz. 20 MHz is the reliable default for AI-Thinker OV2640.
#define CAM_XCLK_HZ         20000000

// ---------------------------------------------------------------------------
//  Camera LED (GPIO4)  -  see README "Known issues GPIO4 + microSD"
// ---------------------------------------------------------------------------
#define ENABLE_CAMERA_LED   true   // allow LED_TOGGLE to act on GPIO4 at all
#define CAMERA_LED_GPIO     4
#define CAMERA_LED_ON_BOOT  false  // MUST stay false (flicker / SD contention)

// ---------------------------------------------------------------------------
//  microSD recording (ASYNCHRONOUS - never on the video path)
// ---------------------------------------------------------------------------
#define ENABLE_SD_RECORDING 1      // 0 = compile SD code out entirely
// TURBO blocks recording outright so nothing competes with the live stream.
#if FPV_MODE_TURBO
  #define SD_RECORDING_ALLOWED 0
#else
  #define SD_RECORDING_ALLOWED 1
#endif
#define SD_ONE_BIT_MODE     true   // frees GPIO4, robust on cheap clones
#define SD_ROOT_DIR         "/FPV"
// The recorder runs in its OWN low-priority FreeRTOS task with a single
// PSRAM slot. The video loop only memcpy's the latest frame into the slot and
// moves on; if the writer is still busy, the recording frame is DROPPED (the
// live stream is never delayed by the card).
#define SD_WRITER_CORE      0          // keep SD off the Arduino/loop core (1)
#define SD_WRITER_PRIO      1          // below the loop task -> never preempts video
#define SD_WRITER_STACK     4096
#define SD_REC_FRAME_SKIP   2          // offer at most every Nth frame to the slot

// ---------------------------------------------------------------------------
//  MPU6050 (OPTIONAL). OFF by default. See README "How to add an MPU6050".
// ---------------------------------------------------------------------------
#define ENABLE_MPU6050      0
#define MPU_I2C_SDA         14     // PROPOSAL - verify no conflict on your board
#define MPU_I2C_SCL         15     // PROPOSAL - shared with SD CLK in 4-bit mode!
#define MPU_I2C_FREQ_HZ     400000
#define MPU_FILTER_ALPHA    0.98f

// ---------------------------------------------------------------------------
//  Timing / robustness (all non-blocking, millis()-based)
// ---------------------------------------------------------------------------
#define TARGET_FPS              18
#define STREAM_CLIENT_TIMEOUT   3000   // drop idle TCP client after ms (TCP mode)
#define TELEMETRY_PERIOD_MS     200    // heap sampling cadence
#define FPS_WINDOW_MS           1000   // real-window FPS counter period
#define LOW_HEAP_WARN_KB        30     // raise FPV_ERR_LOW_HEAP below this

// ---------------------------------------------------------------------------
//  Serial debug. ENABLE_DEBUG_SERIAL=0 compiles out ALL logging (zero overhead
//  in the hot path). Otherwise DEBUG_LEVEL gates it: 1=errors 2=info 3=verbose.
//  NEVER use LOGI/LOGV per video frame.
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
