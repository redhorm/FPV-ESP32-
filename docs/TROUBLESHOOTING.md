# TROUBLESHOOTING — RC_FPV_SCALER_PRO

Open a serial monitor at **115200** on each board first — the firmware logs are
deliberately readable (`[E]/[I]/[V]` prefixes). Set `DEBUG_LEVEL` in the
respective `config.h` (0=silent…3=verbose).

## Camera

| Symptom | Likely cause / fix |
|---------|--------------------|
| `esp_camera_init failed: 0x...` | Brown-out — feed 5 V/≥1 A. Re-seat the module. A `0x105` (ESP_ERR_NOT_FOUND) usually means bad power or a dead OV2640. |
| Boots, reboots when streaming | Insufficient power during Wi-Fi TX. Use a better supply / shorter USB cable. |
| `No PSRAM found` in log | You flashed a non-PSRAM board. The firmware falls back to QVGA single-buffer (higher latency) but still runs. |
| microSD never mounts | Card not FAT32, bad card, or 4-bit contention. Keep `SD_ONE_BIT_MODE = true`. Try another card (≤32 GB FAT32). |
| LED flickers / SD flaky | Leave GPIO4 LED off. Set `ENABLE_CAMERA_LED = false`. |
| AP not visible | Channel congestion — change `WIFI_AP_CHANNEL`. SSID is hidden? `WIFI_AP_HIDDEN` must be 0. |

## Receiver

| Symptom | Likely cause / fix |
|---------|--------------------|
| White/garbage screen | Wrong panel init. Confirm wiring matches `pins.h`. ST7789 needs colour inversion — already set (`invert = true`). |
| Screen flashes random pixels at power-on | Should not happen: we paint black *before* enabling backlight. If it does, check TFT_BL on GPIO21. |
| Backlight on but black | Lower `DISPLAY_SPI_HZ` to 27 MHz (long/dirty wiring). Check MOSI/SCLK. |
| Stuck on “WI-FI LOST” | SSID/password mismatch with the camera AP (`WIFI_SSID`/`WIFI_PASSWORD`). Camera not powered. |
| “STREAM LOST” loop | Camera AP is up but no video arrives. Confirm `CAMERA_IP` (192.168.4.1) and that `VIDEO_USE_UDP`, `VIDEO_UDP_PORT` (81) **and `UDP_CHUNK_PAYLOAD`** match on both firmwares. The receiver auto-sends `VSUB` every `VIDEO_SUB_INTERVAL_MS`. |
| Choppy / high latency | Weak signal (watch the signal bars). Increase `CAM_JPEG_QUALITY` value, keep antennas clear, reduce distance. |
| Many dropped frames | Under poor RSSI a lost UDP chunk drops the whole frame, so FPS falls but latency stays low. Raise `CAM_JPEG_QUALITY` (fewer chunks), set `UDP_CHUNK_GAP_US` to 50–150 on a busy channel, or use the TCP fallback (`VIDEO_USE_UDP 0`) on a clean link. |
| `VIDEO_USE_UDP` mismatch | This flag **must be identical** on camera and receiver. Rebuild/flash both after changing it. |
| Overlay missing, log says “sprite alloc failed” | Very low RAM / no PSRAM. Overlay self-disables to avoid a crash; video still works. Enable PSRAM (see `platformio.ini`). |
| RGB LED does nothing / errors | Your board has no WS2812 on GPIO48. Set `ENABLE_RGB_STATUS_LED 0`. |
| Buttons unresponsive | GPIO0 is also the strapping pin; ensure nothing else drives it. Adjust `BTN_*` thresholds if needed. |

## Build

| Symptom | Fix |
|---------|-----|
| `pin_sscb_sda` / `pin_sccb_sda` error | Handled via `ESP_ARDUINO_VERSION` guard in `camera_service.cpp`; make sure you’re on a normal espressif32 platform. |
| `rgbLedWrite` undefined | Your arduino-esp32 core is < 2.0.7. Update the platform, or set `ENABLE_RGB_STATUS_LED 0`. |
| LovyanGFX not found | `pio` resolves `lib_deps` automatically on first build; ensure you ran inside `receiver_s3_st7789/`. |
| Camera app too big | Keep `board_build.partitions = huge_app.csv` (already set). |

## Quick command-line checks (camera AP)

With your PC joined to `RC-FPV-CAM`:

```
curl http://192.168.4.1/status      # one-line telemetry
curl http://192.168.4.1/snapshot -o snap.jpg   # single JPEG

# UDP command (Linux/macOS):
echo -n "PING" | nc -u -w1 192.168.4.1 82      # expect PONG
echo -n "REC_TOGGLE" | nc -u -w1 192.168.4.1 82
```
