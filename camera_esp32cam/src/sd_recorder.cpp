// =============================================================================
//  sd_recorder.cpp  -  optional microSD JPEG-sequence recorder
// -----------------------------------------------------------------------------
//  Strategy (per the brief): never block the stream.
//    * We write at most every SD_REC_FRAME_SKIP-th frame.
//    * Each write is timed; if a write exceeds SD_WRITE_SLOW_MS we *increase*
//      the effective skip for a while so a slow/old card cannot drag latency
//      down. The skip relaxes back toward the configured value over time.
//    * If the card is absent the whole module degrades to no-ops and the rest
//      of the firmware keeps streaming (sd_available stays false).
//
//  We deliberately store a JPG sequence rather than muxing video on the MCU
//  (encoding a container on an ESP32 is slow and fragile). Post-process the
//  frames into a video on a PC with ffmpeg (see README).
// =============================================================================
#include "sd_recorder.h"
#include "config.h"
#include "app_state.h"
#include <Arduino.h>

#if ENABLE_SD_RECORDING

#include <FS.h>
#include <SD_MMC.h>

static bool     s_mounted        = false;
static char     s_dir[40]        = {0};   // /FPV/REC_xxxx
static uint32_t s_frame_in       = 0;     // frames offered since rec start
static uint16_t s_dyn_skip       = SD_REC_FRAME_SKIP;

// Find the next free /FPV/REC_xxxx index by probing existing folders.
static uint32_t nextRecIndex() {
  uint32_t idx = 1;
  char path[40];
  while (idx < 9999) {
    snprintf(path, sizeof(path), SD_ROOT_DIR "/REC_%04lu", (unsigned long)idx);
    if (!SD_MMC.exists(path)) break;
    idx++;
  }
  return idx;
}

bool sd_begin() {
  // 1-bit mode frees GPIO4 (camera LED) and is the robust default. The second
  // arg `mode1bit` selects it. Third arg false = don't format on fail.
  if (!SD_MMC.begin("/sdcard", SD_ONE_BIT_MODE)) {
    LOGE("microSD mount failed (no card / bad card / wiring). Streaming only.");
    s_mounted = false;
    g_status.sd_available = false;
    return false;
  }
  uint8_t type = SD_MMC.cardType();
  if (type == CARD_NONE) {
    LOGE("No microSD card detected.");
    s_mounted = false;
    g_status.sd_available = false;
    return false;
  }
  if (!SD_MMC.exists(SD_ROOT_DIR)) SD_MMC.mkdir(SD_ROOT_DIR);

  s_mounted = true;
  g_status.sd_available = true;
  LOGI("microSD mounted (%lluMB)", SD_MMC.cardSize() / (1024ULL * 1024ULL));
  return true;
}

bool sd_rec_start() {
  if (!s_mounted)        { LOGE("REC start ignored: no SD"); return false; }
  if (g_status.recording) return true;

  uint32_t idx = nextRecIndex();
  snprintf(s_dir, sizeof(s_dir), SD_ROOT_DIR "/REC_%04lu", (unsigned long)idx);
  if (!SD_MMC.mkdir(s_dir)) {
    LOGE("Could not create %s", s_dir);
    g_status.error_flags |= FPV_ERR_SD_WRITE;
    return false;
  }
  s_frame_in              = 0;
  s_dyn_skip              = SD_REC_FRAME_SKIP;
  g_status.rec_index      = idx;
  g_status.rec_frame_count= 0;
  g_status.recording      = true;
  g_status.error_flags   &= ~FPV_ERR_SD_WRITE;
  LOGI("Recording -> %s", s_dir);
  return true;
}

void sd_rec_stop() {
  if (!g_status.recording) return;
  g_status.recording = false;
  LOGI("Recording stopped (%lu frames in %s)",
       (unsigned long)g_status.rec_frame_count, s_dir);
}

void sd_rec_toggle() {
  if (g_status.recording) sd_rec_stop();
  else                    sd_rec_start();
}

void sd_rec_offer(camera_fb_t* fb) {
  if (!g_status.recording || !s_mounted || !fb) return;

  s_frame_in++;
  if (s_frame_in % s_dyn_skip != 0) return;   // frame-skip throttle

  char path[64];
  snprintf(path, sizeof(path), "%s/frame_%06lu.jpg",
           s_dir, (unsigned long)(g_status.rec_frame_count + 1));

  uint32_t t0 = millis();
  File f = SD_MMC.open(path, FILE_WRITE);
  if (!f) {
    LOGE("SD open failed: %s", path);
    g_status.error_flags |= FPV_ERR_SD_WRITE;
    return;
  }
  size_t written = f.write(fb->buf, fb->len);
  f.close();
  uint32_t dt = millis() - t0;

  if (written != fb->len) {
    LOGE("SD short write (%u/%u) - card full?", (unsigned)written, (unsigned)fb->len);
    g_status.error_flags |= FPV_ERR_SD_WRITE;
    return;
  }
  g_status.rec_frame_count++;
  g_status.error_flags &= ~FPV_ERR_SD_WRITE;

  // Adaptive throttle: a slow write widens the skip; fast writes relax it.
  if (dt > SD_WRITE_SLOW_MS) {
    if (s_dyn_skip < 10) s_dyn_skip++;
    LOGV("Slow SD write %lums -> skip=%u", (unsigned long)dt, s_dyn_skip);
  } else if (s_dyn_skip > SD_REC_FRAME_SKIP) {
    s_dyn_skip--;
  }
}

#else  // ENABLE_SD_RECORDING == 0 : compile recording out entirely

bool sd_begin()                       { g_status.sd_available = false; return false; }
bool sd_rec_start()                   { return false; }
void sd_rec_stop()                    {}
void sd_rec_toggle()                  {}
void sd_rec_offer(camera_fb_t*)       {}

#endif // ENABLE_SD_RECORDING
