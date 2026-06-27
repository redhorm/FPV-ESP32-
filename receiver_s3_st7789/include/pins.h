// =============================================================================
//  pins.h  -  RC_FPV_SCALER_PRO :: ESP32-S3 receiver pin map
// -----------------------------------------------------------------------------
//  *** THE ST7789 PINS BELOW ARE FIXED BY THE PRODUCT SPEC. DO NOT CHANGE. ***
//  They are configured in-project (LovyanGFX, see jpeg_renderer.cpp) so you do
//  NOT have to edit any global library file (no TFT_eSPI User_Setup.h hacks).
// =============================================================================
#pragma once

// ---- ST7789 240x240 SPI display (mandatory, invariant) ----------------------
#define TFT_SCLK   12
#define TFT_MOSI   11
#define TFT_DC      9
#define TFT_CS     10
#define TFT_RST    14
#define TFT_BL     21    // backlight (active high)

// ---- Onboard controls / indicators ------------------------------------------
#define PIN_BOOT_BUTTON   0    // BOOT button (active LOW, has external pull-up)
#define PIN_RGB_LED      48    // WS2812 onboard RGB on many S3 DevKitC-1 boards

// ST7789 panels have no MISO; LovyanGFX is told -1 for that line.
#define TFT_MISO   -1
