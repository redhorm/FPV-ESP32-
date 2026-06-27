// =============================================================================
//  video_client.cpp  -  low-latency JPEG video receiver (the heart of the FPV)
// -----------------------------------------------------------------------------
//  Two selectable transports (config.h :: VIDEO_USE_UDP), same public API so
//  main.cpp never changes. Both implement the same latency policy: always render
//  the FRESHEST complete frame and drop anything older.
//
//   VIDEO_USE_UDP = 1  (DEFAULT) -- UDP chunked receiver
//     * Periodically sends FPV_VIDEO_SUBSCRIBE so the camera unicasts to us.
//     * Reassembles [ChunkHeader][payload] datagrams into whole JPEG frames.
//     * Freshest-frame-wins: a newer frame_id abandons any partial older frame;
//       a partial frame older than FRAME_ASSEMBLY_TMO is dropped. A separate
//       assembly buffer guarantees a just-completed frame is never corrupted by
//       a later partial one while we drain the socket.
//
//   VIDEO_USE_UDP = 0  -- raw-TCP receiver (original)
//     * Reads [FrameHeader][JPEG] units, draining the socket to the newest.
// =============================================================================
#include "video_client.h"
#include "config.h"
#include "app_state.h"
#include <Arduino.h>
#include <WiFi.h>
#include <WiFiUdp.h>
#include <string.h>

// =============================================================================
#if VIDEO_USE_UDP
// =============================================================================
//  UDP chunked transport
// -----------------------------------------------------------------------------
static const int MAXCH = (VIDEO_RX_BUFFER + UDP_CHUNK_PAYLOAD - 1) / UDP_CHUNK_PAYLOAD;

static WiFiUDP    s_vudp;
static uint8_t    s_buf[VIDEO_RX_BUFFER];   // last COMPLETE frame (what we render)
static uint8_t    s_asm[VIDEO_RX_BUFFER];   // scatter buffer for the frame in flight
static uint8_t    s_got[MAXCH];             // which chunk_ids we have this frame
static uint8_t    s_pkt[FPV_CHUNK_HEADER_SIZE + UDP_CHUNK_PAYLOAD];
static FrameHeader s_hdr;                    // latest telemetry (from telemetry datagrams)

static uint32_t   s_frame_len = 0;          // length of the frame in s_buf
static bool       s_have_frame = false;

// assembly state (frame in flight)
static bool       s_asm_active = false;
static uint16_t   s_asm_id = 0, s_asm_count = 0, s_asm_recv = 0;
static uint32_t   s_asm_len = 0, s_asm_start_ms = 0;

static uint32_t   s_last_frame_ms = 0;
static uint32_t   s_last_sub_ms   = 0;
static bool       s_ever_frame    = false;

static void sendSubscribe() {
  IPAddress ip;
  ip.fromString(CAMERA_IP);
  s_vudp.beginPacket(ip, VIDEO_UDP_PORT);
  s_vudp.write((const uint8_t*)FPV_VIDEO_SUBSCRIBE, strlen(FPV_VIDEO_SUBSCRIBE));
  s_vudp.endPacket();
  s_last_sub_ms = millis();
}

static void asmReset(uint16_t id, uint16_t count, uint32_t flen) {
  s_asm_active   = true;
  s_asm_id       = id;
  s_asm_count    = count;
  s_asm_len      = flen;
  s_asm_recv     = 0;
  s_asm_start_ms = millis();
  memset(s_got, 0, sizeof(s_got));
}

// Process one chunk datagram. Returns true if a COMPLETE frame is now in s_buf.
static bool onChunk(const uint8_t* p, int n) {
  if (n < (int)sizeof(ChunkHeader)) return false;
  ChunkHeader h;
  memcpy(&h, p, sizeof(h));

  if (h.magic != FPV_UDP_CHUNK_MAGIC || h.version != FPV_PROTOCOL_VERSION) return false;
  if (h.chunk_count == 0 || h.chunk_count > MAXCH)            return false;
  if (h.frame_len == 0   || h.frame_len > VIDEO_RX_BUFFER)    return false;
  if (h.chunk_id >= h.chunk_count)                            return false;
  if ((int)(sizeof(ChunkHeader) + h.chunk_len) > n)           return false; // truncated

  // Freshest-frame-wins.
  if (!s_asm_active) {
    asmReset(h.frame_id, h.chunk_count, h.frame_len);
  } else if (h.frame_id != s_asm_id) {
    int16_t diff = (int16_t)(h.frame_id - s_asm_id);   // wrap-safe "newer?"
    if (diff > 0) {
      if (s_asm_recv < s_asm_count) g_rx.frames_dropped++;   // abandoned partial
      asmReset(h.frame_id, h.chunk_count, h.frame_len);
    } else {
      return false;   // an older frame's straggler -> ignore
    }
  }

  uint32_t off = (uint32_t)h.chunk_id * UDP_CHUNK_PAYLOAD;
  if (off + h.chunk_len > VIDEO_RX_BUFFER) return false;

  if (!s_got[h.chunk_id]) {                 // ignore duplicates
    memcpy(s_asm + off, p + sizeof(ChunkHeader), h.chunk_len);
    s_got[h.chunk_id] = 1;
    s_asm_recv++;
  }

  if (s_asm_recv == s_asm_count) {          // frame complete
    memcpy(s_buf, s_asm, s_asm_len);        // commit to the render buffer
    s_frame_len  = s_asm_len;
    s_asm_active = false;
    return true;
  }
  return false;
}

void video_begin() {
  s_vudp.begin(VIDEO_UDP_PORT);
  s_have_frame    = false;
  s_frame_len     = 0;
  s_asm_active    = false;
  s_ever_frame    = false;
  s_last_frame_ms = millis();
  sendSubscribe();
}

bool video_loop() {
  // Keep the camera streaming to us.
  if (millis() - s_last_sub_ms > VIDEO_SUB_INTERVAL_MS) sendSubscribe();

  bool got_new = false;

  // Drain every queued datagram; keep only the freshest completed frame in s_buf.
  int sz = s_vudp.parsePacket();
  while (sz > 0) {
    int n = s_vudp.read(s_pkt, sizeof(s_pkt));
    if (n > 0) {
      // Telemetry datagram? (starts with the 4-byte 'RFPV' magic)
      if (n >= (int)sizeof(FrameHeader) &&
          s_pkt[0] == FPV_MAGIC_0 && s_pkt[1] == FPV_MAGIC_1 &&
          s_pkt[2] == FPV_MAGIC_2 && s_pkt[3] == FPV_MAGIC_3) {
        memcpy(&s_hdr, s_pkt, sizeof(FrameHeader));
      } else if (onChunk(s_pkt, n)) {
        got_new         = true;
        s_have_frame    = true;
        s_ever_frame    = true;
        s_last_frame_ms = millis();
      }
    }
    sz = s_vudp.parsePacket();
  }

  // Drop a stale partial so a lost chunk can never wedge the reassembler.
  if (s_asm_active && (millis() - s_asm_start_ms) > FRAME_ASSEMBLY_TMO) {
    g_rx.frames_dropped++;
    s_asm_active = false;
  }
  return got_new;
}

const uint8_t*     video_frame_data()   { return s_buf; }
uint32_t           video_frame_len()    { return s_frame_len; }
const FrameHeader* video_frame_header() { return &s_hdr; }

// "Connected" for UDP = we have seen a frame recently. Drives LIVE vs STREAM_LOST.
bool video_is_connected() {
  return s_ever_frame && (millis() - s_last_frame_ms) < VIDEO_STALL_TMO;
}

void video_request_reconnect() {
  s_asm_active = false;
  s_ever_frame = false;
  s_have_frame = false;
  sendSubscribe();
}

// =============================================================================
#else  // VIDEO_USE_UDP == 0
// =============================================================================
//  Raw-TCP transport (original)
// -----------------------------------------------------------------------------
static WiFiClient s_client;
static uint8_t    s_buf[VIDEO_RX_BUFFER];
static FrameHeader s_hdr;
static uint32_t   s_frame_len   = 0;
static bool       s_have_frame  = false;
static uint32_t   s_last_frame_ms = 0;
static uint32_t   s_last_connect_attempt = 0;
static bool       s_reconnect_req = false;

static bool readExact(uint8_t* dst, uint32_t n, uint32_t deadline_ms) {
  uint32_t got = 0;
  uint32_t start = millis();
  while (got < n) {
    if (!s_client.connected()) return false;
    int avail = s_client.available();
    if (avail > 0) {
      int want = (int)(n - got);
      int r = s_client.read(dst + got, (avail < want) ? avail : want);
      if (r > 0) { got += r; continue; }
    }
    if (millis() - start > deadline_ms) return false;
    yield();
  }
  return true;
}

static int readOneFrame() {
  if (!s_client.connected()) return -1;
  if (s_client.available() < (int)sizeof(FrameHeader)) return 0;

  if (!readExact((uint8_t*)&s_hdr, sizeof(FrameHeader), 300)) return -1;

  if (s_hdr.magic[0] != FPV_MAGIC_0 || s_hdr.magic[1] != FPV_MAGIC_1 ||
      s_hdr.magic[2] != FPV_MAGIC_2 || s_hdr.magic[3] != FPV_MAGIC_3) {
    LOGE("Video: bad magic -> resync");
    return -1;
  }
  if (s_hdr.version != FPV_PROTOCOL_VERSION) {
    LOGE("Video: protocol version %d != %d", s_hdr.version, FPV_PROTOCOL_VERSION);
    return -1;
  }
  if (s_hdr.header_size > sizeof(FrameHeader)) {
    uint8_t skip[16];
    uint32_t extra = s_hdr.header_size - sizeof(FrameHeader);
    while (extra > 0) {
      uint32_t chunk = extra > sizeof(skip) ? sizeof(skip) : extra;
      if (!readExact(skip, chunk, 300)) return -1;
      extra -= chunk;
    }
  }

  if (s_hdr.jpeg_length == 0 || s_hdr.jpeg_length > VIDEO_RX_BUFFER) {
    LOGE("Video: bad jpeg_length %lu (max %d)", (unsigned long)s_hdr.jpeg_length,
         VIDEO_RX_BUFFER);
    return -1;
  }
  if (!readExact(s_buf, s_hdr.jpeg_length, 500)) return -1;

  s_frame_len = s_hdr.jpeg_length;
  return 1;
}

static void connectIfNeeded() {
  if (s_client.connected() && !s_reconnect_req) return;

  if (s_reconnect_req) {
    s_client.stop();
    s_reconnect_req = false;
  }
  uint32_t now = millis();
  if (now - s_last_connect_attempt < 500) return;
  s_last_connect_attempt = now;

  LOGI("Video: connecting to %s:%d ...", CAMERA_IP, VIDEO_TCP_PORT);
  if (s_client.connect(CAMERA_IP, VIDEO_TCP_PORT, VIDEO_CONNECT_TMO)) {
    s_client.setNoDelay(true);
    s_last_frame_ms = millis();
    LOGI("Video: connected");
  } else {
    LOGV("Video: connect failed");
  }
}

void video_begin() {
  s_have_frame = false;
  s_frame_len  = 0;
}

bool video_loop() {
  connectIfNeeded();
  if (!s_client.connected()) {
    s_have_frame = false;
    return false;
  }

  bool got_new = false;
  for (;;) {
    int r = readOneFrame();
    if (r == 1) {
      if (got_new) g_rx.frames_dropped++;
      got_new = true;
      s_have_frame = true;
      s_last_frame_ms = millis();
      if (s_client.available() >= (int)sizeof(FrameHeader)) continue;
      break;
    } else if (r == 0) {
      break;
    } else {
      LOGE("Video: dropping link to resync");
      s_client.stop();
      s_have_frame = false;
      return false;
    }
  }

  if (!got_new && (millis() - s_last_frame_ms > VIDEO_STALL_TMO)) {
    LOGE("Video: stream stalled %lums -> reconnect", (unsigned long)VIDEO_STALL_TMO);
    s_client.stop();
    s_have_frame = false;
    return false;
  }

  return got_new;
}

const uint8_t*     video_frame_data()   { return s_buf; }
uint32_t           video_frame_len()    { return s_frame_len; }
const FrameHeader* video_frame_header() { return &s_hdr; }
bool               video_is_connected() { return s_client.connected(); }
void               video_request_reconnect() { s_reconnect_req = true; }

#endif // VIDEO_USE_UDP
