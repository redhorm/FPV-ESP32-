// =============================================================================
//  protocol.h  -  RC_FPV_SCALER_PRO shared wire protocol
// -----------------------------------------------------------------------------
//  This header is INTENTIONALLY identical between the camera firmware and the
//  receiver firmware. Keep both copies byte-for-byte in sync. It defines:
//
//    1) The low-latency binary video frame header (TCP, port VIDEO_TCP_PORT)
//    2) The UDP command tokens (receiver -> camera)
//    3) The UDP status reply format (camera -> receiver)
//
//  Design goals: tiny, fixed-size, alignment-safe (packed), endianness is
//  little-endian on both ESP32 targets so we transmit raw structs as-is.
// =============================================================================
#pragma once
#include <stdint.h>

// ---- Versioning -------------------------------------------------------------
#define FPV_PROTOCOL_VERSION   1

// 4-byte magic: 'R','F','P','V'  (Rc Fpv Pro Video)
#define FPV_MAGIC_0  0x52
#define FPV_MAGIC_1  0x46
#define FPV_MAGIC_2  0x50
#define FPV_MAGIC_3  0x56

// ---- Frame header flag bits (FrameHeader.flags) -----------------------------
#define FPV_FLAG_RECORDING     (1u << 0)  // SD recording active
#define FPV_FLAG_SD_AVAILABLE  (1u << 1)  // a microSD card is mounted
#define FPV_FLAG_LED_ON        (1u << 2)  // GPIO4 camera LED currently on
#define FPV_FLAG_MPU_AVAILABLE (1u << 3)  // MPU6050 present & initialised
#define FPV_FLAG_KEYFRAME      (1u << 4)  // reserved (every JPEG is standalone)

// ---- Error flag bits (FrameHeader.error_flags / status) ---------------------
#define FPV_ERR_SD_WRITE       (1u << 0)
#define FPV_ERR_CAMERA         (1u << 1)
#define FPV_ERR_LOW_HEAP       (1u << 2)
#define FPV_ERR_MPU            (1u << 3)

// -----------------------------------------------------------------------------
//  Binary video frame header.
//  Layout is fixed and packed; the JPEG payload of `jpeg_length` bytes follows
//  immediately after `header_size` bytes. Telemetry is piggybacked on every
//  frame so the overlay always has fresh data without an extra round-trip.
// -----------------------------------------------------------------------------
#pragma pack(push, 1)
typedef struct {
  uint8_t  magic[4];      // FPV_MAGIC_0..3
  uint8_t  version;       // FPV_PROTOCOL_VERSION
  uint8_t  flags;         // FPV_FLAG_*
  uint16_t header_size;   // sizeof(FrameHeader) - lets receiver skip unknowns
  uint32_t frame_id;      // monotonic, wraps naturally
  uint32_t timestamp_ms;  // camera millis() when frame was captured
  uint32_t jpeg_length;   // bytes of JPEG payload that follow the header

  // --- piggybacked telemetry (small, fixed point to stay compact) ---
  uint8_t  fps_camera_x2; // camera FPS * 2  (0..127.5 fps)
  uint8_t  jpeg_quality;  // current esp_camera quality (lower = better)
  int16_t  roll_ddeg;     // roll  in deci-degrees (deg * 10), 0 if no MPU
  int16_t  pitch_ddeg;    // pitch in deci-degrees (deg * 10), 0 if no MPU
  uint16_t free_heap_kb;  // free internal heap (KiB)
  uint8_t  error_flags;   // FPV_ERR_*
  uint8_t  reserved;      // pad / future use
} FrameHeader;
#pragma pack(pop)

// Compile-time guard: header must stay 30 bytes. If you change the struct,
// update this number on BOTH firmwares so a mismatch is caught at build time.
#define FPV_FRAME_HEADER_SIZE 30
#ifdef __cplusplus
static_assert(sizeof(FrameHeader) == FPV_FRAME_HEADER_SIZE,
              "FrameHeader size changed - keep both firmwares in sync");
#endif

// -----------------------------------------------------------------------------
//  UDP command tokens (receiver -> camera, UDP CMD_UDP_PORT).
//  Plain ASCII tokens terminated by '\n'. Text keeps the protocol trivially
//  debuggable with `nc`/`socat` and adds negligible overhead for control msgs.
//  Optional argument commands use "TOKEN=value".
// -----------------------------------------------------------------------------
#define CMD_REC_TOGGLE       "REC_TOGGLE"
#define CMD_REC_START        "REC_START"
#define CMD_REC_STOP         "REC_STOP"
#define CMD_LED_TOGGLE       "LED_TOGGLE"
#define CMD_PING             "PING"
#define CMD_REQUEST_STATUS   "REQUEST_STATUS"
#define CMD_SET_QUALITY      "SET_QUALITY"      // "SET_QUALITY=10" (4..63)
#define CMD_SET_OVERLAY_MODE "SET_OVERLAY_MODE" // "SET_OVERLAY_MODE=2"

// -----------------------------------------------------------------------------
//  UDP status reply (camera -> receiver). Single line, key=value, '\n' ended.
//  Example:
//  STATUS rec=0 sd=1 led=0 mpu=0 fps=12.0 fid=1234 q=12 roll=0.0 pitch=0.0 \
//         heap=180 up=42 psram=4096 err=0
//  The receiver parses tokens it recognises and ignores the rest (forward
//  compatible). "PONG" is returned for CMD_PING.
// -----------------------------------------------------------------------------
#define STATUS_REPLY_PREFIX  "STATUS"
#define PING_REPLY           "PONG"
