// =============================================================================
//  protocol.h  -  RC_FPV_SCALER_PRO shared wire protocol  (v2, anti-lag)
// -----------------------------------------------------------------------------
//  INTENTIONALLY identical between the camera and receiver firmwares. Keep both
//  copies byte-for-byte in sync. It defines:
//
//    1) The per-frame telemetry header (FrameHeader, 30 B)
//    2) The UDP video chunk header (ChunkHeader, 20 B) - low-latency transport
//    3) The UDP command tokens (receiver -> camera), incl. TSYNC clock sync
//    4) The UDP status reply format (camera -> receiver)
//
//  PROTOCOL v2 changes (vs v1), all aimed at lower, measurable latency:
//    * frame_id widened 16 -> 32 bit (no ambiguity over long sessions).
//    * Each chunk carries the frame's capture timestamp (capture_ms), so the
//      receiver can compute a real frame_age_ms even if the telemetry datagram
//      is lost. ChunkHeader grew 14 -> 20 bytes.
//    * UDP payload trimmed 1400 -> 1200 B (UDP_PAYLOAD_SIZE) for a safer MTU
//      margin and finer-grained loss (a lost packet kills less of a frame).
//    * TSYNC command added: receiver <-> camera millis() exchange to estimate
//      the clock offset (best/lowest-RTT sample wins) -> honest frame_age_ms.
//
//  TRANSPORT (config.h :: VIDEO_USE_UDP):
//    * 1 (default): UDP chunked. Per frame the camera sends one telemetry
//      datagram (FrameHeader) then N chunk datagrams ([ChunkHeader][payload]).
//      No retransmits, no head-of-line blocking: a lost packet costs one frame,
//      never a stall. Newer frame_id always wins (freshest-frame).
//    * 0: raw-TCP [FrameHeader][JPEG] fallback for very clean links / debug.
//
//  Both targets are little-endian, so we transmit the packed structs as-is.
// =============================================================================
#pragma once
#include <stdint.h>

// ---- Versioning -------------------------------------------------------------
#define FPV_PROTOCOL_VERSION   2          // bumped for the v2 wire format

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
// -----------------------------------------------------------------------------
#pragma pack(push, 1)
typedef struct {
  uint8_t  magic[4];      // FPV_MAGIC_0..3
  uint8_t  version;       // FPV_PROTOCOL_VERSION
  uint8_t  flags;         // FPV_FLAG_*
  uint16_t header_size;   // sizeof(FrameHeader) - lets receiver skip unknowns
  uint32_t frame_id;      // monotonic, 32-bit
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
//  UDP video chunk header (v2). Sent as [ChunkHeader][chunk_len JPEG bytes].
//  A frame of `frame_len` bytes is split into `chunk_count` chunks of
//  UDP_PAYLOAD_SIZE bytes each (the last one is shorter). The receiver scatters
//  each chunk to offset (chunk_id * UDP_PAYLOAD_SIZE), renders when all chunks
//  arrive, and a newer frame_id always wins. `capture_ms` is the camera millis()
//  at capture, replicated in every chunk so frame_age survives a lost telemetry
//  datagram.
// -----------------------------------------------------------------------------
typedef struct {
  uint8_t  magic;         // FPV_UDP_CHUNK_MAGIC
  uint8_t  version;       // FPV_PROTOCOL_VERSION
  uint32_t frame_id;      // 32-bit; "newer" decided by (int32_t) difference
  uint16_t chunk_id;      // 0 .. chunk_count-1
  uint16_t chunk_count;   // total chunks composing this frame
  uint16_t chunk_len;     // JPEG payload bytes carried by THIS datagram
  uint32_t frame_len;     // total JPEG length of the whole frame
  uint32_t capture_ms;    // camera millis() at frame capture
} ChunkHeader;
#pragma pack(pop)

// Compile-time guards: sizes must stay fixed. If you change a struct, update the
// number on BOTH firmwares so a mismatch is caught at build time.
#define FPV_FRAME_HEADER_SIZE 30
#define FPV_CHUNK_HEADER_SIZE 20
#ifdef __cplusplus
static_assert(sizeof(FrameHeader) == FPV_FRAME_HEADER_SIZE,
              "FrameHeader size changed - keep both firmwares in sync");
static_assert(sizeof(ChunkHeader) == FPV_CHUNK_HEADER_SIZE,
              "ChunkHeader size changed - keep both firmwares in sync");
#endif

// -----------------------------------------------------------------------------
//  UDP video subscription token (receiver -> camera, on the video UDP port).
//  The receiver sends this periodically; the camera records the sender's
//  IP:port and unicasts video chunks back to it. If no subscription arrives
//  within VIDEO_SUB_TIMEOUT_MS the camera stops transmitting (saves air time).
// -----------------------------------------------------------------------------
#define FPV_VIDEO_SUBSCRIBE  "VSUB"

// -----------------------------------------------------------------------------
//  UDP command tokens (receiver -> camera, UDP CMD_UDP_PORT). ASCII, '\n'-ended.
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
#define CMD_TSYNC            "TSYNC"            // clock-sync probe -> "TS=<ms>"

// -----------------------------------------------------------------------------
//  UDP replies (camera -> receiver). Single line, '\n'-ended.
//    STATUS: "STATUS rec=.. sd=.. led=.. mpu=.. fps=.. fid=.. q=.. roll=..
//             pitch=.. heap=.. psram=.. up=.. err=.."
//    PING  : "PONG"
//    TSYNC : "TS=<camera_millis>"
// -----------------------------------------------------------------------------
#define STATUS_REPLY_PREFIX  "STATUS"
#define PING_REPLY           "PONG"
#define TSYNC_REPLY_PREFIX   "TS="
