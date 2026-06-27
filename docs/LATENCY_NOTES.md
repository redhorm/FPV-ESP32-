# LATENCY NOTES — RC_FPV_SCALER_PRO

Honest engineering notes on what latency to expect from an ESP32-CAM FPV link
and the specific choices this firmware makes to minimise it.

## Where latency comes from

```
sensor exposure ─► JPEG encode ─► camera Wi-Fi TX ─► air ─► receiver Wi-Fi RX
   ─► TCP buffering ─► JPEG decode (S3) ─► SPI/DMA blit to ST7789 ─► photons
```

Typical contributors at 240×240, good signal:

| Stage                      | Rough budget |
|----------------------------|--------------|
| Sensor + JPEG encode       | 30–60 ms     |
| Wi-Fi TX/RX + air          | 20–80 ms (jitter dominated) |
| Receiver decode (TJpg)     | 8–20 ms      |
| SPI blit @ 40 MHz, 240²    | ~25 ms       |
| **End-to-end (good link)** | **~120–250 ms** |

The < 250 ms target is realistic on a clean channel and degrades gracefully as
RSSI drops. The ESP32-CAM **cannot** reach sub-50 ms analog-FPV latency — that
is a hardware reality, not a firmware bug.

## What this firmware does about it

1. **Raw TCP framing, not MJPEG/HTTP.** A 28-byte binary header + JPEG, no
   multipart boundaries, no HTTP keep-alive parsing. (`stream_server.cpp`,
   `video_client.cpp`)
2. **Always render the freshest frame.** If the receiver socket already has a
   newer frame queued when it finishes one, the older frame is **dropped**, not
   shown. This bounds latency instead of letting a backlog accumulate.
   (`video_client.cpp` drain loop)
3. **`CAMERA_GRAB_LATEST` + `fb_count = 2` in PSRAM.** The sensor keeps filling
   the spare buffer while we transmit, and we always dequeue the newest frame.
   With 1 buffer the sensor stalls until release (higher latency); with 2 +
   “latest” we get continuous capture without buffering stale frames.
4. **Wi-Fi power-save disabled** on both ends (`esp_wifi_set_ps(WIFI_PS_NONE)` /
   `WiFi.setSleep(false)`), removing tens of ms of modem-sleep jitter.
5. **Fixed AP channel** (`WIFI_AP_CHANNEL`) avoids scan/roam delays.
6. **`setNoDelay(true)`** (TCP_NODELAY) on both sockets so small frames are sent
   immediately instead of waiting on Nagle’s algorithm.
7. **Native 240×240 capture** maps 1:1 to the panel — no receiver-side scaling.
8. **Overlay never blocks video.** UI is rendered into sprites and DMA-pushed;
   numeric fields refresh at 5 Hz, not per frame.
9. **SD recording self-throttles** (frame-skip + adaptive skip) so it can never
   drag the live view down.

## Tuning for lower latency (at the cost of quality)

In `camera_esp32cam/include/config.h`:
- Raise `CAM_JPEG_QUALITY` toward 16–20 → smaller frames → less air time.
- Keep `CAM_FRAMESIZE = FRAMESIZE_240X240`.
- Keep `CAM_FB_COUNT = 2`.

In `receiver_s3_st7789/include/config.h`:
- Try `DISPLAY_SPI_HZ = 80000000` (80 MHz) if your wiring is short/clean — it
  roughly halves blit time.
- Lower `VIDEO_STALL_TMO` if you prefer faster reconnects over riding out brief
  dropouts.

## Measuring it

The overlay shows an **estimated** latency (top bar). Because the two ESP32s
don’t share a clock, it is computed as `frame_age − min(frame_age)` over the
session — i.e. **relative** latency/jitter, not an absolute calibrated number.
For absolute numbers, film the subject + screen together at high frame rate and
count frames.
