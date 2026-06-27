// =============================================================================
//  camera_service.cpp  -  OV2640 lifecycle, frame grab, LED, optional MPU6050
// =============================================================================
#include "camera_service.h"
#include "camera_pins.h"
#include "config.h"
#include "app_state.h"
#include <Arduino.h>

#if ENABLE_MPU6050
  #include <Wire.h>
  #include <Adafruit_MPU6050.h>
  #include <Adafruit_Sensor.h>
  static Adafruit_MPU6050 s_mpu;
  static float s_roll = 0.0f, s_pitch = 0.0f;     // filtered estimate
  static uint32_t s_mpu_last_us = 0;
#endif

// -----------------------------------------------------------------------------
bool camera_init() {
  camera_config_t cfg = {};
  cfg.ledc_channel = LEDC_CHANNEL_0;
  cfg.ledc_timer   = LEDC_TIMER_0;
  cfg.pin_d0       = Y2_GPIO_NUM;
  cfg.pin_d1       = Y3_GPIO_NUM;
  cfg.pin_d2       = Y4_GPIO_NUM;
  cfg.pin_d3       = Y5_GPIO_NUM;
  cfg.pin_d4       = Y6_GPIO_NUM;
  cfg.pin_d5       = Y7_GPIO_NUM;
  cfg.pin_d6       = Y8_GPIO_NUM;
  cfg.pin_d7       = Y9_GPIO_NUM;
  cfg.pin_xclk     = XCLK_GPIO_NUM;
  cfg.pin_pclk     = PCLK_GPIO_NUM;
  cfg.pin_vsync    = VSYNC_GPIO_NUM;
  cfg.pin_href     = HREF_GPIO_NUM;
  // SCCB pin field was renamed sscb->sccb between arduino-esp32 2.x and 3.x.
#if defined(ESP_ARDUINO_VERSION) && (ESP_ARDUINO_VERSION >= ESP_ARDUINO_VERSION_VAL(3, 0, 0))
  cfg.pin_sccb_sda = SIOD_GPIO_NUM;
  cfg.pin_sccb_scl = SIOC_GPIO_NUM;
#else
  cfg.pin_sscb_sda = SIOD_GPIO_NUM;
  cfg.pin_sscb_scl = SIOC_GPIO_NUM;
#endif
  cfg.pin_pwdn     = PWDN_GPIO_NUM;
  cfg.pin_reset    = RESET_GPIO_NUM;
  cfg.xclk_freq_hz = CAM_XCLK_HZ;
  cfg.pixel_format = PIXFORMAT_JPEG;          // hardware JPEG -> small frames
  cfg.frame_size   = CAM_FRAMESIZE;
  cfg.jpeg_quality = CAM_JPEG_QUALITY;

  // PSRAM is required for double buffering. "grab latest" + 2 buffers is the
  // low-latency sweet spot: the sensor keeps filling the spare buffer while we
  // transmit, and we always dequeue the freshest frame.
  if (psramFound()) {
    cfg.fb_count   = CAM_FB_COUNT;
    cfg.fb_location= CAMERA_FB_IN_PSRAM;
    cfg.grab_mode  = CAMERA_GRAB_LATEST;
  } else {
    // Graceful fallback: smaller frame, single buffer in DRAM.
    LOGE("No PSRAM found - falling back to single buffer (higher latency)");
    cfg.frame_size = FRAMESIZE_QVGA;
    cfg.fb_count   = 1;
    cfg.fb_location= CAMERA_FB_IN_DRAM;
    cfg.grab_mode  = CAMERA_GRAB_WHEN_EMPTY;
  }

  esp_err_t err = esp_camera_init(&cfg);
  if (err != ESP_OK) {
    LOGE("esp_camera_init failed: 0x%x", err);
    g_status.error_flags |= FPV_ERR_CAMERA;
    return false;
  }

  // Sensible defaults for an FPV cab/crawler view.
  sensor_t* s = esp_camera_sensor_get();
  if (s) {
    s->set_quality(s, CAM_JPEG_QUALITY);
    s->set_brightness(s, 0);
    s->set_contrast(s, 0);
    s->set_saturation(s, 0);
    // OV2640 modules are commonly mounted upside-down; expose via these if so.
    s->set_vflip(s, 0);
    s->set_hmirror(s, 0);
  }
  g_status.jpeg_quality = CAM_JPEG_QUALITY;

  // Camera LED: hard OFF on boot regardless of config (see README GPIO4 notes).
#if ENABLE_CAMERA_LED
  pinMode(CAMERA_LED_GPIO, OUTPUT);
  digitalWrite(CAMERA_LED_GPIO, LOW);
  g_status.led_on = CAMERA_LED_ON_BOOT;   // expected to be false
  if (g_status.led_on) digitalWrite(CAMERA_LED_GPIO, HIGH);
#endif

  LOGI("Camera init OK (framesize=%d quality=%d fb=%d)",
       CAM_FRAMESIZE, CAM_JPEG_QUALITY, cfg.fb_count);
  return true;
}

// -----------------------------------------------------------------------------
camera_fb_t* camera_grab() {
  return esp_camera_fb_get();
}

void camera_return(camera_fb_t* fb) {
  if (fb) esp_camera_fb_return(fb);
}

// -----------------------------------------------------------------------------
void camera_set_quality(uint8_t q) {
  if (q < 4)  q = 4;
  if (q > 63) q = 63;
  sensor_t* s = esp_camera_sensor_get();
  if (s) {
    s->set_quality(s, q);
    g_status.jpeg_quality = q;
    LOGI("JPEG quality set to %d", q);
  }
}

// -----------------------------------------------------------------------------
//  Camera LED. Manual control only.
// -----------------------------------------------------------------------------
void camera_led_set(bool on) {
#if ENABLE_CAMERA_LED
  digitalWrite(CAMERA_LED_GPIO, on ? HIGH : LOW);
  g_status.led_on = on;
  LOGI("Camera LED %s", on ? "ON" : "OFF");
#else
  (void)on;
  LOGI("Camera LED disabled at compile time (ENABLE_CAMERA_LED=0)");
#endif
}

void camera_led_toggle() { camera_led_set(!g_status.led_on); }
bool camera_led_state()  { return g_status.led_on; }

// -----------------------------------------------------------------------------
//  MPU6050 (optional). All bodies compile to no-ops when ENABLE_MPU6050==0.
// -----------------------------------------------------------------------------
void camera_mpu_init() {
#if ENABLE_MPU6050
  Wire.begin(MPU_I2C_SDA, MPU_I2C_SCL, MPU_I2C_FREQ_HZ);
  if (!s_mpu.begin(0x68, &Wire)) {
    LOGE("MPU6050 not found on I2C (SDA=%d SCL=%d)", MPU_I2C_SDA, MPU_I2C_SCL);
    g_status.mpu_available = false;
    g_status.error_flags  |= FPV_ERR_MPU;
    return;
  }
  s_mpu.setAccelerometerRange(MPU6050_RANGE_4_G);
  s_mpu.setGyroRange(MPU6050_RANGE_500_DEG);
  s_mpu.setFilterBandwidth(MPU6050_BAND_21_HZ);
  g_status.mpu_available = true;
  s_mpu_last_us = micros();
  LOGI("MPU6050 ready");
#else
  g_status.mpu_available = false;
#endif
}

void camera_mpu_update() {
#if ENABLE_MPU6050
  if (!g_status.mpu_available) return;
  sensors_event_t a, gyro, temp;
  s_mpu.getEvent(&a, &gyro, &temp);

  // Accelerometer-derived angles (absolute but noisy).
  float acc_roll  = atan2f(a.acceleration.y, a.acceleration.z) * 57.2957795f;
  float acc_pitch = atan2f(-a.acceleration.x,
                    sqrtf(a.acceleration.y * a.acceleration.y +
                          a.acceleration.z * a.acceleration.z)) * 57.2957795f;

  // dt for gyro integration.
  uint32_t now = micros();
  float dt = (now - s_mpu_last_us) * 1e-6f;
  s_mpu_last_us = now;
  if (dt <= 0.0f || dt > 0.5f) dt = 0.0f;   // guard against stalls/overflow

  // Complementary filter: gyro for fast motion, accel to kill drift.
  const float A = MPU_FILTER_ALPHA;
  s_roll  = A * (s_roll  + gyro.gyro.x * 57.2957795f * dt) + (1 - A) * acc_roll;
  s_pitch = A * (s_pitch + gyro.gyro.y * 57.2957795f * dt) + (1 - A) * acc_pitch;

  g_status.roll  = s_roll;
  g_status.pitch = s_pitch;
#endif
}
