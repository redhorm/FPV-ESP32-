// =============================================================================
//  camera_pins.h  -  AI-Thinker ESP32-CAM (OV2640) pin map
// -----------------------------------------------------------------------------
//  These are the fixed, well-known AI-Thinker pins. DO NOT change them unless
//  you physically have a different ESP32-CAM variant. The SD card (SD_MMC) uses
//  GPIO2/4/12/13/14/15; in 1-bit mode (see config.h SD_ONE_BIT_MODE) only
//  GPIO2 (D0) + CLK(14) + CMD(15) are used, freeing GPIO4 for the camera LED.
// =============================================================================
#pragma once

// Profile sourced from documented dual-USB-C ESP32-S3-CAM boards.
// Verify against the PCB/schematic before flashing; S3-CAM clones vary.
// Do NOT use AI-Thinker ESP32-CAM GPIOs on an ESP32-S3!
#if defined(FPV_BOARD_ESP32S3_CAM)
#if !defined(CONFIG_IDF_TARGET_ESP32S3)
#error "FPV_BOARD_ESP32S3_CAM requires an ESP32-S3 target"
#endif
#define PWDN_GPIO_NUM     -1
#define RESET_GPIO_NUM    -1
#define XCLK_GPIO_NUM      15
#define SIOD_GPIO_NUM      4
#define SIOC_GPIO_NUM      5
#define Y9_GPIO_NUM        16
#define Y8_GPIO_NUM        17
#define Y7_GPIO_NUM        18
#define Y6_GPIO_NUM        12
#define Y5_GPIO_NUM        10
#define Y4_GPIO_NUM        8
#define Y3_GPIO_NUM        9
#define Y2_GPIO_NUM        11
#define VSYNC_GPIO_NUM     6
#define HREF_GPIO_NUM      7
#define PCLK_GPIO_NUM      13

// microSD integrated slot, 1-bit SD_MMC mode; setPins(clk, cmd, d0).
#define FPV_SD_CLK_GPIO     39
#define FPV_SD_CMD_GPIO     38
#define FPV_SD_D0_GPIO      40
#else  // original AI-Thinker ESP32-CAM: pinout unchanged
#define PWDN_GPIO_NUM     32
#define RESET_GPIO_NUM    -1
#define XCLK_GPIO_NUM      0
#define SIOD_GPIO_NUM     26   // SCCB SDA
#define SIOC_GPIO_NUM     27   // SCCB SCL

#define Y9_GPIO_NUM       35
#define Y8_GPIO_NUM       34
#define Y7_GPIO_NUM       39
#define Y6_GPIO_NUM       36
#define Y5_GPIO_NUM       21
#define Y4_GPIO_NUM       19
#define Y3_GPIO_NUM       18
#define Y2_GPIO_NUM        5
#define VSYNC_GPIO_NUM    25
#define HREF_GPIO_NUM     23
#define PCLK_GPIO_NUM     22

#endif  // FPV_BOARD_ESP32S3_CAM
