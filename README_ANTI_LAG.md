# RC_FPV_SCALER_PRO — Anti-Lag profile (protocol v2)

This variant targets the **freshest possible displayed frame** on an ESP32-CAM
(AI-Thinker) + ESP32-S3 receiver with an ST7789 panel. Everything trades image
quality and secondary features for latency.

---

## 1. Profiles — one switch picks everything

Set `FPV_MODE_TURBO` to the **same value** in both
`camera_esp32cam/include/config.h` and `receiver_s3_st7789/include/config.h`.

| | **TURBO** (`=1`, default) | **QUALITY** (`=0`) |
|---|---|---|
| JPEG quality | 20 (smaller frames, fewer chunks) | 12 |
| Display SPI | 60 MHz | 40 MHz |
| Boot splash | 300 ms | 3000 ms |
| Incomplete-frame timeout | 55 ms | 80 ms |
| Overlay | Minimal (top bar only) | Full |
| SD recording | **blocked** (zero contention) | allowed (async) |

Individual knobs (all in `config.h`): `JPEG_QUALITY_TURBO`,
`JPEG_QUALITY_QUALITY`, `FRAME_TIMEOUT_MS`, `UDP_PAYLOAD_SIZE`, `MAX_JPEG_SIZE`,
`OVERLAY_REFRESH_MS`, `ENABLE_SD_RECORDING`, `ENABLE_DEBUG_SERIAL`,
`ENABLE_RGB_LED`, `ENABLE_MPU6050`, `TSYNC_INTERVAL_MS`.

## 2. Display variants — two build envs, one codebase

| Panel | Build env | Notes |
|-------|-----------|-------|
| 240×240 | `receiver_s3` | `DISPLAY_PANEL_280=0` |
| 240×280 | `receiver_s3_280` | `DISPLAY_PANEL_280=1`, 240×320 GRAM, 20 px offset |

The live video is always **240×240 centred**; on the 240×280 panel the top/bottom
20 px bands hold the HUD (so the HUD does not cover the image).

## 3. Video protocol v2 (UDP chunked)

* `frame_id` is **32-bit** (no wrap ambiguity).
* Every chunk carries the frame's **capture timestamp** (`capture_ms`), so
  `frame_age` survives a lost telemetry datagram. Chunk header = **20 bytes**.
* UDP payload = **1200 B** (`UDP_PAYLOAD_SIZE`): safe MTU margin, finer loss.
* **`TSYNC`**: the receiver probes the camera's `millis()`; the lowest-RTT sample
  sets the clock offset → an honest `frame_age_ms` (capture → end of draw).
* **Freshest-frame-wins**: a newer `frame_id` immediately abandons an older
  incomplete frame; partials older than `FRAME_TIMEOUT_MS` are dropped; stale
  completed frames in the same drain are discarded. No retransmit, no queue —
  at most one frame in reassembly + one displayed.

Both firmwares must run protocol v2 (`FPV_PROTOCOL_VERSION = 2`). `protocol.h`
is byte-identical on both sides (compile-time `static_assert`s on the sizes).

Fallback: set `VIDEO_USE_UDP 0` (both sides) for the raw-TCP path on very clean
links / debugging.

## 4. Asynchronous SD recording (QUALITY only)

SD writes are off the video path entirely: the loop memcpy's the latest JPEG into
a single PSRAM slot and signals a **low-priority FreeRTOS task** (pinned to the
non-loop core) that does the actual write. If the writer is busy the recording
frame is **dropped** (`rec_dropped`), never queued. A mutex serialises SD_MMC
structural ops. In TURBO recording is compiled out of the hot path.

## 5. Honest metrics (PERF screen + overlay)

Open the Quick Menu (short tap in live view) → **Perf / Anti-Lag**:

- `FPS c/r/d` — camera captures / receiver complete frames / displayed frames,
  each counted in a **real 1 s window** (not a smoothed EMA).
- `Frame age` — capture → end-of-draw, via the TSYNC clock offset (shows
  `sync...` until the first TSYNC lands). Green <120 ms, amber <220, red beyond.
- `Decode+draw` — LovyanGFX decodes the JPEG and DMA-blits in **one** call, so
  this is the honest combined duration (not two invented numbers).
- `RX rate` (kbps), `JPEG size`, `Packet loss %`.
- `Drop o/t/s` — partials dropped because newer arrived / timed out / stale
  complete; plus `JPEG errors`, `RSSI`, receiver `Heap`/`PSRAM` free.

The live overlay top bar also shows drawn-FPS + frame age; the bottom bar shows
drops + loss %.

## 6. Build

```sh
cd camera_esp32cam
pio run -e esp32cam

cd ../receiver_s3_st7789
pio run -e receiver_s3          # 240x240 panel
pio run -e receiver_s3_280      # 240x280 panel
```

## 7. Flash (macOS)

```sh
# ESP32-CAM: GPIO0->GND, reset into bootloader, then (115200 = most reliable):
cd camera_esp32cam
pio run -e esp32cam -t upload --upload-port /dev/cu.usbserial-1420
pio device monitor --port /dev/cu.usbserial-1420 -b 115200

# ESP32-S3 (native USB):
cd ../receiver_s3_st7789
pio run -e receiver_s3 -t upload --upload-port /dev/cu.usbmodem14101
pio device monitor --port /dev/cu.usbmodem14101 -b 115200
```

## 8. Test procedure

1. **Near (≤1 m)**: expect the highest FPS and lowest frame age (TURBO often
   ~120–160 ms, loss ≈ 0 %). This is your baseline.
2. **5 m**: FPS should hold; watch `Packet loss` and `Drop o/t/s` start to tick.
   If loss climbs, raise `CAM_JPEG_QUALITY` (smaller frames) or set
   `UDP_CHUNK_GAP_US` 50–150 on the camera.
3. **10 m / through obstacles**: loss and drops rise, FPS falls — but frame age
   should **stay low** (we drop, never queue). If video stalls, the link is the
   limit, not the firmware.
4. **SD off** (TURBO, default): confirm streaming is unaffected — this is the
   reference for "no recording contention".
5. **SD on** (QUALITY): start REC (long-press in live, or the menu). Confirm FPS
   and frame age barely change vs SD off, and `rec_dropped` rises instead of FPS
   falling.
6. **TURBO vs QUALITY**: flash each profile and compare the PERF screen. TURBO
   should show lower frame age and higher FPS; QUALITY a sharper image.

## 9. What "lag is gone" looks like on screen

- **Frame age** green and roughly steady (not creeping upward over time —
  creeping = a queue building, which this design prevents).
- **FPS c/r/d** close together and stable; `d` (displayed) tracking `r`.
- **Packet loss** low and **Drop o/t/s** rising only with distance/interference.
- Moving the camera, the screen reacts almost immediately with no "rubber-band"
  catch-up after you stop.

## 10. Unavoidable ESP32-CAM + ST7789 limits

- Sensor exposure + OV2640 JPEG encode is ~30–60 ms before a byte ever leaves.
- 2.4 GHz Wi-Fi adds 20–80 ms of jitter; UDP removes retransmit/HOL stalls but
  not the air time.
- ST7789 over SPI: a full 240×240 blit is ~15–25 ms even via DMA.
- Net: realistic end-to-end ~**120–250 ms**. This firmware minimises every
  controllable stage; it cannot reach analog-FPV (<50 ms). That is hardware, not
  a bug.

---

> **Build/verification note:** this was last edited in a cloud container whose
> egress policy **blocks PlatformIO's package registry** (403), so `pio run`
> cannot finish there. The protocol v2 structs (`FrameHeader`=30,
> `ChunkHeader`=20) and the reassembly / freshest-frame / loss-timeout logic were
> **unit-tested natively** (byte-exact under simulated loss); the Arduino glue was
> reviewed against arduino-esp32 / LovyanGFX. Run the `pio run` commands above on
> your machine to produce the binaries.
