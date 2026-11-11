# ESP32-S3-CAM dual-USB-C transmitter — experimental hardware profile

This guide adds a **second camera firmware build** while preserving the existing
ESP32-CAM (AI-Thinker) build and the existing ESP32-S3 + ST7789 receiver.

> **Hardware warning:** ESP32-S3-CAM is not one standardized PCB. The mapping
> below matches documented dual-USB-C N16R8 boards and *appears* relevant to
> the photographed board, but its actual chip marking, camera sensor and PCB
> revision were not confirmed. Verify these before relying on the firmware.
> The firmware was **not** flashed or bench-tested on the physical device.

## 1. New build target

`camera_esp32cam/platformio.ini` includes:

- `esp32cam`: **original** ESP32-CAM AI-Thinker (unchanged).
- `esp32s3cam_n16r8`: ESP32-S3-WROOM-1 **16 MB QIO flash + 8 MB OPI PSRAM**
  with dual USB-C (native USB / USB-to-UART).
- Both builds share the **same UDP protocol v2** implementation and AP config.
- Receiver build: `receiver_s3_st7789/receiver_s3` or `receiver_s3_280`,
  **unchanged**. Do not flash camera firmware onto the receiver.

The N16R8 build *requires* PSRAM. It reports a clear camera error at startup
rather than silently falling back to a different, potentially incompatible
resolution if PSRAM is missing. If your module is N8R8/N8R2/N16R0, first
derive a matching memory profile; **do not blindly select N16R8**.

## 2. Proposed camera pinout (S3 only)

| Signal | GPIO | Signal | GPIO |
| --- | ---: | --- | ---: |
| Camera XCLK | 15 | Camera PCLK | 13 |
| Camera SIOD (SDA) | 4 | Camera SIOC (SCL) | 5 |
| Camera VSYNC | 6 | Camera HREF | 7 |
| Camera Y2 / D0 | 11 | Camera Y3 / D1 | 9 |
| Camera Y4 / D2 | 8 | Camera Y5 / D3 | 10 |
| Camera Y6 / D4 | 12 | Camera Y7 / D5 | 18 |
| Camera Y8 / D6 | 17 | Camera Y9 / D7 | 16 |
| Camera PWDN | -1 (not wired) | Camera RESET | -1 (not wired) |
| microSD CLK | 39 | microSD CMD | 38 |
| microSD D0 | 40 | Native USB D-/D+ | 19 / 20 |
| USB-UART TX/RX | 43 / 44 | BOOT | 0 |

**GPIO4 on S3 is camera SCCB SDA; it is *not* the AI-Thinker flash LED.**
The S3 build disables camera LED control instead of driving GPIO4.
The optional MPU6050 configuration on GPIO14/15 is disabled: GPIO15 is XCLK;
do not enable until its pins are remapped to two actually free S3 GPIOs.

**SD_MMC:** `SD_MMC.setPins(39, 38, 40)` is called before `begin` (1-bit)
in QUALITY mode. Using the ESP32-S3 default SDMMC pins could collide with
internal PSRAM. TURBO skips SD mounting entirely and cannot record.

Sources for this candidate PCB map (independent projects; not a vendor schematic):

- https://github.com/larryli/esp-idf_esp32_s3_cam/blob/master/docs/hardware.md
- https://github.com/jaysys/esp32s3-with-camera-ov2640-and-sdcard/blob/main/pinmap.md
- https://github.com/espressif/arduino-esp32/blob/master/libraries/SD_MMC/README.md

## 3. Build and upload (Mac)

From the repository root after installing PlatformIO:

```sh
python3 -m pip install --user platformio
python3 -m platformio run -d camera_esp32cam -e esp32s3cam_n16r8
python3 -m platformio run -d receiver_s3_st7789 -e receiver_s3
```

Connect **one USB-C port at a time**. Native OTG uses the ESP32-S3 USB
controller; the other is USB-to-UART. Select the port that actually enumerates
and provides upload and serial console on your Mac:

```sh
ls /dev/cu.usb*
python3 -m platformio run -d camera_esp32cam -e esp32s3cam_n16r8 \
  -t upload --upload-port /dev/cu.usbmodemXXXX
python3 -m platformio device monitor -b 115200 --port /dev/cu.usbmodemXXXX
```

If the native port does not enumerate or does not show logs, try the UART
bridge port (`/dev/cu.usbserialXXXX`) instead, checking the selected
USB_CDC build flags. For manual ROM download: hold BOOT, tap RST, release BOOT,
then retry upload. Do not connect 5 V to any 3.3 V GPIO.

## 4. Bring-up and acceptance test

1. Confirm module's metal can identifies its flash/PSRAM variant; confirm
   camera flex cable/sensor and the dual-USB-C board mapping.
2. Upload the **S3 camera** target. Watch serial for `Camera init OK`
   and `S3 camera detected: sensor PID=...` plus reported PSRAM/flash.
   If camera initialization fails, STOP and verify pinout/PSRAM; do not assume
   the OV2640/OV3660 is correct without its sensor ID.
3. Connect to AP `RC-FPV-CAM` (change the default demo password before public
   use); request `http://192.168.4.1/snapshot` for one JPEG and
   `http://192.168.4.1/status` for diagnostics.
4. Turn on the **existing** ESP32-S3 ST7789 receiver with protocol v2.
   It must receive JPEG frames and show its usual overlay, TSYNC, RSSI,
   dropped frames and age. No receiver reflashing should be necessary.
5. Test 1 m / 5 m / 10 m under comparable lighting. Record displayed FPS,
   frame age, packet loss, JPEG sizes and video freezes for original ESP32-CAM
   vs S3. Repeat for TURBO and QUALITY. Expect variation by sensor and Wi-Fi.
6. For microSD recording, switch **both firmwares'** `FPV_MODE_TURBO` flag
   to 0 (QUALITY), rebuild both, insert a card, mount via S3 SD pins, and
   verify JPEG sequence recording. TURBO intentionally reports SD unavailable.

The code's displayed frame-age measurement is based on a frame timestamp
taken *after* the camera driver returns a JPEG. It therefore does **not**
include all sensor exposure/encoding time. For true glass-to-glass latency,
film the live scene and the receiver screen together at high frame rate.

## 5. Performance constraints / tuning

- Keep `FRAMESIZE_240X240`, double buffering in PSRAM and
  `CAMERA_GRAB_LATEST`; the existing receiver renders native 240x240.
- TURBO defaults: JPEG quality **20** (smaller payload), no SD recording,
  UDP chunks 1200 B, Wi-Fi power save disabled, channel 6.
- S3 camera avoids grabbing when neither a UDP subscriber nor recording is
  present. The first frame after subscription may take one capture cycle.
- If radio losses increase, tune `UDP_CHUNK_GAP_US` to 50–150 or raise
  JPEG quality value (more compression), then compare displayed FPS and loss.
- Protocol v2 headers remain **30 B FrameHeader / 20 B ChunkHeader**;
  receiver source is unchanged. This is not an H.264 stream.
- **No guaranteed latency/FPS improvement:** both the sensor/encode pipeline
  and RF conditions matter. Existing README figures are project estimates,
  not bench measurements of the new board.

## 6. Static/build checks

```sh
python3 tools/check_s3_compat.py
python3 -m platformio run -d camera_esp32cam -e esp32cam
python3 -m platformio run -d camera_esp32cam -e esp32s3cam_n16r8
python3 -m platformio run -d receiver_s3_st7789 -e receiver_s3
python3 -m platformio run -d receiver_s3_st7789 -e receiver_s3_280
```

The PR CI attempts these commands on GitHub-hosted runners. Passing builds
alone cannot guarantee the GPIO map, camera sensor, PSRAM or RF latency.
