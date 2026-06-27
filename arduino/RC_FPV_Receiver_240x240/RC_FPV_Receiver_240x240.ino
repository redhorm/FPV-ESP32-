// =============================================================================
//  RC_FPV_Receiver_240x240.ino  -  ESP32-S3 + ST7789 anti-lag FPV receiver
// -----------------------------------------------------------------------------
//  Display variant: DISPLAY_PANEL_280 = 0  (240x240)
//  setup()/loop() live in main.cpp (compiled alongside this sketch).
//
//  ARDUINO IDE SETTINGS (Tools menu):
//    Board:             "ESP32S3 Dev Module"  (esp32 core by Espressif)
//    USB CDC On Boot:   Enabled
//    USB Mode:          Hardware CDC and JTAG
//    PSRAM:             OPI PSRAM
//    Flash Size:        16MB (or your board's size)
//    Partition Scheme:  Default 4MB with spiffs (or larger)
//    Core Debug Level:  None
//  Libraries (Library Manager): "LovyanGFX" by lovyan03.
//  The boot artwork is embedded in boot_image.h. Anti-lag options in config.h.
//  NOTE: keep DISPLAY_PANEL_280 and FPV_MODE_TURBO matching the camera profile.
// =============================================================================
