// =============================================================================
//  stream_server.cpp  -  low-latency JPEG video server + HTTP debug endpoints
// -----------------------------------------------------------------------------
//  Two selectable video transports (config.h :: VIDEO_USE_UDP), same public API
//  (stream_begin / stream_send_frame / stream_loop / stream_has_client) so the
//  main loop never changes:
//
//   VIDEO_USE_UDP = 1  (DEFAULT, recommended) -- UDP chunked stream
//     * The receiver subscribes by sending FPV_VIDEO_SUBSCRIBE to VIDEO_UDP_PORT;
//       we remember its IP:port and unicast video there.
//     * Each frame: one small telemetry datagram (FrameHeader) followed by N
//       chunk datagrams ([ChunkHeader][<=UDP_CHUNK_PAYLOAD JPEG bytes]).
//     * No retransmits, no head-of-line blocking: a lost packet costs one frame,
//       not a stall -> higher FPS / lower, steadier latency.
//
//   VIDEO_USE_UDP = 0  -- raw-TCP stream (original)
//     * One TCP client; each frame is [FrameHeader][JPEG]. Simple and reliable
//       on very clean links; kept as a fallback.
//
//  Debug path (both modes): a standard WebServer on HTTP_DEBUG_PORT serves
//  /status and /snapshot. It is request-driven and stays out of the hot path.
// =============================================================================
#include "stream_server.h"
#include "config.h"
#include "app_state.h"
#include "telemetry.h"
#include "camera_service.h"
#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <WiFiUdp.h>

// Stringify the numeric port macro for the debug page text.
#define FPV_STR(x)  #x
#define FPV_XSTR(x) FPV_STR(x)

static WebServer s_http(HTTP_DEBUG_PORT);

// -----------------------------------------------------------------------------
//  Shared HTTP debug handlers (transport-independent).
// -----------------------------------------------------------------------------
static void handleStatus() {
  char buf[256];
  telemetry_build_status(buf, sizeof(buf));
  s_http.send(200, "text/plain", buf);
}

static void handleSnapshot() {
  camera_fb_t* fb = camera_grab();
  if (!fb) { s_http.send(503, "text/plain", "no frame"); return; }
  // Send the JPEG as raw bytes. We write straight to the client so binary data
  // with embedded NULs is transmitted intact (no String/strlen truncation).
  s_http.setContentLength(fb->len);
  s_http.send(200, "image/jpeg", "");
  s_http.client().write(fb->buf, fb->len);
  camera_return(fb);
}

static void handleRoot() {
  s_http.send(200, "text/plain",
    "RC_FPV_SCALER_PRO camera\n"
    "Endpoints: /status  /snapshot\n"
#if VIDEO_USE_UDP
    "Video stream: UDP chunked on port " FPV_XSTR(VIDEO_UDP_PORT)
    " (send \"" FPV_VIDEO_SUBSCRIBE "\" to subscribe)\n");
#else
    "Video stream: raw TCP on port " FPV_XSTR(VIDEO_TCP_PORT) "\n");
#endif
}

static void httpSetup() {
  s_http.on("/",         handleRoot);
  s_http.on("/status",   handleStatus);
  s_http.on("/snapshot", handleSnapshot);
  s_http.begin();
}

// =============================================================================
#if VIDEO_USE_UDP
// =============================================================================
//  UDP chunked transport
// -----------------------------------------------------------------------------
static WiFiUDP   s_vudp;
static IPAddress s_sub_ip;
static uint16_t  s_sub_port = 0;       // 0 = no subscriber yet
static uint32_t  s_sub_last_ms = 0;
static char      s_inbuf[32];

void stream_begin() {
  s_vudp.begin(VIDEO_UDP_PORT);
  httpSetup();
  LOGI("Video UDP server on :%d, HTTP debug on :%d", VIDEO_UDP_PORT, HTTP_DEBUG_PORT);
}

bool stream_has_client() {
  return s_sub_port != 0 && (millis() - s_sub_last_ms) < VIDEO_SUB_TIMEOUT_MS;
}

// Treat ANY datagram from the receiver on the video port as a subscribe/keepalive
// and (re)latch the unicast target to its source IP:port.
static void pumpSubscribe() {
  int sz = s_vudp.parsePacket();
  while (sz > 0) {
    int n = s_vudp.read(s_inbuf, sizeof(s_inbuf) - 1);
    if (n < 0) n = 0;
    s_inbuf[n] = '\0';
    s_sub_ip   = s_vudp.remoteIP();
    s_sub_port = s_vudp.remotePort();
    s_sub_last_ms = millis();
    LOGV("Video subscriber %s:%u", s_sub_ip.toString().c_str(), s_sub_port);
    sz = s_vudp.parsePacket();
  }
}

bool stream_send_frame(camera_fb_t* fb) {
  if (!fb) return false;
  if (!stream_has_client()) return false;

  // 1) Telemetry datagram (FrameHeader) - one per frame, before the chunks.
  FrameHeader hdr;
  telemetry_fill_header(&hdr, fb->len);
  s_vudp.beginPacket(s_sub_ip, s_sub_port);
  s_vudp.write((const uint8_t*)&hdr, sizeof(hdr));
  s_vudp.endPacket();

  // 2) JPEG chunks.
  const uint32_t len   = fb->len;
  const uint32_t cap   = g_status.frame_capture_ms;     // same clock as TSYNC
  uint16_t       count = (uint16_t)((len + UDP_PAYLOAD_SIZE - 1) / UDP_PAYLOAD_SIZE);
  if (count == 0) count = 1;

  for (uint16_t i = 0; i < count; i++) {
    uint32_t off  = (uint32_t)i * UDP_PAYLOAD_SIZE;
    uint16_t clen = (uint16_t)((len - off) > UDP_PAYLOAD_SIZE
                               ? UDP_PAYLOAD_SIZE : (len - off));
    ChunkHeader ch;
    ch.magic       = FPV_UDP_CHUNK_MAGIC;
    ch.version     = FPV_PROTOCOL_VERSION;
    ch.frame_id    = g_status.frame_id;            // 32-bit; receiver uses int32 diff
    ch.chunk_id    = i;
    ch.chunk_count = count;
    ch.chunk_len   = clen;
    ch.frame_len   = len;
    ch.capture_ms  = cap;

    s_vudp.beginPacket(s_sub_ip, s_sub_port);
    s_vudp.write((const uint8_t*)&ch, sizeof(ch));
    s_vudp.write(fb->buf + off, clen);
    // If endPacket() fails the Wi-Fi TX buffer is momentarily full; we simply
    // drop this chunk. The receiver will miss the frame and render the next one
    // (freshest-frame-wins). No retry -> no added latency.
    s_vudp.endPacket();

#if UDP_CHUNK_GAP_US > 0
    delayMicroseconds(UDP_CHUNK_GAP_US);
#endif
  }
  return true;
}

void stream_loop() {
  pumpSubscribe();
  s_http.handleClient();
}

// =============================================================================
#else  // VIDEO_USE_UDP == 0
// =============================================================================
//  Raw-TCP transport (original)
// -----------------------------------------------------------------------------
static WiFiServer s_video(VIDEO_TCP_PORT);
static WiFiClient s_client;
static uint32_t   s_last_rx_ms = 0;

void stream_begin() {
  s_video.begin();
  s_video.setNoDelay(true);     // disable Nagle -> send small frames promptly
  httpSetup();
  LOGI("Video TCP server on :%d, HTTP debug on :%d", VIDEO_TCP_PORT, HTTP_DEBUG_PORT);
}

bool stream_has_client() {
  return s_client && s_client.connected();
}

bool stream_send_frame(camera_fb_t* fb) {
  if (!fb) return false;
  if (!stream_has_client()) return false;

  FrameHeader hdr;
  telemetry_fill_header(&hdr, fb->len);

  size_t n1 = s_client.write((const uint8_t*)&hdr, sizeof(hdr));
  if (n1 != sizeof(hdr)) { s_client.stop(); return false; }

  size_t n2 = s_client.write(fb->buf, fb->len);
  if (n2 != fb->len)     { s_client.stop(); return false; }

  return true;
}

void stream_loop() {
  // Accept a new client; if one is already connected, keep it and reject extras.
  if (s_video.hasClient()) {
    WiFiClient incoming = s_video.available();
    if (!stream_has_client()) {
      s_client = incoming;
      s_client.setNoDelay(true);
      s_last_rx_ms = millis();
      LOGI("Receiver connected from %s", s_client.remoteIP().toString().c_str());
    } else {
      incoming.stop();   // only one receiver supported
    }
  }

  if (stream_has_client()) {
    if (s_client.connected()) {
      s_last_rx_ms = millis();
    } else if (millis() - s_last_rx_ms > STREAM_CLIENT_TIMEOUT) {
      LOGI("Receiver timed out -> closing");
      s_client.stop();
    }
  }

  s_http.handleClient();
}

#endif // VIDEO_USE_UDP
