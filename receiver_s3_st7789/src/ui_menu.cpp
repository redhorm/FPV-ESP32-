// =============================================================================
//  ui_menu.cpp  -  boot screen, quick menu, system info, error screens
// -----------------------------------------------------------------------------
//  The menu replaces the live view (the receiver is in RxState::MENU), so we
//  draw straight to the panel. To avoid flicker we only repaint on change
//  (selection moved / value changed) and, for the live System Info screen,
//  overwrite just the value fields rather than clearing the whole screen.
//
//  Boot screen: a procedural neon-HUD wireframe skull rendered with vector
//  lines (white core + cyan/magenta side-glow) plus corner brackets and the
//  "@luca3d_designs / FPV SYSTEM / BOOTING..." legend - matching the supplied
//  boot artwork's style without embedding any bitmap asset.
// =============================================================================
#include "ui_menu.h"
#include "config.h"
#include "ui_theme.h"
#include "app_state.h"
#include "display.h"
#include "video_client.h"
#include "telemetry_client.h"
#include "ui_overlay.h"
#include "protocol.h"
#include <Arduino.h>

// ---------------------------------------------------------------------------
//  Low-poly skull geometry. Coordinates are in a 0..100 design space and are
//  scaled/centred at runtime. Each entry is an edge {x1,y1,x2,y2}. The set is
//  intentionally faceted to read as "low-poly" like the artwork.
// ---------------------------------------------------------------------------
//  Re-traced from the @luca3d_designs artwork: a symmetric low-poly skull with
//  a triangulated cranium fan, two large angular eye sockets, a triangular
//  nasal cavity, a teeth grid and a tapering jaw. Vectors (not a bitmap) so it
//  stays crisp at any scale and costs no flash.
static const uint8_t SKULL[][4] = {
  // --- outline: cranium -> temple -> cheek -> jaw -> chin (right side) ---
  {50,3, 62,4},  {62,4, 74,9},  {74,9, 84,18}, {84,18, 90,30}, {90,30, 92,43},
  {92,43, 89,54},{89,54, 84,62},{84,62, 80,69},{80,69, 72,82}, {72,82, 60,91},
  {60,91, 50,95},
  // --- outline: left side (mirror) ---
  {50,3, 38,4},  {38,4, 26,9},  {26,9, 16,18}, {16,18, 10,30}, {10,30, 8,43},
  {8,43, 11,54}, {11,54, 16,62},{16,62, 20,69},{20,69, 28,82}, {28,82, 40,91},
  {40,91, 50,95},
  // --- forehead radial fan from the brow centre (50,40) ---
  {50,40, 50,6}, {50,40, 72,10},{50,40, 86,22},{50,40, 90,34},
  {50,40, 28,10},{50,40, 14,22},{50,40, 10,34},
  {12,40, 50,40},{50,40, 88,40},                 // brow ridge
  // --- right eye socket (angular hexagon) ---
  {56,44, 66,42},{66,42, 75,46},{75,46, 74,54},{74,54, 64,57},{64,57, 55,52},
  {55,52, 56,44},{56,44, 74,54},                 // internal facet
  // --- left eye socket (mirror) ---
  {44,44, 34,42},{34,42, 25,46},{25,46, 26,54},{26,54, 36,57},{36,57, 45,52},
  {45,52, 44,44},{44,44, 26,54},                 // internal facet
  // --- nose (inverted triangle + bridge) ---
  {50,50, 50,58},{50,58, 44,72},{50,58, 56,72},{44,72, 50,76},{56,72, 50,76},
  {47,66, 50,58},{53,66, 50,58},
  // --- cheek / jaw facets ---
  {74,54, 89,54},{64,57, 84,62},{64,57, 56,72},{64,80, 70,76},
  {26,54, 11,54},{36,57, 16,62},{36,57, 44,72},{36,80, 30,76},
  // --- teeth grid (top + bottom rails, vertical dividers) ---
  {36,80, 64,80},{36,88, 64,88},
  {36,80, 36,88},{40,80, 40,88},{44,80, 44,88},{48,80, 48,88},
  {52,80, 52,88},{56,80, 56,88},{60,80, 60,88},{64,80, 64,88},
  // --- lower jaw teeth ---
  {42,88, 44,93},{50,88, 50,94},{58,88, 56,93},
};
static const int SKULL_EDGES = sizeof(SKULL) / sizeof(SKULL[0]);

// ---- boot scaling: place skull centred horizontally, in the upper area ----
static const float BOOT_SCALE = 1.55f;            // 100u -> 155px tall
static const int   BOOT_OX    = (DISPLAY_W - (int)(100 * BOOT_SCALE)) / 2;
static const int   BOOT_OY    = 6;

static inline int sx(uint8_t v) { return BOOT_OX + (int)(v * BOOT_SCALE); }
static inline int sy(uint8_t v) { return BOOT_OY + (int)(v * BOOT_SCALE); }

// Draw one neon edge: cyan glow (left), magenta glow (right), white core.
static void neonLine(int x1, int y1, int x2, int y2) {
  auto& g = display();
  g.drawLine(x1 - 1, y1, x2 - 1, y2, COL_ACCENT);
  g.drawLine(x1 + 1, y1, x2 + 1, y2, COL_MAGENTA);
  g.drawLine(x1, y1, x2, y2, COL_TEXT);
}

// Corner HUD brackets + crosshair (static chrome around the skull).
static void drawHudChrome() {
  auto& g = display();
  const int L = 18, M = 6;
  // four corners (alternating accent colours like the artwork)
  uint16_t c1 = COL_ACCENT, c2 = COL_MAGENTA;
  // top-left
  g.drawFastHLine(M, M, L, c1); g.drawFastVLine(M, M, L, c1);
  // top-right
  g.drawFastHLine(DISPLAY_W - M - L, M, L, c2); g.drawFastVLine(DISPLAY_W - M, M, L, c2);
  // bottom-left
  g.drawFastHLine(M, DISPLAY_H - M, L, c1); g.drawFastVLine(M, DISPLAY_H - M - L, L, c1);
  // bottom-right
  g.drawFastHLine(DISPLAY_W - M - L, DISPLAY_H - M, L, c2);
  g.drawFastVLine(DISPLAY_W - M, DISPLAY_H - M - L, L, c2);
  // top crosshair
  g.drawCircle(DISPLAY_W / 2, 8, 3, COL_ACCENT);
  g.drawFastHLine(DISPLAY_W / 2 - 10, 8, 5, COL_TEXT_DIM);
  g.drawFastHLine(DISPLAY_W / 2 + 6, 8, 5, COL_TEXT_DIM);
}

// ---------------------------------------------------------------------------
void menu_boot_screen(uint32_t duration_ms) {
  auto& g = display();
  g.fillScreen(COL_BG);
  drawHudChrome();

  // Reveal skull edges progressively over ~60% of the duration.
  uint32_t reveal_ms = (duration_ms * 6) / 10;
  uint32_t per_edge  = reveal_ms / (SKULL_EDGES ? SKULL_EDGES : 1);
  uint32_t t0 = millis();
  for (int i = 0; i < SKULL_EDGES; i++) {
    neonLine(sx(SKULL[i][0]), sy(SKULL[i][1]), sx(SKULL[i][2]), sy(SKULL[i][3]));
    uint32_t target = t0 + per_edge * (i + 1);
    while ((int32_t)(target - millis()) > 0) yield();   // bounded, non-greedy
  }

  // Legend, bottom-anchored so it sits correctly on both 240 and 280 panels.
  const int by = DISPLAY_H;
  g.setTextDatum(textdatum_t::middle_center);
  g.setFont(&fonts::Font4);
  g.setTextColor(COL_TEXT);
  g.drawString("@luca3d_designs", DISPLAY_W / 2, by - 60);

  // "FPV SYSTEM" inside a chevron bar.
  g.drawFastHLine(40, by - 44, DISPLAY_W - 80, COL_ACCENT_DK);
  g.setFont(&fonts::Font2);
  g.setTextColor(COL_ACCENT);
  g.drawString("F P V   S Y S T E M", DISPLAY_W / 2, by - 40);

  // Firmware + hardware status.
  g.setTextColor(COL_TEXT_DIM);
  g.setFont(&fonts::Font0);
  char ver[40];
  snprintf(ver, sizeof(ver), "%s  v%s", FW_NAME, FW_VERSION);
  g.drawString(ver, DISPLAY_W / 2, by - 26);
  g.drawString(FPV_MODE_TURBO ? "TURBO  ST7789 OK" : "QUALITY  ST7789 OK",
               DISPLAY_W / 2, by - 17);

  // Animated "BOOTING..." dots on their own bottom line (its own clear band, so
  // it never erases the legend above it).
  g.setFont(&fonts::Font2);
  int dots = 0;
  while (millis() - t0 < duration_ms) {
    char b[20];
    snprintf(b, sizeof(b), "BOOTING%s", (dots == 0) ? "." : (dots == 1) ? ".." : "...");
    g.setTextColor(COL_MAGENTA);
    g.setTextDatum(textdatum_t::middle_center);
    g.fillRect(40, by - 14, DISPLAY_W - 80, 13, COL_BG);   // clear previous dots
    g.drawString(b, DISPLAY_W / 2, by - 7);
    dots = (dots + 1) % 3;
    uint32_t step = millis();
    while (millis() - step < 350 && millis() - t0 < duration_ms) yield();
  }
  g.setFont(&fonts::Font0);  // restore default small font
}

// ===========================================================================
//  Quick menu / System Info
// ===========================================================================
enum class MenuScreen : uint8_t { QUICK, SYSINFO, PERF };

enum {
  IT_REC = 0, IT_LED, IT_QUALITY, IT_OVERLAY, IT_RECONNECT, IT_PERF, IT_SYSINFO,
  IT_CLOSE, IT_COUNT
};
static const char* ITEM_LABEL[IT_COUNT] = {
  "Start / Stop REC", "Camera LED", "Quality", "Overlay",
  "Reconnect Stream", "Perf / Anti-Lag", "System Info", "Close Menu",
};

static bool       s_active = false;
static MenuScreen s_screen = MenuScreen::QUICK;
static int        s_sel = 0;
static bool       s_dirty = true;
static uint32_t   s_last_draw = 0;

static const char* qualityName() {
  switch (g_rx.quality_pref) { case 0: return "Low"; case 2: return "High"; default: return "Med"; }
}
static const char* overlayName() {
  switch (g_rx.overlay_mode) {
    case OVERLAY_MINIMAL: return "Minimal";
    case OVERLAY_OFFROAD: return "Offroad";
    default:              return "Full";
  }
}

void menu_open()  { s_active = true; s_screen = MenuScreen::QUICK; s_sel = 0; s_dirty = true; g_rx.state = RxState::MENU; }
void menu_close() { s_active = false; g_rx.state = RxState::LIVE; overlay_invalidate(); }
bool menu_is_active() { return s_active; }

static void applyQuality() {
  // Map preference -> esp_camera quality value (lower = better).
  long q = (g_rx.quality_pref == 0) ? 22 : (g_rx.quality_pref == 2) ? 8 : 12;
  telemetry_send_arg(CMD_SET_QUALITY, q);
}

static void activate(int item) {
  switch (item) {
    case IT_REC:       telemetry_send(CMD_REC_TOGGLE); break;
    case IT_LED:       telemetry_send(CMD_LED_TOGGLE); break;
    case IT_QUALITY:   g_rx.quality_pref = (g_rx.quality_pref + 1) % 3; applyQuality(); break;
    case IT_OVERLAY:   g_rx.overlay_mode = (g_rx.overlay_mode + 1) % 3;
                       telemetry_send_arg(CMD_SET_OVERLAY_MODE, g_rx.overlay_mode); break;
    case IT_RECONNECT: video_request_reconnect(); menu_close(); return;
    case IT_PERF:      s_screen = MenuScreen::PERF; break;
    case IT_SYSINFO:   s_screen = MenuScreen::SYSINFO; break;
    case IT_CLOSE:     menu_close(); return;
  }
  s_dirty = true;
}

bool menu_handle_button(ButtonEvent ev) {
  if (!s_active || ev == ButtonEvent::NONE) return s_active;

  if (s_screen == MenuScreen::SYSINFO || s_screen == MenuScreen::PERF) {
    // Any press returns to the quick menu.
    s_screen = MenuScreen::QUICK;
    s_dirty = true;
    return true;
  }

  switch (ev) {
    case ButtonEvent::SHORT:     s_sel = (s_sel + 1) % IT_COUNT; s_dirty = true; break;
    case ButtonEvent::LONG:      activate(s_sel); break;
    case ButtonEvent::VERY_LONG: menu_close(); break;
    default: break;
  }
  return true;
}

// ---- drawing ----
static void drawTitle(const char* title) {
  auto& g = display();
  g.fillRoundRect(6, 6, DISPLAY_W - 12, 26, 6, COL_PANEL);
  g.drawRoundRect(6, 6, DISPLAY_W - 12, 26, 6, COL_ACCENT_DK);
  g.setFont(&fonts::Font2);
  g.setTextDatum(textdatum_t::middle_left);
  g.setTextColor(COL_ACCENT);
  g.drawString(title, 14, 19);
}

static void drawQuick() {
  auto& g = display();
  g.fillScreen(COL_BG);
  drawTitle("QUICK MENU");

  const int top = 36, rowH = 23;
  g.setFont(&fonts::Font2);
  for (int i = 0; i < IT_COUNT; i++) {
    int y = top + i * rowH;
    bool sel = (i == s_sel);
    if (sel) {
      g.fillRoundRect(8, y, DISPLAY_W - 16, rowH - 3, 5, COL_PANEL);
      g.drawRoundRect(8, y, DISPLAY_W - 16, rowH - 3, 5, COL_ACCENT);
    }
    g.setTextDatum(textdatum_t::middle_left);
    g.setTextColor(sel ? COL_TEXT : COL_TEXT_DIM);
    g.drawString(ITEM_LABEL[i], 16, y + (rowH - 3) / 2);

    // value column for the cycling items
    const char* val = nullptr;
    if      (i == IT_QUALITY) val = qualityName();
    else if (i == IT_OVERLAY) val = overlayName();
    else if (i == IT_LED)     val = g_rx.cam_led_on ? "ON" : "OFF";
    else if (i == IT_REC)     val = g_rx.cam_recording ? "REC" : "--";
    if (val) {
      g.setTextDatum(textdatum_t::middle_right);
      g.setTextColor(sel ? COL_ACCENT : COL_TEXT_DIM);
      g.drawString(val, DISPLAY_W - 18, y + (rowH - 3) / 2);
    }
  }
  // footer hint
  g.setFont(&fonts::Font0);
  g.setTextDatum(textdatum_t::middle_center);
  g.setTextColor(COL_TEXT_DIM);
  g.drawString("tap=next  hold=select  hold++=back", DISPLAY_W / 2, DISPLAY_H - 8);
}

static void drawSysRow(int y, const char* k, const char* v) {
  auto& g = display();
  g.setFont(&fonts::Font2);
  g.setTextDatum(textdatum_t::middle_left);
  g.setTextColor(COL_TEXT_DIM); g.drawString(k, 14, y);
  g.setTextDatum(textdatum_t::middle_right);
  g.setTextColor(COL_TEXT);     g.drawString(v, DISPLAY_W - 14, y);
}

static void drawSysInfo() {
  auto& g = display();
  g.fillScreen(COL_BG);
  drawTitle("SYSTEM INFO");

  char b[40];
  int y = 48, dy = 21;
  drawSysRow(y, "Camera IP", CAMERA_IP);                                  y += dy;
  snprintf(b, sizeof(b), "%d dBm", g_rx.rssi);          drawSysRow(y, "RSSI", b);            y += dy;
  snprintf(b, sizeof(b), "%.0f / %.0f", g_rx.fps_video, g_rx.cam_fps);
                                                        drawSysRow(y, "FPS rx/cam", b);      y += dy;
  snprintf(b, sizeof(b), "%lu", (unsigned long)g_rx.frames_dropped);
                                                        drawSysRow(y, "Dropped", b);         y += dy;
  drawSysRow(y, "SD card", g_rx.cam_sd_ok ? "OK" : "MISSING");            y += dy;
  drawSysRow(y, "Recording", g_rx.cam_recording ? "ON" : "OFF");         y += dy;
  snprintf(b, sizeof(b), "%lus", (unsigned long)g_rx.cam_uptime_s);
                                                        drawSysRow(y, "Cam uptime", b);      y += dy;
  snprintf(b, sizeof(b), "%u KB", g_rx.cam_heap_kb);   drawSysRow(y, "Cam heap", b);         y += dy;
  snprintf(b, sizeof(b), "%u KB", g_rx.cam_psram_kb);  drawSysRow(y, "Cam PSRAM", b);

  g.setFont(&fonts::Font0);
  g.setTextDatum(textdatum_t::middle_center);
  g.setTextColor(COL_TEXT_DIM);
  g.drawString("press to go back", DISPLAY_W / 2, DISPLAY_H - 8);
}

// ---- PERF / anti-lag screen: the honest, real-time latency metrics ----------
static void drawPerf() {
  auto& g = display();
  g.fillScreen(COL_BG);
  drawTitle("PERF / ANTI-LAG");

  char b[40];
  int y = 44, dy = 18;
  // age coloured by how fresh it is (the whole point of the build)
  g.setFont(&fonts::Font2);
  uint16_t agecol = (g_rx.frame_age_ms < 120) ? COL_OK
                  : (g_rx.frame_age_ms < 220) ? COL_WARN : COL_ERR;
  snprintf(b, sizeof(b), "%lu ms", (unsigned long)g_rx.frame_age_ms);
  g.setTextDatum(textdatum_t::middle_left);
  g.setTextColor(COL_TEXT_DIM); g.drawString("Frame age", 14, y);
  g.setTextDatum(textdatum_t::middle_right);
  g.setTextColor(g_rx.clock_synced ? agecol : COL_TEXT_DIM);
  g.drawString(g_rx.clock_synced ? b : "sync...", DISPLAY_W - 14, y); y += dy;

  snprintf(b, sizeof(b), "%.0f/%.0f/%.0f", g_rx.cam_fps, g_rx.fps_rx, g_rx.fps_video);
  drawSysRow(y, "FPS c/r/d", b);                                          y += dy;
  snprintf(b, sizeof(b), "%lu ms", (unsigned long)(g_rx.decode_draw_us / 1000));
  drawSysRow(y, "Decode+draw", b);                                       y += dy;
  snprintf(b, sizeof(b), "%lu kbps", (unsigned long)g_rx.rx_kbps);
  drawSysRow(y, "RX rate", b);                                           y += dy;
  snprintf(b, sizeof(b), "%lu B", (unsigned long)g_rx.jpeg_size);
  drawSysRow(y, "JPEG size", b);                                         y += dy;
  snprintf(b, sizeof(b), "%u %%", g_rx.packet_loss);
  drawSysRow(y, "Packet loss", b);                                       y += dy;
  snprintf(b, sizeof(b), "%lu/%lu/%lu", (unsigned long)g_rx.dropped_old,
           (unsigned long)g_rx.dropped_timeout, (unsigned long)g_rx.dropped_stale);
  drawSysRow(y, "Drop o/t/s", b);                                        y += dy;
  snprintf(b, sizeof(b), "%lu", (unsigned long)g_rx.jpeg_err);
  drawSysRow(y, "JPEG errors", b);                                       y += dy;
  snprintf(b, sizeof(b), "%d dBm", g_rx.rssi);
  drawSysRow(y, "RSSI", b);                                              y += dy;
  snprintf(b, sizeof(b), "%lu KB", (unsigned long)(g_rx.free_heap / 1024));
  drawSysRow(y, "Heap free", b);                                         y += dy;
  snprintf(b, sizeof(b), "%lu KB", (unsigned long)(g_rx.free_psram / 1024));
  drawSysRow(y, "PSRAM free", b);

  g.setFont(&fonts::Font0);
  g.setTextDatum(textdatum_t::middle_center);
  g.setTextColor(COL_TEXT_DIM);
  g.drawString(FPV_MODE_TURBO ? "TURBO - press to go back"
                              : "QUALITY - press to go back", DISPLAY_W / 2, DISPLAY_H - 8);
}

void menu_draw() {
  if (!s_active) return;
  uint32_t now = millis();

  if (s_screen == MenuScreen::SYSINFO) {
    if (s_dirty || now - s_last_draw > 500) { drawSysInfo(); s_last_draw = now; s_dirty = false; }
    return;
  }
  if (s_screen == MenuScreen::PERF) {
    // live metrics ~4 Hz
    if (s_dirty || now - s_last_draw > 250) { drawPerf(); s_last_draw = now; s_dirty = false; }
    return;
  }
  if (s_dirty) { drawQuick(); s_last_draw = now; s_dirty = false; }
}

// ===========================================================================
//  Error screen
// ===========================================================================
void menu_draw_error() {
  static RxError last = RxError::NONE;
  static uint32_t last_ms = 0;
  uint32_t now = millis();
  // Only repaint on change or slow heartbeat (keeps the spinner subtle).
  bool changed = (g_rx.error != last);
  if (!changed && now - last_ms < 400) return;
  last = g_rx.error; last_ms = now;

  auto& g = display();
  g.fillScreen(COL_BG);

  const char* title = "ERROR";
  const char* hint  = "";
  switch (g_rx.error) {
    case RxError::WIFI_LOST:        title = "WI-FI LOST";       hint = "Reconnecting to RC-FPV-CAM..."; break;
    case RxError::STREAM_LOST:      title = "STREAM LOST";      hint = "Re-establishing video link..."; break;
    case RxError::CAMERA_NOT_FOUND: title = "CAMERA NOT FOUND"; hint = "Is the ESP32-CAM powered?";     break;
    case RxError::JPEG_DECODE:      title = "DECODE ERROR";     hint = "Corrupt frame, recovering...";  break;
    case RxError::SD_MISSING:       title = "NO SD CARD";       hint = "Recording unavailable.";        break;
    default:                        title = "RECONNECTING";     hint = "Please wait...";                break;
  }

  // alert panel
  g.fillRoundRect(16, 70, DISPLAY_W - 32, 100, 10, COL_PANEL);
  g.drawRoundRect(16, 70, DISPLAY_W - 32, 100, 10, COL_ERR);

  // warning glyph (triangle + bang)
  int cx = DISPLAY_W / 2;
  g.fillTriangle(cx, 84, cx - 16, 112, cx + 16, 112, COL_ERR);
  g.fillRect(cx - 1, 92, 3, 12, COL_BG);
  g.fillRect(cx - 1, 107, 3, 3, COL_BG);

  g.setFont(&fonts::Font2);
  g.setTextDatum(textdatum_t::middle_center);
  g.setTextColor(COL_TEXT);     g.drawString(title, cx, 130);
  g.setFont(&fonts::Font0);
  g.setTextColor(COL_TEXT_DIM); g.drawString(hint, cx, 152);

  // animated dots at the bottom
  int n = (now / 400) % 4;
  char dots[5] = "    ";
  for (int i = 0; i < n; i++) dots[i] = '.';
  g.setFont(&fonts::Font4);
  g.setTextColor(COL_ACCENT);
  g.drawString(dots, cx, 190);
}
