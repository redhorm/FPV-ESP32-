# WIRING — RC_FPV_SCALER_PRO

This document lists every pin used by both boards. **No optional hardware is
required.** The system runs as: ESP32-CAM (AP + camera + SD) ⇄ Wi-Fi ⇄
ESP32-S3 (ST7789 monitor).

---

## 1. ESP32-CAM (AI-Thinker, OV2640)

The camera pins are the standard AI-Thinker map (`camera_esp32cam/include/camera_pins.h`)
and **must not be changed** on this board.

| Function        | GPIO | Notes                                            |
|-----------------|------|--------------------------------------------------|
| PWDN            | 32   | camera power-down                                |
| XCLK            | 0    | camera master clock (also strapping pin)         |
| SIOD / SIOC     | 26 / 27 | SCCB (camera I²C control)                      |
| Y2..Y9          | 5,18,19,21,36,39,34,35 | parallel data                     |
| VSYNC/HREF/PCLK | 25 / 23 / 22 | sync signals                               |
| **LED (flash)** | **4** | white LED **AND** SD_DATA1 (see below) — OFF by default |
| microSD (1-bit) | 2 (D0), 14 (CLK), 15 (CMD) | `SD_MMC` 1-bit mode          |

### microSD — why 1-bit mode
The SD card in the on-board slot can run in 4-bit mode (GPIO 2/4/12/13) or
1-bit mode (GPIO 2 only for data). We mount it **1-bit** (`SD_ONE_BIT_MODE = true`)
for two reasons:

1. **It frees GPIO4** so the camera LED can be controlled without fighting the
   SD bus.
2. 1-bit is far more reliable on cheap AI-Thinker clones.

The trade-off is lower SD write bandwidth — which is fine, because we only
write a throttled JPEG sequence (see `LATENCY_NOTES.md` / README recording
section).

### ⚠ GPIO4 + microSD
GPIO4 is shared between the flash LED and SD_DATA1. Even in 1-bit mode the LED
can inject noise. The firmware keeps the LED **OFF at boot and never turns it
on automatically** (`CAMERA_LED_ON_BOOT = false`). Only `LED_TOGGLE` from the
receiver switches it. If your card misbehaves, leave the LED off
(`ENABLE_CAMERA_LED = false`).

### Power
Feed the ESP32-CAM from a solid **5 V / ≥1 A** source on the 5V pin. Brown-outs
during Wi-Fi TX are the #1 cause of camera init failures and reboots.

---

## 2. ESP32-S3 DevKitC-1 + ST7789 240×240

**These display pins are fixed by the product spec and are configured in-project
(`receiver_s3_st7789/include/pins.h`). You do not edit any library file.**

| Display signal | GPIO |
|----------------|------|
| TFT_SCLK       | 12   |
| TFT_MOSI       | 11   |
| TFT_DC         | 9    |
| TFT_CS         | 10   |
| TFT_RST        | 14   |
| TFT_BL (backlight) | 21 |

ST7789 240×240 panels have **no MISO** — LovyanGFX is told `-1`.

| Other          | GPIO | Notes                               |
|----------------|------|-------------------------------------|
| BOOT button    | 0    | active-LOW, on-board pull-up        |
| RGB status LED | 48   | WS2812 (present on most DevKitC-1)  |

Wire the ST7789:

```
ST7789        ESP32-S3
------        --------
GND  ───────── GND
VCC  ───────── 3V3
SCL  ───────── GPIO12  (SCLK)
SDA  ───────── GPIO11  (MOSI)
RES  ───────── GPIO14  (RST)
DC   ───────── GPIO9
CS   ───────── GPIO10
BLK  ───────── GPIO21  (backlight)
```

---

## 3. Optional MPU6050 (NOT required, OFF by default)

See README “How to add MPU6050”. **Recommendation: put the MPU6050 on the
ESP32-S3 receiver, not the camera** — the AI-Thinker has almost no free I²C
pins (the proposed SDA=14/SCL=15 collide with the SD CLK line). The firmware
ships with `ENABLE_MPU6050 = 0` and shows a DEMO/disabled attitude gauge.
