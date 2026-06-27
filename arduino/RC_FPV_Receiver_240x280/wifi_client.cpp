// =============================================================================
//  wifi_client.cpp  -  join the camera AP and keep the link healthy
// -----------------------------------------------------------------------------
//  Non-blocking connect with automatic retry. We never busy-wait: the loop just
//  checks WiFi.status() and re-issues WiFi.begin() on a backoff if the link
//  drops. RSSI is sampled for the overlay's signal bar.
// =============================================================================
#include "wifi_client.h"
#include "config.h"
#include "app_state.h"
#include <Arduino.h>
#include <WiFi.h>

static uint32_t s_last_attempt = 0;
static uint32_t s_attempt_start = 0;
static bool     s_connecting = false;

static void beginAttempt() {
  LOGI("Wi-Fi: connecting to '%s'...", WIFI_SSID);
  WiFi.disconnect(true, true);
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);             // low latency: no modem sleep
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  s_connecting    = true;
  s_attempt_start = millis();
  s_last_attempt  = millis();
}

void wifi_begin() {
  beginAttempt();
}

void wifi_loop() {
  wl_status_t st = WiFi.status();

  if (st == WL_CONNECTED) {
    if (!g_rx.wifi_connected) {
      LOGI("Wi-Fi connected, IP %s", WiFi.localIP().toString().c_str());
    }
    g_rx.wifi_connected = true;
    g_rx.rssi = WiFi.RSSI();
    s_connecting = false;
    return;
  }

  // Not connected.
  if (g_rx.wifi_connected) {
    LOGE("Wi-Fi lost");
    g_rx.wifi_connected = false;
  }

  uint32_t now = millis();
  if (s_connecting) {
    // Give the current attempt up to WIFI_CONNECT_TMO before retrying.
    if (now - s_attempt_start > WIFI_CONNECT_TMO) {
      LOGE("Wi-Fi connect timed out, retrying");
      beginAttempt();
    }
  } else {
    // Backoff between attempts.
    if (now - s_last_attempt > 2000) beginAttempt();
  }
}

bool wifi_is_connected() {
  return WiFi.status() == WL_CONNECTED;
}
