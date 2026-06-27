// camera_service.h - OV2640 init, frame grab, quality/LED control, optional MPU
#pragma once
#include <esp_camera.h>
#include <stdbool.h>
#include <stdint.h>

// Initialise the OV2640 with the settings from config.h. Returns false and
// sets FPV_ERR_CAMERA on failure (firmware then enters CAMERA_ERROR).
bool camera_init();

// Grab the freshest frame. Caller MUST call camera_return() when done. May
// return nullptr transiently; callers treat that as "skip this iteration".
camera_fb_t* camera_grab();
void         camera_return(camera_fb_t* fb);

// Runtime JPEG quality (4..63, lower = better). Clamped internally.
void         camera_set_quality(uint8_t q);

// Camera LED (GPIO4). Honors ENABLE_CAMERA_LED; never auto-on.
void         camera_led_set(bool on);
void         camera_led_toggle();
bool         camera_led_state();

// Optional MPU6050. When ENABLE_MPU6050==0 these are no-ops and roll/pitch
// stay at 0. When enabled, call camera_mpu_update() periodically.
void         camera_mpu_init();
void         camera_mpu_update();   // updates g_status.roll / g_status.pitch
