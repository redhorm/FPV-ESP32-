# RC_FPV_SCALER_PRO

A premium, low-latency **Wi-Fi FPV system for RC scalers / crawlers**, built as
two cooperating ESP32 firmwares:

- **`camera_esp32cam/`** — ESP32-CAM (AI-Thinker, OV2640): creates the Wi-Fi AP,
  streams JPEG over a dedicated low-latency TCP protocol, records to microSD,
  and accepts UDP commands.
- **`receiver_s3_st7789/`** — ESP32-S3 DevKitC-1 + ST7789 240×240: connects to
  the camera, decodes and draws frames, renders a flicker-free FPV overlay and
  a single-button graphical menu, drives an optional RGB status LED.

> Design priorities (in order): **low latency · robustness · clean UI · easy
> configuration · maintainable code** — over high video quality, heavy effects
> or fragile complexity.

---

## 1. Description

The camera is the access point. The receiver is a battery-powered handheld
monitor with a square 240×240 display and one button. The video path is a
purpose-built binary TCP stream (no heavy web server in the hot path), tuned so
the receiver always shows the **freshest** frame and discards stale ones. An
anti-flicker HUD overlay (signal, FPS, latency, REC, SD, drops, optional
off-road attitude gauge) is drawn *on the receiver*, never burned into the
video.

## 2. Logical diagram

```
        ┌──────────────────────────┐                 ┌───────────────────────────┐
        │      ESP32-CAM            │                 │      ESP32-S3 DevKitC-1   │
        │   (AI-Thinker, OV2640)    │                 │   + ST7789 240x240 (SPI)  │
        │                           │   Wi-Fi AP      │                           │
        │  OV2640 ─► JPEG encode    │  192.168.4.1    │  TCP :81  ─► JPEG decode  │
        │     │                     │ ───────────────►│      │      (LovyanGFX)   │
        │     ├─► TCP server :81  ──┼─ video frames ─►│      └─► ST7789 blit (DMA)│
        │     ├─► UDP server :82  ◄─┼─ commands ──────┤  UDP :82 ◄─ BOOT button   │
        │     ├─► HTTP :80 (debug)  │   status ───────┤  overlay + menu + RGB LED │
        │     └─► microSD (1-bit)   │                 │                           │
        │   [opt] MPU6050 attitude  │                 │   [opt] MPU6050 here too  │
        └──────────────────────────┘                 └───────────────────────────┘
```

## 3. Hardware

- ESP32-CAM AI-Thinker with OV2640 + microSD in the on-board slot.
- ESP32-S3 DevKitC-1 + ST7789 240×240 SPI display.
- Solid 5 V supply for the camera (≥1 A). USB for the S3.
- *Optional, not required:* MPU6050 (off-road attitude). Off by default.

No other hardware is mandatory. The system works **without** the microSD (no
recording) and **without** the MPU6050 (demo/disabled gauge).

## 4. Camera pins

Standard AI-Thinker map — see [`docs/WIRING.md`](docs/WIRING.md). LED on
**GPIO4** (off by default). microSD mounted in **1-bit** mode (frees GPIO4).

## 5. Receiver pins (fixed, configured in-project)

```
TFT_SCLK 12 · TFT_MOSI 11 · TFT_DC 9 · TFT_CS 10 · TFT_RST 14 · TFT_BL 21
BOOT button GPIO0 · RGB LED GPIO48
```
These are set in `receiver_s3_st7789/include/pins.h` and `src/jpeg_renderer.cpp`
— **no editing of any global library file is required.**

## 6. Build the camera

```bash
cd camera_esp32cam
pio run -e esp32cam
```

## 7. Build the receiver

```bash
cd receiver_s3_st7789
pio run -e receiver_s3
```

PlatformIO downloads the libraries listed in each `platformio.ini` on first
build.

## 8. Flash the ESP32-CAM

The AI-Thinker has no USB. Use a USB-TTL adapter, pull **GPIO0 → GND** to enter
bootloader, power-cycle, then:

```bash
cd camera_esp32cam
pio run -e esp32cam -t upload --upload-port /dev/cu.usbserial-XXXX
# remove GPIO0->GND link and reset to run
pio device monitor -b 115200
```

## 9. Flash the ESP32-S3

Native USB — just plug it in:

```bash
cd receiver_s3_st7789
pio run -e receiver_s3 -t upload --upload-port /dev/cu.usbmodemXXXX
pio device monitor -b 115200
```
If upload fails, hold **BOOT**, tap **RST**, release **BOOT** to force download
mode.

## 10. Test the camera (browser / debug endpoints)

Join Wi-Fi **`RC-FPV-CAM`** (password `12345678`), then:

```bash
curl http://192.168.4.1/status                  # one-line telemetry
curl http://192.168.4.1/snapshot -o snap.jpg    # single JPEG
echo -n "PING" | nc -u -w1 192.168.4.1 82       # -> PONG
```
The debug HTTP server is intentionally lightweight and **separate** from the
TCP video path, so probing it does not slow streaming.

## 11. Test the receiver

Power it on. You should see: neon-skull boot screen → `WI-FI LOST`/connecting →
`STREAM LOST`/connecting → **live video with overlay** once the camera is up.
Without a camera it cycles the error screens and keeps retrying — that itself is
a good robustness test.

## 12. Using the BOOT button

| Gesture     | Live view            | Menu                |
|-------------|----------------------|---------------------|
| Short tap   | Open Quick Menu      | Next item           |
| Long (0.5s) | REC start/stop       | Select / activate   |
| Very long (1.5s) | Camera LED toggle | Back / close menu |

Full UI details in [`docs/UI_GUIDE.md`](docs/UI_GUIDE.md).

## 13. microSD recording

- Optional. Start/stop from the receiver (long-press in live view, or the Quick
  Menu). Commands travel by UDP (`REC_TOGGLE/START/STOP`).
- Frames are saved as a JPG sequence:
  `/FPV/REC_0001/frame_000001.jpg`, `frame_000002.jpg`, …
- **Recording never blocks streaming:** it writes at most every
  `SD_REC_FRAME_SKIP`-th frame and *adaptively increases* the skip if the card
  is slow (`SD_WRITE_SLOW_MS`).
- If the card is absent, the overlay shows **NO SD** and streaming continues.
- We deliberately do **not** mux video on the MCU. Turn the sequence into a
  video on a PC:
  ```bash
  ffmpeg -framerate 12 -i frame_%06d.jpg -c:v libx264 -pix_fmt yuv420p out.mp4
  ```

## 14. Enable / disable the GPIO4 camera LED

In `camera_esp32cam/include/config.h`:
```c
#define ENABLE_CAMERA_LED   true    // allow LED_TOGGLE to act at all
#define CAMERA_LED_ON_BOOT  false   // keep false — never auto-on
```
The LED is OFF at boot and only switches on a manual `LED_TOGGLE`. To remove it
entirely, set `ENABLE_CAMERA_LED false`.

## 15. Known issues — GPIO4 + microSD

GPIO4 is **both** the flash LED **and** SD_DATA1. In 4-bit SD mode they
conflict; we therefore mount the card in **1-bit** mode (`SD_ONE_BIT_MODE`),
which frees GPIO4. Even so, driving the LED can inject noise near the SD bus —
so the firmware never auto-enables it. **If your card acts up, leave the LED
off.**

## 16. How to add an MPU6050

Default is `ENABLE_MPU6050 = 0` (compiles, gauge shows DEMO/disabled).

To enable on the **camera** (proposal, verify conflicts):
```c
// camera_esp32cam/include/config.h
#define ENABLE_MPU6050 1
#define MPU_I2C_SDA 14   // PROPOSAL
#define MPU_I2C_SCL 15   // PROPOSAL — collides with SD CLK in 4-bit mode!
```
⚠ The AI-Thinker has almost no free pins; SDA=14/SCL=15 overlap SD lines.
**Recommended: mount the MPU6050 on the ESP32-S3 receiver** (plenty of free
I²C pins) or rely on software-only overlay. When enabled, roll/pitch are
complementary-filtered and piggybacked on every video frame to drive the
off-road gauge.

## 17. Real latency limits of ESP32-CAM

Expect **~120–250 ms** end-to-end at 240×240 on a good link — see
[`docs/LATENCY_NOTES.md`](docs/LATENCY_NOTES.md). The ESP32-CAM physically
cannot match analog-FPV latency; this firmware minimises every controllable
stage but cannot beat the sensor/encode/Wi-Fi floor.

## 18. Recommended low-latency parameters

```c
// camera config.h
CAM_FRAMESIZE   = FRAMESIZE_240X240   // native square, no receiver scaling
CAM_JPEG_QUALITY= 12                  // raise toward 16–20 for less air time
CAM_FB_COUNT    = 2                   // + CAMERA_GRAB_LATEST
WIFI_AP_CHANNEL = 6 (fixed)           // no roaming/scan delay
// power save disabled on both ends, TCP_NODELAY on both sockets
```

## 19. Troubleshooting

See [`docs/TROUBLESHOOTING.md`](docs/TROUBLESHOOTING.md) for a full table
(power brown-outs, panel init, reconnection, SD, build errors, CLI checks).

## 20. Roadmap

- Optional dual-core SD writer task (queue + copy) for full-rate recording.
- On-receiver MPU6050 path + horizon calibration UI.
- Adaptive quality: auto-tune JPEG quality from measured RSSI/drops.
- Optional AES on the UDP command channel.
- Snapshot-to-SD button and on-screen REC timer.
- Persist user settings (overlay mode, quality) in NVS.

---

## Why these libraries

- **`esp_camera`, `WiFi`, `WebServer`, `SD_MMC`, `WiFiUDP`** — all bundled with
  arduino-esp32; well maintained, zero exotic dependencies on the camera side.
- **LovyanGFX** (receiver display) — chosen over TFT_eSPI + TJpg_Decoder
  because (a) the panel pins are configured **in code**, so no global
  `User_Setup.h` editing and the build is reproducible; (b) it has a fast
  built-in JPEG decoder writing through a **DMA** SPI bus — exactly our
  full-screen-every-frame hot path; (c) first-class **sprites/DMA** enable the
  flicker-free overlay. It is actively maintained and widely used.
- **Adafruit MPU6050 + Unified Sensor** — only compiled in when
  `ENABLE_MPU6050 = 1`; mature, well-documented I²C drivers.

## Protocol summary

Binary video header (28 bytes, little-endian, `protocol.h`, identical on both
firmwares) + JPEG payload, over TCP :81:

```
magic[4]='RFPV' · version · flags · header_size · frame_id · timestamp_ms ·
jpeg_length · fps_x2 · jpeg_quality · roll_ddeg · pitch_ddeg · free_heap_kb ·
error_flags · reserved
```
UDP :82 commands (ASCII, `\n`): `REC_TOGGLE/START/STOP`, `LED_TOGGLE`, `PING`,
`REQUEST_STATUS`, `SET_QUALITY=n`, `SET_OVERLAY_MODE=n`. Camera replies with a
single `STATUS key=value …` line (or `PONG`).

---

## SENIOR REVIEW NOTES

**Technical choices**
- *Raw TCP video, control on UDP.* Separating bulk video (TCP, reliable,
  ordered) from latency-sensitive control (UDP, fire-and-forget) means a lost
  command never stalls video and vice-versa.
- *Telemetry piggybacked on every frame header.* The overlay always has fresh
  data with **zero** extra round-trips; the UDP `STATUS` poll is only for the
  richer System Info screen.
- *“Freshest frame wins.”* The receiver drains the socket and renders only the
  newest complete frame, trading dropped frames for bounded latency — the right
  trade for FPV.
- *Single shared `AppStatus`/`RxStatus` struct.* Predictable, lock-free
  (single-loop) state instead of scattered globals.
- *In-code LovyanGFX panel config.* Reproducible builds; no library edits.
- *1-bit SD mount.* Frees GPIO4 and is robust on cheap clones.

**Compromises / limits**
- End-to-end latency is bounded by sensor+encode+Wi-Fi (~120–250 ms); not
  analog-FPV territory. Documented, not hidden.
- The displayed latency is a **relative** estimate (no shared clock).
- SD recording is rate-limited (frame-skip), so it is *sampled* video, not
  full-rate — a deliberate choice to protect streaming (full-rate dual-core
  writer is on the roadmap).
- `stream_send_frame` may block briefly on a stalled socket; bounded in
  practice by TCP_NODELAY + small frames, and a broken link is detected on the
  next write and triggers a clean reconnect.
- MPU6050 on the AI-Thinker is genuinely pin-constrained; the README recommends
  the receiver instead rather than pretending the camera pins are free.

**Robustness handled**
- No `delay()` in any hot path; everything is `millis()`-based with explicit
  timeouts (Wi-Fi connect, video connect, stream stall, SD slow-write).
- SD absent / fails → degrades to streaming-only, never crashes.
- Wi-Fi drop → auto-reconnect with backoff on both ends; receiver shows the
  error screen and recovers.
- Corrupt/oversize frame or bad magic → the receiver drops the link and resyncs
  from a clean boundary (bounded reads guard against half-frames wedging the
  loop).
- JPEG decode failure → counted as a drop, transient error shown, link kept
  alive.
- Low-heap flag raised in telemetry; sprite allocation failure degrades the
  overlay gracefully instead of crashing.
- Camera LED forced OFF at boot regardless of config (flicker/SD safety).

**Future improvements** — see Roadmap above.

> ⚠ **Build/verification note:** the code targets PlatformIO + arduino-esp32
> and was written against those APIs, but it has **not** been compiled or
> hardware-tested in this environment (no toolchain/hardware available here).
> Run `pio run` in each subproject on your machine; report any board-specific
> warnings and they’ll be quick to resolve.
