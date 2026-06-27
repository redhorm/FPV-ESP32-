// =============================================================================
//  config.h  -  RC_FPV_SCALER_PRO :: ESP32-CAM firmware configuration
// -----------------------------------------------------------------------------
//  EVERY user-tunable parameter lives here. Source files must not hardcode
//  network, timing, or quality constants - pull them from this file.
// =============================================================================
#pragma once
#include <stdint.h>

// ---------------------------------------------------------------------------
//  Wi-Fi Access Point (the camera is the AP; the receiver is the station)
// ---------------------------------------------------------------------------
#define WIFI_AP_SSID        "RC-FPV-CAM"
#define WIFI_AP_PASSWORD    "12345678"     // >= 8 chars (WPA2 requirement)
#define WIFI_AP_CHANNEL     6              // fixed channel = stabler latency
#define WIFI_AP_MAX_CONN    2              // we only expect one receiver
#define WIFI_AP_HIDDEN      0
// The softAP default gateway/IP for channel-6 AP mode is 192.168.4.1.
// We pin it explicitly so the receiver can hardcode the camera address.
#define WIFI_AP_IP_0        192
#define WIFI_AP_IP_1        168
#define WIFI_AP_IP_2        4
#define WIFI_AP_IP_3        1

// ---------------------------------------------------------------------------
//  Network ports
// ---------------------------------------------------------------------------
#define VIDEO_TCP_PORT      81     // dedicated low-latency JPEG stream
#define CMD_UDP_PORT        82     // receiver -> camera commands, status reply
#define HTTP_DEBUG_PORT     80     // optional /status and /snapshot (debug only)

// ---------------------------------------------------------------------------
//  Camera / video
// ---------------------------------------------------------------------------
// Frame size. The OV2640 supports a native square 240x240, which maps 1:1 to
// the ST7789 with no receiver-side scaling -> lowest latency. If you switch to
// QVGA (320x240) the receiver will centre-crop it.
//   FRAMESIZE_240X240  -> 240x240  (recommended)
//   FRAMESIZE_QVGA     -> 320x240
#define CAM_FRAMESIZE       FRAMESIZE_240X240

// JPEG quality: LOWER number = HIGHER quality & bigger frames. For low latency
// over Wi-Fi we bias toward smaller frames. 12 is a good square-240 default.
#define CAM_JPEG_QUALITY    12     // valid range 4..63

// Frame buffers in PSRAM. 2 + grab "latest" lets the sensor keep capturing
// while we transmit, and we always send the freshest frame (low latency).
// With 1 buffer the sensor stalls until we release it (higher latency).
#define CAM_FB_COUNT        2

// XCLK in Hz. 20 MHz is the reliable default for AI-Thinker OV2640.
#define CAM_XCLK_HZ         20000000

// ---------------------------------------------------------------------------
//  Camera LED (GPIO4)  -  see README "Known issues GPIO4 + microSD"
// ---------------------------------------------------------------------------
//  GPIO4 drives the bright white LED AND is SD_DATA1 in 4-bit SD mode. We mount
//  the SD card in 1-bit mode (below) which frees GPIO4 for the LED, but the LED
//  still stays OFF unless explicitly toggled. Never auto-enable it.
#define ENABLE_CAMERA_LED   true   // allow LED_TOGGLE to act on GPIO4 at all
#define CAMERA_LED_GPIO     4
#define CAMERA_LED_ON_BOOT  false  // MUST stay false (flicker / SD contention)

// ---------------------------------------------------------------------------
//  microSD recording
// ---------------------------------------------------------------------------
#define ENABLE_SD_RECORDING 1      // 0 = compile SD code out entirely
// Mount in 1-bit mode: uses only GPIO2 (D0) + CLK/CMD, leaves GPIO4 free.
// 4-bit mode is faster but collides with the camera LED and is flaky on many
// AI-Thinker clones. 1-bit is the robust choice for this product.
#define SD_ONE_BIT_MODE     true
// Save at most every Nth streamed frame to the card. SD writes are slower than
// the stream; skipping keeps the live view smooth. 2 => record ~half the fps.
#define SD_REC_FRAME_SKIP   2
// If a single SD write takes longer than this, bump the skip dynamically to
// protect streaming latency. Milliseconds.
#define SD_WRITE_SLOW_MS    60
#define SD_ROOT_DIR         "/FPV"

// ---------------------------------------------------------------------------
//  MPU6050 (OPTIONAL oscilloscope / off-road attitude). OFF by default.
//  See README: on the AI-Thinker, free I2C pins are scarce; the MPU is better
//  placed on the receiver. These pins are a PROPOSAL only.
// ---------------------------------------------------------------------------
#define ENABLE_MPU6050      0      // 0 = no I2C, overlay uses demo/none attitude
#define MPU_I2C_SDA         14     // PROPOSAL - verify no conflict on your board
#define MPU_I2C_SCL         15     // PROPOSAL - shared with SD CLK in 4-bit mode!
#define MPU_I2C_FREQ_HZ     400000
// Complementary filter blend (0..1): higher = trust gyro more / smoother.
#define MPU_FILTER_ALPHA    0.98f

// ---------------------------------------------------------------------------
//  Timing / robustness (all non-blocking, millis()-based)
// ---------------------------------------------------------------------------
#define TARGET_FPS              15
#define STREAM_CLIENT_TIMEOUT   3000   // drop idle TCP client after ms
#define TELEMETRY_PERIOD_MS     200    // status push / heap sampling cadence
#define LOW_HEAP_WARN_KB        30     // raise FPV_ERR_LOW_HEAP below this

// ---------------------------------------------------------------------------
//  Serial debug. 0=silent 1=errors 2=info 3=verbose
// ---------------------------------------------------------------------------
#define DEBUG_LEVEL         2
#define SERIAL_BAUD         115200

#define LOGE(fmt, ...) do { if (DEBUG_LEVEL >= 1) Serial.printf("[E] " fmt "\n", ##__VA_ARGS__); } while (0)
#define LOGI(fmt, ...) do { if (DEBUG_LEVEL >= 2) Serial.printf("[I] " fmt "\n", ##__VA_ARGS__); } while (0)
#define LOGV(fmt, ...) do { if (DEBUG_LEVEL >= 3) Serial.printf("[V] " fmt "\n", ##__VA_ARGS__); } while (0)
