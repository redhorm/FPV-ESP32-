// =============================================================================
//  video_client.cpp  -  low-latency JPEG video receiver (protocol v2)
// -----------------------------------------------------------------------------
//  UDP chunked receiver (VIDEO_USE_UDP=1, default):
//    * Periodically sends FPV_VIDEO_SUBSCRIBE so the camera unicasts to us.
//    * Reassembles [ChunkHeader][payload] into whole JPEG frames using a
//      separate assembly buffer, so a just-completed frame is never corrupted
//      by a later partial one while we drain the socket.
//    * Freshest-frame-wins: a newer (int32 diff) frame_id abandons any older
//      partial (dropped_old++); a partial older than FRAME_TIMEOUT_MS is
//      abandoned (dropped_timeout++); if two complete frames land in one drain,
//      only the newest is kept (dropped_stale++).
//    * Captures honest metrics: fps_rx, rx_kbps, packet_loss%, and the frame's
//      capture timestamp (for frame_age, finalised after the draw in main.cpp).
//
//  TCP fallback (VIDEO_USE_UDP=0): reads [FrameHeader][JPEG], drains to newest.
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
static const int MAXCH = (MAX_JPEG_SIZE + UDP_PAYLOAD_SIZE - 1) / UDP_PAYLOAD_SIZE;

static WiFiUDP     s_vudp;
static uint8_t     s_buf[MAX_JPEG_SIZE];   // last COMPLETE frame (we render this)
static uint8_t     s_asm[MAX_JPEG_SIZE];   // scatter buffer for the frame in flight
static uint8_t     s_got[MAXCH];
static uint8_t     s_pkt[FPV_CHUNK_HEADER_SIZE + UDP_PAYLOAD_SIZE];
static FrameHeader s_hdr;                   // latest telemetry datagram

static uint32_t s_frame_len = 0;           // length of the frame in s_buf
static uint32_t s_frame_cap = 0;           // capture_ms of the frame in s_buf

// assembly state (frame in flight)
static bool     s_asm_active = false;
static uint32_t s_asm_id = 0, s_asm_len = 0, s_asm_cap = 0, s_asm_start_ms = 0;
static uint16_t s_asm_count = 0, s_asm_recv = 0;

static uint32_t s_last_frame_ms = 0;
static uint32_t s_last_sub_ms   = 0;
static bool     s_ever_frame    = false;

// rolling 1 s metric window
static uint32_t s_win_ms = 0, s_win_complete = 0, s_win_bytes = 0;
static uint32_t s_win_exp_chunks = 0, s_win_got_chunks = 0;

static void sendSubscribe() {
  IPAddress ip; ip.fromString(CAMERA_IP);
  s_vudp.beginPacket(ip, VIDEO_UDP_PORT);
  s_vudp.write((const uint8_t*)FPV_VIDEO_SUBSCRIBE, strlen(FPV_VIDEO_SUBSCRIBE));
  s_vudp.endPacket();
  s_last_sub_ms = millis();
}

static void asmReset(uint32_t id, uint16_t count, uint32_t flen, uint32_t cap) {
  s_asm_active = true; s_asm_id = id; s_asm_count = count; s_asm_len = flen;
  s_asm_cap = cap; s_asm_recv = 0; s_asm_start_ms = millis();
  memset(s_got, 0, sizeof(s_got));
  s_win_exp_chunks += count;     // we now expect this many chunks
}

// Returns true if a COMPLETE frame just landed in s_buf.
static bool onChunk(const uint8_t* p, int n) {
  if (n < (int)sizeof(ChunkHeader)) return false;
  ChunkHeader h; memcpy(&h, p, sizeof(h));

  if (h.magic != FPV_UDP_CHUNK_MAGIC || h.version != FPV_PROTOCOL_VERSION) return false;
  if (h.chunk_count == 0 || h.chunk_count > MAXCH)         return false;
  if (h.frame_len == 0   || h.frame_len > MAX_JPEG_SIZE)   return false;
  if (h.chunk_id >= h.chunk_count)                         return false;
  if ((int)(sizeof(ChunkHeader) + h.chunk_len) > n)        return false;

  if (!s_asm_active) {
    asmReset(h.frame_id, h.chunk_count, h.frame_len, h.capture_ms);
  } else if (h.frame_id != s_asm_id) {
    int32_t diff = (int32_t)(h.frame_id - s_asm_id);   // wrap-safe "newer?"
    if (diff > 0) {
      if (s_asm_recv < s_asm_count) { g_rx.dropped_old++; g_rx.frames_dropped++; }
      asmReset(h.frame_id, h.chunk_count, h.frame_len, h.capture_ms);
    } else {
      return false;   // straggler from an older frame
    }
  }

  uint32_t off = (uint32_t)h.chunk_id * UDP_PAYLOAD_SIZE;
  if (off + h.chunk_len > MAX_JPEG_SIZE) return false;

  if (!s_got[h.chunk_id]) {
    memcpy(s_asm + off, p + sizeof(ChunkHeader), h.chunk_len);
    s_got[h.chunk_id] = 1;
    s_asm_recv++;
    s_win_got_chunks++;
  }

  if (s_asm_recv == s_asm_count) {
    memcpy(s_buf, s_asm, s_asm_len);
    s_frame_len  = s_asm_len;
    s_frame_cap  = s_asm_cap;
    s_asm_active = false;
    return true;
  }
  return false;
}

void video_begin() {
  s_vudp.begin(VIDEO_UDP_PORT);
  s_frame_len = 0; s_asm_active = false; s_ever_frame = false;
  s_last_frame_ms = millis(); s_win_ms = millis();
  sendSubscribe();
}

bool video_loop() {
  if (millis() - s_last_sub_ms > VIDEO_SUB_INTERVAL_MS) sendSubscribe();

  bool got_new = false;

  int sz = s_vudp.parsePacket();
  while (sz > 0) {
    int n = s_vudp.read(s_pkt, sizeof(s_pkt));
    if (n > 0) {
      s_win_bytes += n;
      if (n >= (int)sizeof(FrameHeader) &&
          s_pkt[0] == FPV_MAGIC_0 && s_pkt[1] == FPV_MAGIC_1 &&
          s_pkt[2] == FPV_MAGIC_2 && s_pkt[3] == FPV_MAGIC_3) {
        memcpy(&s_hdr, s_pkt, sizeof(FrameHeader));     // telemetry datagram
      } else if (onChunk(s_pkt, n)) {
        if (got_new) { g_rx.dropped_stale++; g_rx.frames_dropped++; } // keep freshest
        got_new = true;
        s_ever_frame = true;
        s_last_frame_ms = millis();
        s_win_complete++;
      }
    }
    sz = s_vudp.parsePacket();
  }

  // Stale partial -> drop so a lost chunk never wedges the reassembler.
  if (s_asm_active && (millis() - s_asm_start_ms) > FRAME_TIMEOUT_MS) {
    g_rx.dropped_timeout++; g_rx.frames_dropped++;
    s_asm_active = false;
  }

  // Roll up the 1 s metric window.
  uint32_t now = millis();
  uint32_t wdt = now - s_win_ms;
  if (wdt >= 1000) {
    g_rx.fps_rx   = (s_win_complete * 1000.0f) / (float)wdt;
    g_rx.rx_kbps  = (uint32_t)((s_win_bytes * 8ULL) / wdt);   // bytes*8/ms = kbit/s
    g_rx.jpeg_size = s_frame_len;
    g_rx.packet_loss = (s_win_exp_chunks > 0)
        ? (uint8_t)(100 - (s_win_got_chunks * 100ULL) / s_win_exp_chunks) : 0;
    s_win_ms = now; s_win_complete = 0; s_win_bytes = 0;
    s_win_exp_chunks = 0; s_win_got_chunks = 0;
  }
  return got_new;
}

const uint8_t*     video_frame_data()       { return s_buf; }
uint32_t           video_frame_len()        { return s_frame_len; }
const FrameHeader* video_frame_header()     { return &s_hdr; }
uint32_t           video_frame_capture_ms() { return s_frame_cap; }

bool video_is_connected() {
  return s_ever_frame && (millis() - s_last_frame_ms) < VIDEO_STALL_TMO;
}

void video_request_reconnect() {
  s_asm_active = false; s_ever_frame = false; sendSubscribe();
}

// =============================================================================
#else  // VIDEO_USE_UDP == 0  ->  raw-TCP fallback
// =============================================================================
static WiFiClient s_client;
static uint8_t    s_buf[MAX_JPEG_SIZE];
static FrameHeader s_hdr;
static uint32_t   s_frame_len   = 0;
static uint32_t   s_last_frame_ms = 0;
static uint32_t   s_last_connect_attempt = 0;
static bool       s_reconnect_req = false;

static bool readExact(uint8_t* dst, uint32_t n, uint32_t deadline_ms) {
  uint32_t got = 0, start = millis();
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
      s_hdr.magic[2] != FPV_MAGIC_2 || s_hdr.magic[3] != FPV_MAGIC_3) return -1;
  if (s_hdr.version != FPV_PROTOCOL_VERSION) return -1;
  if (s_hdr.header_size > sizeof(FrameHeader)) {
    uint8_t skip[16];
    uint32_t extra = s_hdr.header_size - sizeof(FrameHeader);
    while (extra > 0) {
      uint32_t c = extra > sizeof(skip) ? sizeof(skip) : extra;
      if (!readExact(skip, c, 300)) return -1;
      extra -= c;
    }
  }
  if (s_hdr.jpeg_length == 0 || s_hdr.jpeg_length > MAX_JPEG_SIZE) return -1;
  if (!readExact(s_buf, s_hdr.jpeg_length, 500)) return -1;
  s_frame_len = s_hdr.jpeg_length;
  return 1;
}

static void connectIfNeeded() {
  if (s_client.connected() && !s_reconnect_req) return;
  if (s_reconnect_req) { s_client.stop(); s_reconnect_req = false; }
  uint32_t now = millis();
  if (now - s_last_connect_attempt < 500) return;
  s_last_connect_attempt = now;
  if (s_client.connect(CAMERA_IP, VIDEO_TCP_PORT, VIDEO_CONNECT_TMO)) {
    s_client.setNoDelay(true);
    s_last_frame_ms = millis();
  }
}

void video_begin() { s_frame_len = 0; }

bool video_loop() {
  connectIfNeeded();
  if (!s_client.connected()) return false;
  bool got_new = false;
  for (;;) {
    int r = readOneFrame();
    if (r == 1) {
      if (got_new) { g_rx.dropped_stale++; g_rx.frames_dropped++; }
      got_new = true; s_last_frame_ms = millis();
      if (s_client.available() >= (int)sizeof(FrameHeader)) continue;
      break;
    } else if (r == 0) break;
    else { s_client.stop(); return false; }
  }
  if (!got_new && (millis() - s_last_frame_ms > VIDEO_STALL_TMO)) {
    s_client.stop(); return false;
  }
  return got_new;
}

const uint8_t*     video_frame_data()       { return s_buf; }
uint32_t           video_frame_len()        { return s_frame_len; }
const FrameHeader* video_frame_header()     { return &s_hdr; }
uint32_t           video_frame_capture_ms() { return s_hdr.timestamp_ms; }
bool               video_is_connected()     { return s_client.connected(); }
void               video_request_reconnect(){ s_reconnect_req = true; }

#endif // VIDEO_USE_UDP
