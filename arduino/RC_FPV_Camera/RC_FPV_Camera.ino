// =============================================================================
//  RC_FPV_Camera.ino  -  ESP32-CAM (AI-Thinker) anti-lag FPV camera
// -----------------------------------------------------------------------------
//  setup()/loop() live in main.cpp (compiled alongside this sketch). This .ino
//  only marks the sketch folder for the Arduino IDE.
//
//  ARDUINO IDE SETTINGS (Tools menu):
//    Board:            "AI Thinker ESP32-CAM"   (esp32 core by Espressif)
//    PSRAM:            Enabled
//    Partition Scheme: Huge APP (3MB No OTA/1MB SPIFFS)
//    Core Debug Level: None
//    Upload Speed:     115200   (most reliable via USB-TTL; GPIO0->GND to flash)
//  Libraries: none required for the camera (esp_camera/WiFi/SD_MMC ship with the
//  ESP32 core). "Adafruit MPU6050" + "Adafruit Unified Sensor" only if you set
//  ENABLE_MPU6050 1 in config.h.
//  Edit anti-lag options (TURBO/QUALITY etc.) in config.h.
// =============================================================================
