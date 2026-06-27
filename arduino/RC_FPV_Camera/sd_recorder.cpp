// =============================================================================
//  sd_recorder.cpp  -  ASYNCHRONOUS microSD JPEG-sequence recorder
// -----------------------------------------------------------------------------
//  Anti-lag rule: the SD card MUST NEVER touch the video path.
//
//  Design:
//    * A dedicated low-priority FreeRTOS task (pinned to the non-loop core) owns
//      the bulk SD_MMC writes.
//    * Handoff is a SINGLE PSRAM slot (MAX_JPEG_SIZE). The video loop, at most
//      every SD_REC_FRAME_SKIP-th frame, memcpy's the latest JPEG into the slot
//      and signals the writer - that memcpy is the ONLY recording cost on the
//      video path (tens of microseconds).
//    * If the writer is still busy when a new frame is offered, that frame is
//      DROPPED (rec_dropped++). No queue -> SD latency can never pile up behind
//      the live stream.
//    * A mutex serialises SD_MMC structural ops (mkdir/probe in sd_rec_start vs
//      file writes in the task) so the two contexts never collide.
//    * TURBO profile (SD_RECORDING_ALLOWED==0) compiles recording out of the
//      hot path entirely and refuses REC commands.
//
//  We store a JPG sequence (no on-MCU muxing). Turn it into a video on a PC:
//    ffmpeg -framerate 12 -i frame_%06d.jpg -c:v libx264 -pix_fmt yuv420p out.mp4
// =============================================================================
#include "sd_recorder.h"
#include "config.h"
#include "app_state.h"
#include <Arduino.h>

#if ENABLE_SD_RECORDING

#include <FS.h>
#include <SD_MMC.h>
#include <esp_heap_caps.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

static bool s_mounted = false;

#if SD_RECORDING_ALLOWED
// ---- state used only when recording is actually compiled in ------------------
static char              s_dir[40]   = {0};   // /FPV/REC_xxxx
static uint32_t          s_frame_in  = 0;     // frames offered since rec start
static uint8_t*          s_slot      = nullptr;
static volatile uint32_t s_slot_len  = 0;
static volatile bool     s_slot_full = false;
static SemaphoreHandle_t s_sem       = nullptr;   // "slot has data" signal
static SemaphoreHandle_t s_sd_mtx    = nullptr;   // serialises SD_MMC structural ops
static TaskHandle_t      s_task      = nullptr;

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

static void sdWriterTask(void*) {
  for (;;) {
    if (xSemaphoreTake(s_sem, portMAX_DELAY) != pdTRUE) continue;
    if (!s_slot_full) continue;

    if (g_status.recording && s_mounted) {
      char path[64];
      snprintf(path, sizeof(path), "%s/frame_%06lu.jpg",
               s_dir, (unsigned long)(g_status.rec_frame_count + 1));
      xSemaphoreTake(s_sd_mtx, portMAX_DELAY);
      File f = SD_MMC.open(path, FILE_WRITE);
      if (f) {
        size_t w = f.write((const uint8_t*)s_slot, s_slot_len);
        f.close();
        if (w == s_slot_len) { g_status.rec_frame_count++;
                               g_status.error_flags &= ~FPV_ERR_SD_WRITE; }
        else                 { g_status.error_flags |= FPV_ERR_SD_WRITE; }
      } else {
        g_status.error_flags |= FPV_ERR_SD_WRITE;
      }
      xSemaphoreGive(s_sd_mtx);
    }
    s_slot_full = false;     // slot free for the next frame
  }
}
#endif // SD_RECORDING_ALLOWED

bool sd_begin() {
  if (!SD_MMC.begin("/sdcard", SD_ONE_BIT_MODE)) {
    LOGE("microSD mount failed (no card / bad card / wiring). Streaming only.");
    s_mounted = false; g_status.sd_available = false; return false;
  }
  if (SD_MMC.cardType() == CARD_NONE) {
    LOGE("No microSD card detected.");
    s_mounted = false; g_status.sd_available = false; return false;
  }
  if (!SD_MMC.exists(SD_ROOT_DIR)) SD_MMC.mkdir(SD_ROOT_DIR);
  s_mounted = true; g_status.sd_available = true;
  LOGI("microSD mounted (%lluMB)", SD_MMC.cardSize() / (1024ULL * 1024ULL));

#if SD_RECORDING_ALLOWED
  s_slot = (uint8_t*)heap_caps_malloc(MAX_JPEG_SIZE, MALLOC_CAP_SPIRAM);
  if (!s_slot) s_slot = (uint8_t*)malloc(MAX_JPEG_SIZE);
  s_sem    = xSemaphoreCreateBinary();
  s_sd_mtx = xSemaphoreCreateMutex();
  if (s_slot && s_sem && s_sd_mtx) {
    xTaskCreatePinnedToCore(sdWriterTask, "sd_writer", SD_WRITER_STACK,
                            nullptr, SD_WRITER_PRIO, &s_task, SD_WRITER_CORE);
    LOGI("SD writer task on core %d (prio %d)", SD_WRITER_CORE, SD_WRITER_PRIO);
  } else {
    LOGE("SD writer alloc failed - recording disabled");
  }
#endif
  return true;
}

bool sd_rec_start() {
#if !SD_RECORDING_ALLOWED
  LOGI("REC blocked: TURBO profile (set FPV_MODE_TURBO 0 to record)");
  return false;
#else
  if (!s_mounted || !s_slot || !s_sem || !s_sd_mtx) { LOGE("REC start: SD not ready"); return false; }
  if (g_status.recording) return true;

  xSemaphoreTake(s_sd_mtx, portMAX_DELAY);
  uint32_t idx = nextRecIndex();
  snprintf(s_dir, sizeof(s_dir), SD_ROOT_DIR "/REC_%04lu", (unsigned long)idx);
  bool ok = SD_MMC.mkdir(s_dir);
  xSemaphoreGive(s_sd_mtx);
  if (!ok) {
    LOGE("Could not create %s", s_dir);
    g_status.error_flags |= FPV_ERR_SD_WRITE;
    return false;
  }
  s_frame_in               = 0;
  g_status.rec_index       = idx;
  g_status.rec_frame_count = 0;
  g_status.rec_dropped     = 0;
  g_status.error_flags    &= ~FPV_ERR_SD_WRITE;
  g_status.recording       = true;
  LOGI("Recording -> %s", s_dir);
  return true;
#endif
}

void sd_rec_stop() {
  if (!g_status.recording) return;
  g_status.recording = false;
  LOGI("Recording stopped (%lu frames, %lu dropped)",
       (unsigned long)g_status.rec_frame_count, (unsigned long)g_status.rec_dropped);
}

void sd_rec_toggle() {
  if (g_status.recording) sd_rec_stop();
  else                    sd_rec_start();
}

void sd_rec_offer(camera_fb_t* fb) {
#if !SD_RECORDING_ALLOWED
  (void)fb;
#else
  if (!g_status.recording || !s_mounted || !fb || !s_slot) return;

  s_frame_in++;
  if (s_frame_in % SD_REC_FRAME_SKIP != 0) return;     // sample, don't record all
  if (s_slot_full)             { g_status.rec_dropped++; return; }   // writer busy
  if (fb->len > MAX_JPEG_SIZE) { g_status.rec_dropped++; return; }

  memcpy(s_slot, fb->buf, fb->len);   // the ONLY recording cost on the video path
  s_slot_len  = fb->len;
  s_slot_full = true;
  xSemaphoreGive(s_sem);              // wake the writer task
#endif
}

#else  // ENABLE_SD_RECORDING == 0 : compile recording out entirely

bool sd_begin()                 { g_status.sd_available = false; return false; }
bool sd_rec_start()             { return false; }
void sd_rec_stop()              {}
void sd_rec_toggle()            {}
void sd_rec_offer(camera_fb_t*) {}

#endif // ENABLE_SD_RECORDING
