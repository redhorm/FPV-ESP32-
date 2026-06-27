// =============================================================================
//  protocol.h  -  RC_FPV_SCALER_PRO shared wire protocol
// -----------------------------------------------------------------------------
//  This header is INTENTIONALLY identical between the camera firmware and the
//  receiver firmware. Keep both copies byte-for-byte in sync. It defines:
//
//    1) The per-frame telemetry header (FrameHeader)
//    2) The UDP video chunk header (ChunkHeader) - low-latency transport
//    3) The UDP command tokens (receiver -> camera)
//    4) The UDP status reply format (camera -> receiver)
//
//  TRANSPORT (selectable in config.h via VIDEO_USE_UDP):
//    * VIDEO_USE_UDP = 1 (default, recommended): video travels as UDP datagrams.
//      Each JPEG frame is split into chunks (ChunkHeader + payload) and blasted
//      unicast to the subscribed receiver. A small telemetry datagram (the
//      FrameHeader below) is sent once per frame just before its chunks. UDP has
//      no head-of-line blocking and no retransmit stalls, so a lost packet costs
//      at most one frame instead of freezing the stream -> higher FPS / lower,
//      more consistent latency on real Wi-Fi links (this is what the reference
//      rc-fpv-esp32 design uses to reach ~18 fps).
//    * VIDEO_USE_UDP = 0 (fallback): the original raw-TCP stream, where each
//      frame is [FrameHeader][JPEG] over a single TCP socket. Kept as a robust
//      fallback for very clean links or debugging.
//
//  Both targets are little-endian, so we transmit the packed structs as-is.
// =============================================================================
#pragma once
#include <stdint.h>

// ---- Versioning -------------------------------------------------------------
#define FPV_PROTOCOL_VERSION   1

// 4-byte magic for the telemetry/TCP FrameHeader: 'R','F','P','V'.
#define FPV_MAGIC_0  0x52
#define FPV_MAGIC_1  0x46
#define FPV_MAGIC_2  0x50
#define FPV_MAGIC_3  0x56

// 1-byte magic for a UDP video chunk. Deliberately != FPV_MAGIC_0 (0x52='R') so
// the receiver can tell a telemetry datagram from a video chunk by byte 0 alone.
#define FPV_UDP_CHUNK_MAGIC    0xA5

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
//  Per-frame telemetry header.
//  * UDP mode: sent as a standalone datagram once per frame (no JPEG payload).
//  * TCP mode: immediately followed by `jpeg_length` JPEG bytes.
//  Telemetry is piggybacked so the overlay always has fresh data without an
//  extra round-trip. Layout is fixed and packed.
// -----------------------------------------------------------------------------
#pragma pack(push, 1)
typedef struct {
  uint8_t  magic[4];      // FPV_MAGIC_0..3
  uint8_t  version;       // FPV_PROTOCOL_VERSION
  uint8_t  flags;         // FPV_FLAG_*
  uint16_t header_size;   // sizeof(FrameHeader) - lets receiver skip unknowns
  uint32_t frame_id;      // monotonic, wraps naturally
  uint32_t timestamp_ms;  // camera millis() when frame was captured
  uint32_t jpeg_length;   // bytes of JPEG payload for this frame

  // --- piggybacked telemetry (small, fixed point to stay compact) ---
  uint8_t  fps_camera_x2; // camera FPS * 2  (0..127.5 fps)
  uint8_t  jpeg_quality;  // current esp_camera quality (lower = better)
  int16_t  roll_ddeg;     // roll  in deci-degrees (deg * 10), 0 if no MPU
  int16_t  pitch_ddeg;    // pitch in deci-degrees (deg * 10), 0 if no MPU
  uint16_t free_heap_kb;  // free internal heap (KiB)
  uint8_t  error_flags;   // FPV_ERR_*
  uint8_t  reserved;      // pad / future use
} FrameHeader;

// -----------------------------------------------------------------------------
//  UDP video chunk header. Sent as [ChunkHeader][chunk_len JPEG bytes]. A frame
//  of `frame_len` bytes is split into `chunk_count` chunks of UDP_CHUNK_PAYLOAD
//  bytes each (the last one is shorter). The receiver scatters each chunk into a
//  reassembly buffer at offset (chunk_id * UDP_CHUNK_PAYLOAD) and renders the
//  frame once all chunks have arrived. A newer frame_id always wins (freshest
//  frame), so a lost chunk costs exactly one frame, never a stall.
// -----------------------------------------------------------------------------
typedef struct {
  uint8_t  magic;         // FPV_UDP_CHUNK_MAGIC
  uint8_t  version;       // FPV_PROTOCOL_VERSION
  uint16_t frame_id;      // wraps; "newer" decided by (int16_t) difference
  uint16_t chunk_id;      // 0 .. chunk_count-1
  uint16_t chunk_count;   // total chunks composing this frame
  uint16_t chunk_len;     // JPEG payload bytes carried by THIS datagram
  uint32_t frame_len;     // total JPEG length of the whole frame
} ChunkHeader;
#pragma pack(pop)

// Compile-time guards: sizes must stay fixed. If you change a struct, update the
// number on BOTH firmwares so a mismatch is caught at build time.
#define FPV_FRAME_HEADER_SIZE 30
#define FPV_CHUNK_HEADER_SIZE 14
#ifdef __cplusplus
static_assert(sizeof(FrameHeader) == FPV_FRAME_HEADER_SIZE,
              "FrameHeader size changed - keep both firmwares in sync");
static_assert(sizeof(ChunkHeader) == FPV_CHUNK_HEADER_SIZE,
              "ChunkHeader size changed - keep both firmwares in sync");
#endif

// -----------------------------------------------------------------------------
//  UDP video subscription token (receiver -> camera, on the video UDP port).
//  The receiver sends this periodically; the camera records the sender's
//  IP:port and unicasts the video chunks back to it. If no subscription arrives
//  within VIDEO_SUB_TIMEOUT_MS the camera stops transmitting (saves air time).
// -----------------------------------------------------------------------------
#define FPV_VIDEO_SUBSCRIBE  "VSUB"

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
//  Example: STATUS rec=0 sd=1 led=0 mpu=0 fps=12.0 fid=1234 q=12 roll=0.0
//           pitch=0.0 heap=180 up=42 psram=4096 err=0
//  The receiver parses tokens it recognises and ignores the rest (forward
//  compatible). "PONG" is returned for CMD_PING.
// -----------------------------------------------------------------------------
#define STATUS_REPLY_PREFIX  "STATUS"
#define PING_REPLY           "PONG"
