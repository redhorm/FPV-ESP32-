// =============================================================================
//  command_udp.cpp  -  UDP control channel (receiver -> camera) + status reply
// -----------------------------------------------------------------------------
//  Plain-text tokens (see protocol.h). Non-blocking: udp_loop() drains all
//  pending datagrams each call and replies to the sender's IP/port. Keeping
//  control on UDP (not the TCP video socket) means a lost command never stalls
//  the video pipeline, and commands stay snappy.
// =============================================================================
#include "command_udp.h"
#include "config.h"
#include "app_state.h"
#include "telemetry.h"
#include "camera_service.h"
#include "sd_recorder.h"
#include <Arduino.h>
#include <WiFiUdp.h>

static WiFiUDP s_udp;
static char    s_buf[128];

static void sendReply(const IPAddress& ip, uint16_t port, const char* msg) {
  s_udp.beginPacket(ip, port);
  s_udp.write((const uint8_t*)msg, strlen(msg));
  s_udp.endPacket();
}

static void sendStatus(const IPAddress& ip, uint16_t port) {
  char out[256];
  telemetry_build_status(out, sizeof(out));
  sendReply(ip, port, out);
}

// Parse an optional "=value" integer argument. Returns true if present.
static bool parseIntArg(const char* token, long* out) {
  const char* eq = strchr(token, '=');
  if (!eq) return false;
  *out = strtol(eq + 1, nullptr, 10);
  return true;
}

void udp_begin() {
  s_udp.begin(CMD_UDP_PORT);
  LOGI("UDP command server on :%d", CMD_UDP_PORT);
}

void udp_loop() {
  int packetSize = s_udp.parsePacket();
  while (packetSize > 0) {
    int len = s_udp.read(s_buf, sizeof(s_buf) - 1);
    if (len < 0) len = 0;
    s_buf[len] = '\0';

    // Trim trailing CR/LF/space.
    while (len > 0 && (s_buf[len-1] == '\n' || s_buf[len-1] == '\r' ||
                       s_buf[len-1] == ' ')) {
      s_buf[--len] = '\0';
    }

    IPAddress rip = s_udp.remoteIP();
    uint16_t  rport = s_udp.remotePort();
    LOGV("UDP cmd from %s:%u -> '%s'", rip.toString().c_str(), rport, s_buf);

    if      (strcmp(s_buf, CMD_PING) == 0) {
      sendReply(rip, rport, PING_REPLY "\n");
    }
    else if (strcmp(s_buf, CMD_REQUEST_STATUS) == 0) {
      sendStatus(rip, rport);
    }
    else if (strcmp(s_buf, CMD_REC_TOGGLE) == 0) {
      sd_rec_toggle(); sendStatus(rip, rport);
    }
    else if (strcmp(s_buf, CMD_REC_START) == 0) {
      sd_rec_start();  sendStatus(rip, rport);
    }
    else if (strcmp(s_buf, CMD_REC_STOP) == 0) {
      sd_rec_stop();   sendStatus(rip, rport);
    }
    else if (strcmp(s_buf, CMD_LED_TOGGLE) == 0) {
      camera_led_toggle(); sendStatus(rip, rport);
    }
    else if (strncmp(s_buf, CMD_SET_QUALITY, strlen(CMD_SET_QUALITY)) == 0) {
      long q;
      if (parseIntArg(s_buf, &q)) camera_set_quality((uint8_t)q);
      sendStatus(rip, rport);
    }
    else if (strncmp(s_buf, CMD_SET_OVERLAY_MODE, strlen(CMD_SET_OVERLAY_MODE)) == 0) {
      long m;
      if (parseIntArg(s_buf, &m)) g_status.overlay_mode = (uint8_t)m;
      sendStatus(rip, rport);
    }
    else {
      LOGV("Unknown UDP command: %s", s_buf);
    }

    packetSize = s_udp.parsePacket();   // drain any further queued datagrams
  }
}
