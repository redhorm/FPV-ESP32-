// =============================================================================
//  stream_server.cpp  -  low-latency TCP JPEG stream + HTTP debug endpoints
// -----------------------------------------------------------------------------
//  Video path (the important one):
//    * A bare TCP server on VIDEO_TCP_PORT accepts ONE receiver.
//    * Each frame is sent as [FrameHeader][JPEG bytes]. No MJPEG/multipart, no
//      HTTP keep-alive overhead -> minimal per-frame bytes and latency.
//    * We never queue frames here; main.cpp pushes the latest frame and we
//      write it straight to the socket. If the socket would block we bail and
//      try again next frame (the receiver discards stale frames anyway).
//
//  Debug path (optional, must not disturb the video path):
//    * A standard WebServer on HTTP_DEBUG_PORT serves /status and /snapshot.
//    * Snapshot grabs its own short-lived frame; it is rate-limited implicitly
//      by being request-driven.
// =============================================================================
#include "stream_server.h"
#include "config.h"
#include "app_state.h"
#include "telemetry.h"
#include "camera_service.h"
#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>

static WiFiServer s_video(VIDEO_TCP_PORT);
static WiFiClient s_client;
static uint32_t   s_last_rx_ms = 0;

static WebServer  s_http(HTTP_DEBUG_PORT);

// Stringify the numeric port macro for the debug page text.
#define FPV_STR(x)  #x
#define FPV_XSTR(x) FPV_STR(x)

// -----------------------------------------------------------------------------
static void handleStatus() {
  char buf[256];
  telemetry_build_status(buf, sizeof(buf));
  s_http.send(200, "text/plain", buf);
}

static void handleSnapshot() {
  camera_fb_t* fb = camera_grab();
  if (!fb) { s_http.send(503, "text/plain", "no frame"); return; }
  // WebServer needs a content length; send raw JPEG.
  s_http.setContentLength(fb->len);
  s_http.send(200, "image/jpeg", "");
  s_http.sendContent((const char*)fb->buf, fb->len);
  camera_return(fb);
}

static void handleRoot() {
  s_http.send(200, "text/plain",
    "RC_FPV_SCALER_PRO camera\n"
    "Endpoints: /status  /snapshot\n"
    "Video stream: raw TCP on port " FPV_XSTR(VIDEO_TCP_PORT) "\n");
}

// -----------------------------------------------------------------------------
void stream_begin() {
  s_video.begin();
  s_video.setNoDelay(true);     // disable Nagle -> send small frames promptly

  s_http.on("/",         handleRoot);
  s_http.on("/status",   handleStatus);
  s_http.on("/snapshot", handleSnapshot);
  s_http.begin();

  LOGI("Video TCP server on :%d, HTTP debug on :%d",
       VIDEO_TCP_PORT, HTTP_DEBUG_PORT);
}

bool stream_has_client() {
  return s_client && s_client.connected();
}

// -----------------------------------------------------------------------------
bool stream_send_frame(camera_fb_t* fb) {
  if (!fb) return false;
  if (!stream_has_client()) return false;

  FrameHeader hdr;
  telemetry_fill_header(&hdr, fb->len);

  // Write header then payload. WiFiClient::write blocks up to the socket send
  // timeout; with NoDelay + a healthy link this is sub-millisecond for our
  // small frames. We accept a short block here in exchange for simplicity.
  size_t n1 = s_client.write((const uint8_t*)&hdr, sizeof(hdr));
  if (n1 != sizeof(hdr)) { s_client.stop(); return false; }

  size_t n2 = s_client.write(fb->buf, fb->len);
  if (n2 != fb->len)     { s_client.stop(); return false; }

  return true;
}

// -----------------------------------------------------------------------------
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

  // Liveness: the receiver isn't required to send anything, so we treat a
  // broken connection (detected on write) as the primary signal. As a
  // belt-and-braces timeout, drop a client that has been gone for too long.
  if (stream_has_client()) {
    if (s_client.connected()) {
      s_last_rx_ms = millis();              // still connected -> healthy
    } else if (millis() - s_last_rx_ms > STREAM_CLIENT_TIMEOUT) {
      LOGI("Receiver timed out -> closing");
      s_client.stop();
    }
  }

  // Debug HTTP server (cheap when idle).
  s_http.handleClient();
}
