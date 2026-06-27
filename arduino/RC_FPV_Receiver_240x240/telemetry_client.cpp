// =============================================================================
//  telemetry_client.cpp  -  UDP commands + STATUS poll + TSYNC clock sync
// -----------------------------------------------------------------------------
//  Commands go over UDP (snappy, never blocks the video path). We also:
//    * Poll REQUEST_STATUS for the richer System Info screen.
//    * Run TSYNC: send our send-time, the camera replies with its millis(); the
//      exchange with the LOWEST round-trip time gives the best clock-offset
//      estimate (offset = camera_ms + rtt/2 - arrival). main.cpp uses it to turn
//      a frame's capture_ms into an honest frame_age_ms.
//    * Sample the receiver's own heap/PSRAM for the PERF screen.
// =============================================================================
#include "telemetry_client.h"
#include "config.h"
#include "app_state.h"
#include "protocol.h"
#include <Arduino.h>
#include <WiFi.h>
#include <WiFiUdp.h>
#include <esp_heap_caps.h>

static WiFiUDP  s_udp;
static char     s_rx[300];

static uint32_t s_tsync_send_ms = 0;     // when we last sent a TSYNC probe
static uint32_t s_best_rtt      = 0xFFFFFFFF;

void telemetry_begin() {
  s_udp.begin(0);   // ephemeral local port; we only send + read replies
}

void telemetry_send(const char* cmd) {
  IPAddress ip; ip.fromString(CAMERA_IP);
  s_udp.beginPacket(ip, CMD_UDP_PORT);
  s_udp.write((const uint8_t*)cmd, strlen(cmd));
  s_udp.write((uint8_t)'\n');
  s_udp.endPacket();
}

void telemetry_send_arg(const char* cmd, long arg) {
  char buf[48];
  snprintf(buf, sizeof(buf), "%s=%ld", cmd, arg);
  telemetry_send(buf);
}

static float getf(const char* s, const char* key, float def) {
  const char* p = strstr(s, key); return p ? strtof(p + strlen(key), nullptr) : def;
}
static long getl(const char* s, const char* key, long def) {
  const char* p = strstr(s, key); return p ? strtol(p + strlen(key), nullptr, 10) : def;
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

static void handleTsyncReply(const char* s) {
  uint32_t now    = millis();
  uint32_t cam_ms = (uint32_t)strtoul(s + strlen(TSYNC_REPLY_PREFIX), nullptr, 10);
  uint32_t rtt    = now - s_tsync_send_ms;
  if (rtt < s_best_rtt) {                 // keep the most accurate (lowest-RTT) sample
    s_best_rtt        = rtt;
    g_rx.clock_offset = (int32_t)(cam_ms + rtt / 2) - (int32_t)now;  // camera - local
    g_rx.clock_synced = true;
  }
}

void telemetry_loop() {
  // Drain replies.
  int pkt = s_udp.parsePacket();
  while (pkt > 0) {
    int len = s_udp.read(s_rx, sizeof(s_rx) - 1);
    if (len < 0) len = 0;
    s_rx[len] = '\0';
    if (strncmp(s_rx, TSYNC_REPLY_PREFIX, strlen(TSYNC_REPLY_PREFIX)) == 0) {
      handleTsyncReply(s_rx);
    } else if (strncmp(s_rx, PING_REPLY, strlen(PING_REPLY)) == 0) {
      /* PONG */
    } else {
      parseStatus(s_rx);
    }
    pkt = s_udp.parsePacket();
  }

  uint32_t now = millis();

  // Periodic STATUS poll (richer info screens).
  static uint32_t last_status = 0;
  if (g_rx.wifi_connected && (now - last_status >= STATUS_POLL_MS)) {
    last_status = now;
    telemetry_send(CMD_REQUEST_STATUS);
  }

  // Periodic TSYNC probe. Let s_best_rtt slowly "forget" so a temporarily good
  // sample doesn't lock us out of re-syncing as conditions change.
  static uint32_t last_tsync = 0;
  if (g_rx.wifi_connected && (now - last_tsync >= TSYNC_INTERVAL_MS)) {
    last_tsync = now;
    s_best_rtt += s_best_rtt >> 4;        // decay ~6% each probe
    s_tsync_send_ms = now;
    telemetry_send(CMD_TSYNC);
  }

  // Receiver self heap/PSRAM for the PERF screen.
  static uint32_t last_mem = 0;
  if (now - last_mem >= 250) {
    last_mem = now;
    g_rx.free_heap  = ESP.getFreeHeap();
    g_rx.free_psram = ESP.getPsramSize() ? ESP.getFreePsram() : 0;
  }
}
