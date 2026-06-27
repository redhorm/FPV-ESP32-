# Arduino IDE sketches

Ready-to-open Arduino IDE versions of the firmware (generated from the canonical
PlatformIO projects by `tools/make_arduino_sketches.sh`). Each folder is a
self-contained sketch — all `.h`/`.cpp` are flattened next to the `.ino`, which
Arduino compiles automatically. `setup()`/`loop()` live in `main.cpp`.

| Sketch | Board | For |
|--------|-------|-----|
| `RC_FPV_Camera/` | AI Thinker ESP32-CAM | the camera (same for both panels) |
| `RC_FPV_Receiver_240x240/` | ESP32S3 Dev Module | 240×240 ST7789 receiver |
| `RC_FPV_Receiver_240x280/` | ESP32S3 Dev Module | 240×280 ST7789 receiver |

Flash **one camera** + **one receiver** (the one matching your panel).

## 0. One-time setup
- Arduino IDE 2.x → **Boards Manager**: install **esp32 by Espressif Systems**.
- **Library Manager**: install **LovyanGFX** by lovyan03 (receiver only).
- Optional (camera, only if `ENABLE_MPU6050 1`): **Adafruit MPU6050** +
  **Adafruit Unified Sensor**.

## 1. Camera — `RC_FPV_Camera`
Tools menu:
- Board: **AI Thinker ESP32-CAM**
- PSRAM: **Enabled**
- Partition Scheme: **Huge APP (3MB No OTA/1MB SPIFFS)**
- Core Debug Level: **None**
- Upload Speed: **115200**

Flashing: pull **GPIO0 → GND**, reset into the bootloader, Upload, then remove
the GPIO0–GND link and reset.

## 2. Receiver — `RC_FPV_Receiver_240x240` or `_240x280`
Tools menu:
- Board: **ESP32S3 Dev Module**
- USB CDC On Boot: **Enabled**
- USB Mode: **Hardware CDC and JTAG**
- PSRAM: **OPI PSRAM**
- Flash Size: **16MB** (or your board's)
- Partition Scheme: **Default 4MB with spiffs** (or larger)
- Core Debug Level: **None**

The display variant is already pinned in each folder's `config.h`
(`DISPLAY_PANEL_280` = 0 for 240×240, 1 for 240×280). The boot artwork is
embedded in `boot_image.h`.

## 3. Keep the two ends matching
`FPV_MODE_TURBO`, `VIDEO_USE_UDP`, `UDP_PAYLOAD_SIZE` and `MAX_JPEG_SIZE` **must
be identical** in the camera's `config.h` and the receiver's `config.h`.

## Regenerating
These folders are generated. Edit the canonical sources under
`camera_esp32cam/` and `receiver_s3_st7789/`, then:

```sh
bash tools/make_arduino_sketches.sh
```
