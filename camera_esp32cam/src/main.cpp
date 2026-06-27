// =============================================================================
//  RC_FPV_SCALER_PRO  -  ESP32-CAM firmware (camera + AP + stream + REC)
// -----------------------------------------------------------------------------
//  Responsibilities of this file: bring up hardware, create the Wi-Fi AP, and
//  run the non-blocking main loop / state machine. All heavy lifting lives in
//  the modules (camera_service, stream_server, sd_recorder, command_udp,
//  telemetry). There are NO long delays anywhere in the hot path.
//
//  Pipeline per loop iteration:
//    grab latest frame -> push to TCP client -> offer to SD recorder -> return
//    frame -> service UDP commands, HTTP debug, telemetry, optional MPU.
// =============================================================================
#include <Arduino.h>
#include <WiFi.h>
#include <esp_wifi.h>

#include "config.h"
#include "app_state.h"
#include "camera_service.h"
#include "stream_server.h"
#include "sd_recorder.h"
#include "command_udp.h"
#include "telemetry.h"

AppStatus g_status;   // the single shared state instance (declared extern elsewhere)

// -----------------------------------------------------------------------------
static void startAccessPoint() {
  WiFi.mode(WIFI_AP);

  IPAddress ip(WIFI_AP_IP_0, WIFI_AP_IP_1, WIFI_AP_IP_2, WIFI_AP_IP_3);
  IPAddress gw(WIFI_AP_IP_0, WIFI_AP_IP_1, WIFI_AP_IP_2, WIFI_AP_IP_3);
  IPAddress mask(255, 255, 255, 0);
  WiFi.softAPConfig(ip, gw, mask);

  bool ok = WiFi.softAP(WIFI_AP_SSID, WIFI_AP_PASSWORD,
                        WIFI_AP_CHANNEL, WIFI_AP_HIDDEN, WIFI_AP_MAX_CONN);
  if (!ok) {
    LOGE("softAP() failed");
  }

  // Disable Wi-Fi power save: power save adds latency/jitter to our stream.
  esp_wifi_set_ps(WIFI_PS_NONE);

  LOGI("AP '%s' up, IP %s, channel %d",
       WIFI_AP_SSID, WiFi.softAPIP().toString().c_str(), WIFI_AP_CHANNEL);
}

// -----------------------------------------------------------------------------
void setup() {
  Serial.begin(SERIAL_BAUD);
  delay(50);   // tiny settle for the UART; not in any hot path
  LOGI("\n=== RC_FPV_SCALER_PRO :: ESP32-CAM boot ===");

  g_status.state = CamState::BOOT;

  if (!camera_init()) {
    g_status.state = CamState::CAMERA_ERROR;
    LOGE("Camera failed - will keep AP up so receiver can show the error.");
  }

  startAccessPoint();

  // SD is optional: a failure here is non-fatal (sd_available stays false).
  sd_begin();

  camera_mpu_init();      // no-op unless ENABLE_MPU6050

  stream_begin();
  udp_begin();

  g_status.state = (g_status.state == CamState::CAMERA_ERROR)
                     ? CamState::CAMERA_ERROR : CamState::AP_READY;
  LOGI("Setup complete. State=%d", (int)g_status.state);
}

// -----------------------------------------------------------------------------
void loop() {
  // --- Always service control/debug/telemetry, even on camera error ---
  stream_loop();
  udp_loop();
  telemetry_loop();
  camera_mpu_update();    // no-op unless MPU enabled

  if (g_status.state == CamState::CAMERA_ERROR) {
    // Without a camera there is nothing to stream; keep the box responsive.
    delay(5);
    return;
  }

  // --- Video hot path ---
  camera_fb_t* fb = camera_grab();   // blocks until a fresh frame is ready
  if (!fb) {
    LOGV("fb_get returned null");
    return;
  }

  g_status.frame_id++;
  telemetry_on_frame();

  // Prioritise streaming. Send first, then persist to SD (recorder self-skips
  // to protect latency). Update the coarse state for diagnostics.
  bool sent = stream_send_frame(fb);
  sd_rec_offer(fb);

  if (g_status.recording)        g_status.state = CamState::RECORDING;
  else if (sent)                 g_status.state = CamState::STREAMING;
  else                           g_status.state = CamState::AP_READY;

  camera_return(fb);
}
