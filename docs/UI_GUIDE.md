# UI GUIDE — RC_FPV_SCALER_PRO (receiver)

Everything is driven by the single **BOOT** button (GPIO0). The UI is a small
state machine; the live view and the menu never fight over the screen.

## Button language

| Gesture        | Threshold            | In LIVE view              | In MENU                  |
|----------------|----------------------|---------------------------|--------------------------|
| **Short tap**  | `BTN_DEBOUNCE_MS`…`BTN_LONG_MS` | Open Quick Menu | Next item                |
| **Long press** | ≥ `BTN_LONG_MS` (0.5 s) | REC start/stop (toggle) | Select / activate item   |
| **Very long**  | ≥ `BTN_VLONG_MS` (1.5 s) | Camera LED toggle      | Back / close menu        |

`VERY_LONG` fires *while held* (immediate feedback); the following release is
ignored. `SHORT`/`LONG` are decided on release. See `button_handler.cpp`.

## Screens

### 1. Boot Screen
Neon-HUD wireframe skull (drawn with vector lines, white core + cyan/magenta
glow), `@luca3d_designs`, `FPV SYSTEM`, animated `BOOTING…`, firmware version
and a quick hardware line. ~3 s, then the receiver starts connecting.

### 2. Live View
Full-screen video with the FPV overlay on top. Overlay contents:

- **Top bar:** Wi-Fi signal bars + RSSI · FPS + estimated latency · quality
  (HQ/MQ/LQ) · blinking REC dot.
- **Bottom bar:** SD status / REC · dropped-frame counter · camera/LED state +
  lens glyph.
- **Off-road gauge** (Offroad overlay mode only): tilting horizon driven by
  roll/pitch with a fixed aircraft reference and numeric roll. Shows `DEMO`
  when no MPU6050 is present.

The overlay is **anti-flicker**: it is rendered into off-screen sprites,
numeric fields refresh at `OVERLAY_REFRESH_HZ` (5 Hz), and only complete
sprites are DMA-pushed over the video — you never see a half-drawn HUD.

### 3. Quick Menu
`Start/Stop REC · Camera LED · Quality (Low/Med/High) · Overlay
(Minimal/Full/Offroad) · Reconnect Stream · System Info · Close`. The current
value is shown on the right of each relevant row; the selected row is framed in
cyan.

### 4. System Info
Live: camera IP, RSSI, FPS (rx/cam), dropped frames, SD status, recording
status, camera uptime, camera free heap and PSRAM. Any press returns to the
Quick Menu.

### 5. Error Screen
Shown automatically in `ERROR_RECONNECTING`. Distinct messages for Wi-Fi lost,
stream lost, camera not found, JPEG decode error and SD missing, with a calm
animated indicator while the firmware auto-recovers.

## RGB status LED (GPIO48)

| Colour            | Meaning                         |
|-------------------|---------------------------------|
| Dim white         | boot / idle                     |
| Blue              | connecting (Wi-Fi/stream)       |
| Cyan              | in menu                         |
| Green             | stream live                     |
| Red               | error / reconnecting            |
| Pulsing magenta   | recording                       |

Disable with `ENABLE_RGB_STATUS_LED 0` if your board lacks the LED.

## Overlay modes

| Mode     | Shows                                            |
|----------|--------------------------------------------------|
| Minimal  | top bar only (signal/FPS/latency/REC)            |
| Full     | top + bottom bars                                |
| Offroad  | top + bottom bars + attitude/off-road gauge      |

Default is `OVERLAY_MODE_DEFAULT` (Full). Changing it from the menu also sends
`SET_OVERLAY_MODE` to the camera for telemetry consistency.
