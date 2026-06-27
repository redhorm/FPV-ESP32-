// =============================================================================
//  telemetry_client.cpp  -  UDP command sender + status poller/parser
// -----------------------------------------------------------------------------
//  Commands to the camera go over UDP (snappy, never blocks the video TCP).
//  We periodically send REQUEST_STATUS and parse the single-line reply into
//  g_rx for the System Info screen. The live overlay does NOT depend on this -
//  it uses the telemetry piggybacked on every video frame header.
// =============================================================================
#include "telemetry_client.h"
#include "config.h"
#include "app_state.h"
#include "protocol.h"
#include <Arduino.h>
#include <WiFi.h>
#include <WiFiUdp.h>

static WiFiUDP s_udp;
static char    s_rx[300];

void telemetry_begin() {
  s_udp.begin(0);   // ephemeral local port; we only send + read replies
}

void telemetry_send(const char* cmd) {
  IPAddress ip;
  ip.fromString(CAMERA_IP);
  s_udp.beginPacket(ip, CMD_UDP_PORT);
  s_udp.write((const uint8_t*)cmd, strlen(cmd));
  s_udp.write((uint8_t)'\n');
  s_udp.endPacket();
  LOGV("UDP -> %s", cmd);
}

void telemetry_send_arg(const char* cmd, long arg) {
  char buf[48];
  snprintf(buf, sizeof(buf), "%s=%ld", cmd, arg);
  telemetry_send(buf);
}

// Pull a float that follows "key=" inside the status line. Returns def if absent.
static float getf(const char* s, const char* key, float def) {
  const char* p = strstr(s, key);
  if (!p) return def;
  return strtof(p + strlen(key), nullptr);
}
static long getl(const char* s, const char* key, long def) {
  const char* p = strstr(s, key);
  if (!p) return def;
  return strtol(p + strlen(key), nullptr, 10);
}

static void parseStatus(const char* s) {
  if (strncmp(s, STATUS_REPLY_PREFIX, strlen(STATUS_REPLY_PREFIX)) != 0) return;
  g_rx.cam_recording = getl(s, "rec=",  0) != 0;
  g_rx.cam_sd_ok     = getl(s, "sd=",   0) != 0;
  g_rx.cam_led_on    = getl(s, "led=",  0) != 0;
  g_rx.cam_mpu_ok    = getl(s, "mpu=",  0) != 0;
  g_rx.cam_fps       = getf(s, "fps=",  0);
  g_rx.cam_frame_id  = getl(s, "fid=",  0);
  g_rx.cam_quality   = getl(s, "q=",    0);
  g_rx.roll          = getf(s, "roll=", 0);
  g_rx.pitch         = getf(s, "pitch=",0);
  g_rx.cam_heap_kb   = getl(s, "heap=", 0);
  g_rx.cam_psram_kb  = getl(s, "psram=",0);
  g_rx.cam_uptime_s  = getl(s, "up=",   0);
  g_rx.cam_error     = getl(s, "err=",  0);
}

void telemetry_loop() {
  // Read any pending replies.
  int pkt = s_udp.parsePacket();
  while (pkt > 0) {
    int len = s_udp.read(s_rx, sizeof(s_rx) - 1);
    if (len < 0) len = 0;
    s_rx[len] = '\0';
    if (strncmp(s_rx, PING_REPLY, strlen(PING_REPLY)) == 0) {
      LOGV("PONG");
    } else {
      parseStatus(s_rx);
    }
    pkt = s_udp.parsePacket();
  }

  // Periodically request fresh status (only meaningful when on the AP).
  static uint32_t last = 0;
  uint32_t now = millis();
  if (g_rx.wifi_connected && (now - last >= STATUS_POLL_MS)) {
    last = now;
    telemetry_send(CMD_REQUEST_STATUS);
  }
}
