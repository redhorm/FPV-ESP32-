// =============================================================================
//  ui_theme.h  -  premium dark theme palette + shared layout constants
// -----------------------------------------------------------------------------
//  Colours are RGB565 (the native ST7789 format). The palette is a restrained,
//  high-contrast "HUD" look: near-black background, cool cyan accents and a
//  magenta highlight for REC/alerts - matching the boot artwork's neon vibe.
//  Drawing is done with vector primitives (no bitmaps), so the look scales and
//  stays crisp on 240x240.
// =============================================================================
#pragma once
#include <stdint.h>

// Helper: compile-time RGB888 -> RGB565
#define RGB565(r, g, b) ((uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3)))

// ---- Core palette -----------------------------------------------------------
#define COL_BG          RGB565(  6,  8, 12)   // near-black background
#define COL_PANEL       RGB565( 16, 20, 28)   // raised panel fill
#define COL_PANEL_EDGE  RGB565( 40, 48, 64)   // panel border
#define COL_TEXT        RGB565(225, 232, 240)  // primary text (off-white)
#define COL_TEXT_DIM    RGB565(120, 132, 150)  // secondary text
#define COL_ACCENT      RGB565(  0, 224, 255)  // cyan accent (primary)
#define COL_ACCENT_DK   RGB565(  0, 120, 150)  // dim cyan
#define COL_MAGENTA     RGB565(255,  40, 150)  // magenta highlight / REC
#define COL_OK          RGB565(  0, 230, 130)  // green = good
#define COL_WARN        RGB565(255, 180,  40)  // amber = warning
#define COL_ERR         RGB565(255,  60,  60)  // red = error
#define COL_SHADOW      RGB565(  2,  3,  5)    // pseudo drop-shadow

// ---- Overlay layout (Live View) ---------------------------------------------
#define TOPBAR_H        20    // top status strip height
#define BOTBAR_H        18    // bottom status strip height
#define PAD             4

// ---- Off-road attitude gauge ------------------------------------------------
#define GAUGE_RADIUS    34
#define GAUGE_CX        (DISPLAY_W - GAUGE_RADIUS - 8)
#define GAUGE_CY        (DISPLAY_H - GAUGE_RADIUS - BOTBAR_H - 8)
