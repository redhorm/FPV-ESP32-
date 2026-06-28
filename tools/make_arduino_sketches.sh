#!/usr/bin/env bash
# Generate Arduino IDE sketches from the canonical PlatformIO sources.
#   bash tools/make_arduino_sketches.sh
# Arduino IDE compiles every .ino/.cpp/.h in a sketch folder, so we FLATTEN the
# include/ + src/ trees into one folder per sketch. main.cpp keeps setup()/loop()
# (valid in a .cpp); the .ino is a banner so the IDE recognises the sketch.
set -euo pipefail
cd "$(dirname "$0")/.."
ROOT="arduino"
rm -rf "$ROOT"
mkdir -p "$ROOT"

copy_flat() { # <srcproj> <destdir>
  mkdir -p "$2"
  cp "$1"/include/*.h "$2"/
  cp "$1"/src/*.cpp   "$2"/
}

# ---- Camera -----------------------------------------------------------------
CAM="$ROOT/RC_FPV_Camera"
copy_flat camera_esp32cam "$CAM"
cat > "$CAM/RC_FPV_Camera.ino" <<'INO'
// =============================================================================
//  RC_FPV_Camera.ino  -  ESP32-CAM (AI-Thinker) anti-lag FPV camera
// -----------------------------------------------------------------------------
//  setup()/loop() live in main.cpp (compiled alongside this sketch). This .ino
//  only marks the sketch folder for the Arduino IDE.
//
//  ARDUINO IDE SETTINGS (Tools menu):
//    Board:            "AI Thinker ESP32-CAM"   (esp32 core by Espressif)
//    PSRAM:            Enabled
//    Partition Scheme: Huge APP (3MB No OTA/1MB SPIFFS)
//    Core Debug Level: None
//    Upload Speed:     115200   (most reliable via USB-TTL; GPIO0->GND to flash)
//  Libraries: none required for the camera (esp_camera/WiFi/SD_MMC ship with the
//  ESP32 core). "Adafruit MPU6050" + "Adafruit Unified Sensor" only if you set
//  ENABLE_MPU6050 1 in config.h.
//  Edit anti-lag options (TURBO/QUALITY etc.) in config.h.
// =============================================================================
INO
cat > "$CAM/sketch.yaml" <<'YAML'
profiles:
  default:
    fqbn: esp32:esp32:esp32cam:PartitionScheme=huge_app,DebugLevel=none,UploadSpeed=115200
    platforms:
      - platform: esp32:esp32
YAML

# ---- Receiver (two display variants) ----------------------------------------
make_receiver() { # <foldername> <panel280: 0|1>
  local DST="$ROOT/$1"
  copy_flat receiver_s3_st7789 "$DST"
  # Pin the display variant directly in this sketch's config.h (no -D in Arduino).
  sed -i "s/\(#define DISPLAY_PANEL_280  *\)[01]/\1$2/" "$DST/config.h"
  cat > "$DST/$1.ino" <<INO
// =============================================================================
//  $1.ino  -  ESP32-S3 + ST7789 anti-lag FPV receiver
// -----------------------------------------------------------------------------
//  Display variant: DISPLAY_PANEL_280 = $2  ($([ "$2" = 1 ] && echo "240x280" || echo "240x240"))
//  setup()/loop() live in main.cpp (compiled alongside this sketch).
//
//  ARDUINO IDE SETTINGS (Tools menu):
//    Board:             "ESP32S3 Dev Module"  (esp32 core by Espressif)
//    USB CDC On Boot:   Enabled
//    USB Mode:          Hardware CDC and JTAG
//    PSRAM:             OPI PSRAM
//    Flash Size:        16MB (or your board's size)
//    Partition Scheme:  Default 4MB with spiffs (or larger)
//    Core Debug Level:  None
//  Libraries (Library Manager): "LovyanGFX" by lovyan03.
//  The boot artwork is embedded in boot_image.h. Anti-lag options in config.h.
//  NOTE: keep DISPLAY_PANEL_280 and FPV_MODE_TURBO matching the camera profile.
// =============================================================================
INO
  cat > "$DST/sketch.yaml" <<'YAML'
profiles:
  default:
    fqbn: esp32:esp32:esp32s3:USBMode=hwcdc,CDCOnBoot=cdc,PSRAM=opi,DebugLevel=none
    platforms:
      - platform: esp32:esp32
    libraries:
      - name: LovyanGFX
YAML
}
make_receiver "RC_FPV_Receiver_240x240" 0
make_receiver "RC_FPV_Receiver_240x280" 1

echo "Generated sketches:"
for d in "$ROOT"/*/; do echo "  $d ($(ls "$d" | wc -l) files)"; done
