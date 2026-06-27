// =============================================================================
//  camera_pins.h  -  AI-Thinker ESP32-CAM (OV2640) pin map
// -----------------------------------------------------------------------------
//  These are the fixed, well-known AI-Thinker pins. DO NOT change them unless
//  you physically have a different ESP32-CAM variant. The SD card (SD_MMC) uses
//  GPIO2/4/12/13/14/15; in 1-bit mode (see config.h SD_ONE_BIT_MODE) only
//  GPIO2 (D0) + CLK(14) + CMD(15) are used, freeing GPIO4 for the camera LED.
// =============================================================================
#pragma once

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
