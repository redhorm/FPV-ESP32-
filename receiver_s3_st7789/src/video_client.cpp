// =============================================================================
//  video_client.cpp  -  low-latency TCP JPEG receiver (the heart of the FPV)
// -----------------------------------------------------------------------------
//  Latency policy:
//    * We read complete [FrameHeader][JPEG] units from the socket.
//    * "Always show the freshest frame": if, right after reading a frame, the
//      socket already has more bytes queued, that means newer frame(s) are
//      waiting - we discard the one we just read (count it as dropped) and read
//      again, converging on the newest available frame before we render once.
//    * Robustness: bounded reads (deadline), magic validation, oversize guard,
//      stall timeout -> clean reconnect. A corrupt/oversize frame closes the
//      socket so we resync from a known boundary instead of byte-hunting.
// =============================================================================
#include "video_client.h"
#include "config.h"
#include "app_state.h"
#include <Arduino.h>
#include <WiFi.h>

static WiFiClient s_client;
static uint8_t    s_buf[VIDEO_RX_BUFFER];
static FrameHeader s_hdr;
static uint32_t   s_frame_len   = 0;
static bool       s_have_frame  = false;     // valid frame currently in s_buf
static uint32_t   s_last_frame_ms = 0;
static uint32_t   s_last_connect_attempt = 0;
static bool       s_reconnect_req = false;

// Read exactly `n` bytes into `dst`, or fail. Bounded by `deadline_ms` of
// wall-clock so a half-sent frame can never wedge the loop.
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
    // brief yield without a fixed delay; lets Wi-Fi stack make progress
    yield();
  }
  return true;
}

// Read one full frame into s_buf/s_hdr. Returns:
//   1  = frame read OK
//   0  = no complete frame ready yet (try later)
//  -1  = protocol error / connection bad -> caller should drop the link
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
  // Forward-compat: skip any extra header bytes a newer camera might add.
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
  if (now - s_last_connect_attempt < 500) return;   // backoff between dials
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

// -----------------------------------------------------------------------------
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
  // Drain: read frames while complete ones are available, keeping only the last.
  for (;;) {
    int r = readOneFrame();
    if (r == 1) {
      if (got_new) g_rx.frames_dropped++;  // we already had a fresh one -> drop it
      got_new = true;
      s_have_frame = true;
      s_last_frame_ms = millis();
      // If more bytes are already queued a newer frame is waiting: loop again.
      if (s_client.available() >= (int)sizeof(FrameHeader)) continue;
      break;
    } else if (r == 0) {
      break;                               // nothing complete pending
    } else {                               // r < 0 : protocol/link error
      LOGE("Video: dropping link to resync");
      s_client.stop();
      s_have_frame = false;
      return false;
    }
  }

  // Stall watchdog: connected but silent -> reconnect.
  if (!got_new && (millis() - s_last_frame_ms > VIDEO_STALL_TMO)) {
    LOGE("Video: stream stalled %lums -> reconnect", VIDEO_STALL_TMO);
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
