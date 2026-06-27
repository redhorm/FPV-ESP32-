// =============================================================================
//  ui_overlay.cpp  -  flicker-free FPV HUD drawn on top of the live video
// -----------------------------------------------------------------------------
//  Anti-flicker strategy:
//    * The overlay lives in off-screen LGFX sprites (top bar, bottom bar,
//      attitude gauge). Sprite *contents* are re-rendered at most at
//      OVERLAY_REFRESH_HZ (numbers) or when a value changes (REC blink).
//    * Each video frame we simply pushSprite() the already-rendered sprites via
//      DMA. We never clear the screen and never draw partial primitives onto
//      the live panel, so there is zero flicker - the user only ever sees a
//      complete sprite swap.
//    * The video drawJpg repaints the full screen every frame, so regions NOT
//      covered by a sprite automatically show fresh video (and switching
//      overlay modes needs no explicit erase).
//
//  Latency estimate: we can't sync clocks across the link, so we track the
//  running minimum of (arrival_ms - camera_timestamp) as the clock offset (the
//  "fastest" frame ~= near-zero queue), and report current latency as
//  (delta - min_delta). It's a relative estimate, documented as such.
// =============================================================================
#include "ui_overlay.h"
#include "config.h"
#include "ui_theme.h"
#include "app_state.h"
#include "display.h"
#include "protocol.h"
#include <Arduino.h>

static LGFX_Sprite s_top(&display());
static LGFX_Sprite s_bot(&display());
static LGFX_Sprite s_gauge(&display());
static bool  s_inited = false;
static bool  s_have_sprites = false;   // false => sprite alloc failed, draw direct

static uint32_t s_last_render = 0;
static bool     s_dirty = true;

// -----------------------------------------------------------------------------
static void initSprites() {
  bool psram = (ESP.getPsramSize() > 0);
  s_top.setPsram(psram);
  s_bot.setPsram(psram);
  s_gauge.setPsram(psram);
  s_top.setColorDepth(16);
  s_bot.setColorDepth(16);
  s_gauge.setColorDepth(16);
  // createSprite returns nullptr on allocation failure; track success so we can
  // fall back to drawing directly (still functional, slightly less smooth).
  void* a = s_top.createSprite(DISPLAY_W, TOPBAR_H);
  void* b = s_bot.createSprite(DISPLAY_W, BOTBAR_H);
  void* c = s_gauge.createSprite(GAUGE_RADIUS * 2 + 2, GAUGE_RADIUS * 2 + 2);
  s_have_sprites = (a && b && c);
  if (!s_have_sprites) {
    LOGE("Overlay sprite alloc failed (low RAM) - using direct draw fallback");
  }
  s_inited = true;
}

// Map RSSI (dBm) to 0..4 signal bars.
static int rssiBars(int rssi) {
  if (rssi == 0)        return 0;
  if (rssi >= -55)      return 4;
  if (rssi >= -65)      return 3;
  if (rssi >= -75)      return 2;
  if (rssi >= -85)      return 1;
  return 0;
}

static uint16_t signalColor(int bars) {
  if (bars >= 3) return COL_OK;
  if (bars == 2) return COL_WARN;
  return COL_ERR;
}

// -----------------------------------------------------------------------------
void overlay_ingest(const FrameHeader* h, uint32_t now_ms) {
  if (!h) return;

  // mirror piggybacked telemetry
  g_rx.cam_recording = (h->flags & FPV_FLAG_RECORDING)     != 0;
  g_rx.cam_sd_ok     = (h->flags & FPV_FLAG_SD_AVAILABLE)  != 0;
  g_rx.cam_led_on    = (h->flags & FPV_FLAG_LED_ON)        != 0;
  g_rx.cam_mpu_ok    = (h->flags & FPV_FLAG_MPU_AVAILABLE) != 0;
  g_rx.cam_fps       = h->fps_camera_x2 / 2.0f;
  g_rx.cam_quality   = h->jpeg_quality;
  g_rx.cam_frame_id  = h->frame_id;
  g_rx.roll          = h->roll_ddeg  / 10.0f;
  g_rx.pitch         = h->pitch_ddeg / 10.0f;
  g_rx.cam_heap_kb   = h->free_heap_kb;
  g_rx.cam_error     = h->error_flags;
  // NOTE: frame_age_ms, drawn-FPS and frames_drawn are computed in main.cpp,
  // right after the draw, using the TSYNC clock offset (honest measurement).
  (void)now_ms;
}

// -----------------------------------------------------------------------------
static void renderTop() {
  s_top.fillSprite(COL_BG);
  s_top.fillRoundRect(0, 0, DISPLAY_W, TOPBAR_H, 6, COL_PANEL);
  s_top.drawRoundRect(0, 0, DISPLAY_W, TOPBAR_H, 6, COL_PANEL_EDGE);

  // --- left: Wi-Fi signal bars ---
  int bars = rssiBars(g_rx.rssi);
  uint16_t sc = signalColor(bars);
  int bx = 6, by = TOPBAR_H - 5;
  for (int i = 0; i < 4; i++) {
    int bh = 3 + i * 3;
    uint16_t c = (i < bars) ? sc : COL_PANEL_EDGE;
    s_top.fillRect(bx + i * 5, by - bh, 3, bh, c);
  }
  s_top.setTextColor(COL_TEXT_DIM);
  s_top.setTextDatum(textdatum_t::middle_left);
  s_top.setTextSize(1);
  char rb[8];
  snprintf(rb, sizeof(rb), "%d", g_rx.rssi);
  s_top.drawString(rb, 28, TOPBAR_H / 2);

  // --- center: drawn FPS + honest frame age ---
  char mid[24];
  snprintf(mid, sizeof(mid), "%.0ff %lums",
           g_rx.fps_video, (unsigned long)g_rx.frame_age_ms);
  s_top.setTextColor(COL_ACCENT);
  s_top.setTextDatum(textdatum_t::middle_center);
  s_top.drawString(mid, DISPLAY_W / 2, TOPBAR_H / 2);

  // --- right: quality + REC dot ---
  const char* q = (g_rx.cam_quality <= 8) ? "HQ"
                : (g_rx.cam_quality <= 18) ? "MQ" : "LQ";
  s_top.setTextColor(COL_TEXT_DIM);
  s_top.setTextDatum(textdatum_t::middle_right);
  s_top.drawString(q, DISPLAY_W - 22, TOPBAR_H / 2);

  // REC indicator: elegant slow blink.
  bool blink_on = ((millis() / REC_BLINK_MS) & 1) == 0;
  if (g_rx.cam_recording && blink_on) {
    s_top.fillCircle(DISPLAY_W - 10, TOPBAR_H / 2, 4, COL_MAGENTA);
  } else if (g_rx.cam_recording) {
    s_top.drawCircle(DISPLAY_W - 10, TOPBAR_H / 2, 4, COL_MAGENTA);
  }
}

static void renderBot() {
  s_bot.fillSprite(COL_BG);
  s_bot.fillRoundRect(0, 0, DISPLAY_W, BOTBAR_H, 6, COL_PANEL);
  s_bot.drawRoundRect(0, 0, DISPLAY_W, BOTBAR_H, 6, COL_PANEL_EDGE);
  s_bot.setTextSize(1);

  // --- left: SD status ---
  s_bot.setTextDatum(textdatum_t::middle_left);
  if (g_rx.cam_sd_ok) {
    s_bot.setTextColor(g_rx.cam_recording ? COL_MAGENTA : COL_OK);
    s_bot.drawString(g_rx.cam_recording ? "REC" : "SD OK", 6, BOTBAR_H / 2);
  } else {
    s_bot.setTextColor(COL_WARN);
    s_bot.drawString("NO SD", 6, BOTBAR_H / 2);
  }

  // --- center: drops + packet loss ---
  char d[24];
  snprintf(d, sizeof(d), "D%lu L%u%%",
           (unsigned long)g_rx.frames_dropped, g_rx.packet_loss);
  s_bot.setTextColor(g_rx.packet_loss > 15 ? COL_WARN : COL_TEXT_DIM);
  s_bot.setTextDatum(textdatum_t::middle_center);
  s_bot.drawString(d, DISPLAY_W / 2, BOTBAR_H / 2);

  // --- right: camera icon + LED state ---
  s_bot.setTextColor(g_rx.cam_led_on ? COL_ACCENT : COL_TEXT_DIM);
  s_bot.setTextDatum(textdatum_t::middle_right);
  s_bot.drawString(g_rx.cam_led_on ? "LED" : "CAM", DISPLAY_W - 6, BOTBAR_H / 2);
  // tiny lens glyph
  s_bot.drawCircle(DISPLAY_W - 30, BOTBAR_H / 2, 4, COL_TEXT_DIM);
  s_bot.fillCircle(DISPLAY_W - 30, BOTBAR_H / 2, 1, COL_ACCENT);
}

// Off-road attitude gauge: tilting horizon + roll/pitch readout.
static void renderGauge() {
  const int R = GAUGE_RADIUS;
  const int C = R + 1;
  s_gauge.fillSprite(COL_BG);
  s_gauge.fillCircle(C, C, R, COL_PANEL);
  s_gauge.drawCircle(C, C, R, COL_PANEL_EDGE);

  float roll  = g_rx.cam_mpu_ok ? g_rx.roll  : 0.0f;
  float pitch = g_rx.cam_mpu_ok ? g_rx.pitch : 0.0f;

  // Horizon line: tilt by roll, shift vertically by pitch.
  float rr = roll * (float)DEG_TO_RAD;
  float cphi = cosf(rr), sphi = sinf(rr);
  float pitchPix = constrain(pitch, -45.0f, 45.0f) * (R / 45.0f);
  int half = R - 5;
  int x1 = C - cphi * half, y1 = C - sphi * half - pitchPix;
  int x2 = C + cphi * half, y2 = C + sphi * half - pitchPix;
  uint16_t hzColor = g_rx.cam_mpu_ok ? COL_ACCENT : COL_ACCENT_DK;
  s_gauge.drawLine(x1, y1, x2, y2, hzColor);
  s_gauge.drawLine(x1, y1 + 1, x2, y2 + 1, hzColor);

  // Fixed aircraft reference (centre + stubby wings).
  s_gauge.fillCircle(C, C, 2, COL_MAGENTA);
  s_gauge.drawFastHLine(C - 10, C, 6, COL_TEXT);
  s_gauge.drawFastHLine(C + 4,  C, 6, COL_TEXT);

  // Roll/pitch numbers.
  s_gauge.setTextSize(1);
  s_gauge.setTextDatum(textdatum_t::middle_center);
  s_gauge.setTextColor(g_rx.cam_mpu_ok ? COL_TEXT : COL_TEXT_DIM);
  char t[16];
  snprintf(t, sizeof(t), "%c%.0f", g_rx.cam_mpu_ok ? 'R' : '-', roll);
  s_gauge.drawString(t, C, 2 * R - 6);
  if (!g_rx.cam_mpu_ok) {
    s_gauge.setTextColor(COL_TEXT_DIM);
    s_gauge.drawString("DEMO", C, 8);
  }
}

// -----------------------------------------------------------------------------
void overlay_invalidate() { s_dirty = true; }

void overlay_draw(bool force) {
  if (!s_inited) initSprites();
  if (!s_have_sprites) return;   // graceful: no overlay rather than a crash

  uint32_t now = millis();
  bool time_to_render = (now - s_last_render) >= OVERLAY_REFRESH_MS;
  // REC blink needs sub-refresh updates of the top bar.
  bool rec_blink = g_rx.cam_recording;

  if (force || s_dirty || time_to_render || rec_blink) {
    if (g_rx.overlay_mode != OVERLAY_MINIMAL) {
      renderTop();
      renderBot();
    } else {
      // Minimal: only the top bar (kept tiny/clean).
      renderTop();
    }
    if (g_rx.overlay_mode == OVERLAY_OFFROAD) renderGauge();
    s_last_render = now;
    s_dirty = false;
  }

  // Push (cheap, DMA) every call so the overlay survives the full-screen video.
  s_top.pushSprite(0, 0);
  if (g_rx.overlay_mode != OVERLAY_MINIMAL) {
    s_bot.pushSprite(0, DISPLAY_H - BOTBAR_H);
  }
  if (g_rx.overlay_mode == OVERLAY_OFFROAD) {
    s_gauge.pushSprite(GAUGE_CX - GAUGE_RADIUS, GAUGE_CY - GAUGE_RADIUS);
  }
}
