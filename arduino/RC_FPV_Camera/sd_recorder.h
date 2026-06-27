// sd_recorder.h - optional microSD JPEG sequence recorder (non-blocking-aware)
#pragma once
#include <esp_camera.h>
#include <stdbool.h>

// Mount the SD card (1-bit mode per config). Safe to call when no card is
// present: sets g_status.sd_available=false and the rest of the firmware keeps
// streaming. Returns true if a card was mounted.
bool sd_begin();

// Start a new recording session -> creates /FPV/REC_xxxx. No-op if SD absent or
// already recording. Returns true if recording is now active.
bool sd_rec_start();
void sd_rec_stop();
void sd_rec_toggle();

// Offer a frame to the recorder. Internally applies frame-skip and adaptive
// throttling so it never stalls the stream. Cheap no-op when not recording.
void sd_rec_offer(camera_fb_t* fb);
