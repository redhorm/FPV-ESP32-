// =============================================================================
//  display.h  -  single shared LovyanGFX device + helpers
// -----------------------------------------------------------------------------
//  One global display object is shared by jpeg_renderer, ui_overlay and
//  ui_menu. The concrete LGFX class (with the ST7789 pins from pins.h) is
//  defined in jpeg_renderer.cpp so the panel config lives next to the code that
//  pushes pixels. We expose it here as an abstract LovyanGFX reference.
// =============================================================================
#pragma once
#include <LovyanGFX.hpp>

// The concrete device instance lives in jpeg_renderer.cpp.
extern LGFX_Device& display();

// Bring up the panel: SPI, reset, rotation, backlight on, cleared to black.
void display_begin();

// Backlight control (TFT_BL).
void display_backlight(bool on);
