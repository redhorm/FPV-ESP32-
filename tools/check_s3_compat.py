#!/usr/bin/env python3
"""Static safety/protocol checks for the dual-USB-C ESP32-S3-CAM profile.

These intentionally require no hardware or downloaded toolchain.
Run from any directory: python3 tools/check_s3_compat.py
"""
from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[1]
camera = ROOT / "camera_esp32cam"
receiver = ROOT / "receiver_s3_st7789"

def need(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)

tx_protocol = (camera / "include/protocol.h").read_bytes()
rx_protocol = (receiver / "include/protocol.h").read_bytes()
need(tx_protocol == rx_protocol, "Camera/receiver UDP v2 header drift")
need(b"#define FPV_PROTOCOL_VERSION   2" in tx_protocol, "Wrong protocol version")
need(b"#define FPV_CHUNK_HEADER_SIZE 20" in tx_protocol, "Wrong chunk layout")
need(b"#define FPV_FRAME_HEADER_SIZE 30" in tx_protocol, "Wrong telemetry layout")

pins = (camera / "include/camera_pins.h").read_text()
need("#if defined(FPV_BOARD_ESP32S3_CAM)" in pins, "Missing S3 camera profile")
s3 = pins.split("#if defined(FPV_BOARD_ESP32S3_CAM)", 1)[1].split(
    "#else  // original AI-Thinker ESP32-CAM:", 1
)[0]
expected = {
    "PWDN_GPIO_NUM": -1, "RESET_GPIO_NUM": -1,
    "XCLK_GPIO_NUM": 15, "SIOD_GPIO_NUM": 4, "SIOC_GPIO_NUM": 5,
    "Y9_GPIO_NUM": 16, "Y8_GPIO_NUM": 17, "Y7_GPIO_NUM": 18,
    "Y6_GPIO_NUM": 12, "Y5_GPIO_NUM": 10, "Y4_GPIO_NUM": 8,
    "Y3_GPIO_NUM": 9, "Y2_GPIO_NUM": 11,
    "VSYNC_GPIO_NUM": 6, "HREF_GPIO_NUM": 7, "PCLK_GPIO_NUM": 13,
    "FPV_SD_CLK_GPIO": 39, "FPV_SD_CMD_GPIO": 38, "FPV_SD_D0_GPIO": 40,
}
for key, val in expected.items():
    match = re.search(r"^#define\s+" + re.escape(key) + r"\s+(-?\d+)\s*$",
                      s3, re.MULTILINE)
    need(match and int(match.group(1)) == val, f"S3 GPIO mismatch: {key}")
cam_pins = {v for k, v in expected.items()
            if k not in ("PWDN_GPIO_NUM", "RESET_GPIO_NUM") and
            not k.startswith("FPV_SD_")}
sd_pins = {expected[k] for k in expected if k.startswith("FPV_SD_")}
need(not (cam_pins & sd_pins), "Camera and SD signals share GPIOs")

ini = (camera / "platformio.ini").read_text()
need("[env:esp32cam]" in ini, "Original camera build removed")
need("[env:esp32s3cam_n16r8]" in ini, "New S3 build missing")
need("-DFPV_BOARD_ESP32S3_CAM=1" in ini, "S3 build flag absent")
need("qio_opi" in ini, "Octal PSRAM mode absent")

config = (camera / "include/config.h").read_text()
need("#define ENABLE_CAMERA_LED  false" in config, "GPIO4 LED hazard on S3")
need("#define VIDEO_USE_UDP        1" in config, "UDP v2 transport disabled")
need("#define UDP_PAYLOAD_SIZE     1200" in config, "RX chunk size mismatch")

recorder = (camera / "src/sd_recorder.cpp").read_text()
need("SD_MMC.setPins(FPV_SD_CLK_GPIO, FPV_SD_CMD_GPIO, FPV_SD_D0_GPIO)" in recorder,
     "S3 SDMMC remapping is absent")

print("PASS: UDP v2 byte-identical, camera/SD GPIOs isolated, S3 N16R8 profile present")
