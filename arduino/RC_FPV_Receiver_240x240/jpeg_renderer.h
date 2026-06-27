// jpeg_renderer.h - decode a JPEG buffer straight to the ST7789
#pragma once
#include <stdint.h>
#include <stdbool.h>

// Decode `len` bytes of JPEG at `data` and blit to the display. For QVGA input
// the image is centre-cropped to 240x240. Returns false on decode error (the
// caller raises a JPEG_DECODE error and keeps the link alive).
bool jpeg_draw(const uint8_t* data, uint32_t len);
