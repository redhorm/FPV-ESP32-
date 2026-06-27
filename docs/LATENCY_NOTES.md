# LATENCY NOTES — RC_FPV_SCALER_PRO

Honest engineering notes on what latency to expect from an ESP32-CAM FPV link
and the specific choices this firmware makes to minimise it.

## Where latency comes from

```
sensor exposure ─► JPEG encode ─► camera Wi-Fi TX ─► air ─► receiver Wi-Fi RX
   ─► UDP chunk reassembly ─► JPEG decode (S3) ─► SPI/DMA blit to ST7789 ─► photons
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

1. **UDP chunked video (default), not MJPEG/HTTP or TCP.** Each JPEG is split
   into ≤1400-byte chunks (14-byte `ChunkHeader`) and sent unicast; a 30-byte
   telemetry datagram precedes each frame. UDP has **no head-of-line blocking
   and no retransmits**, so a lost packet costs at most one frame instead of
   stalling the whole stream the way a dropped TCP segment does — this is the
   single biggest FPS/latency win and why ~15–18 fps is reachable.
   (`stream_server.cpp`, `video_client.cpp`; set `VIDEO_USE_UDP 0` for the old
   raw-TCP path.)
2. **Always render the freshest frame.** The receiver reassembles chunks into a
   frame and, the moment a **newer** `frame_id` appears, abandons any older
   partial frame (counted as a drop). A partial frame older than
   `FRAME_ASSEMBLY_TMO` is also dropped. This bounds latency instead of letting
   a backlog accumulate. (`video_client.cpp` reassembler)
3. **`CAMERA_GRAB_LATEST` + `fb_count = 2` in PSRAM.** The sensor keeps filling
   the spare buffer while we transmit, and we always dequeue the newest frame.
   With 1 buffer the sensor stalls until release (higher latency); with 2 +
   “latest” we get continuous capture without buffering stale frames.
4. **Wi-Fi power-save disabled** on both ends (`esp_wifi_set_ps(WIFI_PS_NONE)` /
   `WiFi.setSleep(false)`), removing tens of ms of modem-sleep jitter.
5. **Fixed AP channel** (`WIFI_AP_CHANNEL`) avoids scan/roam delays.
6. **No transport-level waiting.** UDP datagrams leave immediately (no Nagle, no
   ACK round-trips, no congestion-control ramp). In the TCP fallback we set
   `setNoDelay(true)` (TCP_NODELAY) so small frames are not delayed by Nagle.
7. **Native 240×240 capture** maps 1:1 to the panel — no receiver-side scaling.
8. **Overlay never blocks video.** UI is rendered into sprites and DMA-pushed;
   numeric fields refresh at 5 Hz, not per frame.
9. **SD recording self-throttles** (frame-skip + adaptive skip) so it can never
   drag the live view down.

## Tuning for lower latency (at the cost of quality)

In `camera_esp32cam/include/config.h`:
- Raise `CAM_JPEG_QUALITY` toward 16–20 → smaller frames → fewer chunks → less
  air time and fewer chances of a lost chunk killing the frame.
- Keep `CAM_FRAMESIZE = FRAMESIZE_240X240`.
- Keep `CAM_FB_COUNT = 2`.
- Keep `VIDEO_USE_UDP = 1` for the lowest-latency path. If a busy channel shows
  heavy per-frame loss, set `UDP_CHUNK_GAP_US` to 50–150 (lets the Wi-Fi TX
  buffer drain between chunks at a tiny latency cost).

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
