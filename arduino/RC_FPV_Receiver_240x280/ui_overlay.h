// ui_overlay.h - the anti-flicker FPV overlay drawn on top of the live video
#pragma once
#include <stdint.h>

// Update local stats from the latest frame header (latency, drops, mirrored
// camera telemetry). Call once per drawn frame.
struct FrameHeader;
void overlay_ingest(const FrameHeader* h, uint32_t now_ms);

// Draw the overlay. Internally throttled to OVERLAY_REFRESH_HZ and uses dirty
// regions so it does not repaint (or flicker) every video frame. `force`
// repaints everything (e.g. after returning from the menu).
void overlay_draw(bool force);

// Mark the overlay dirty so the next overlay_draw repaints fully.
void overlay_invalidate();
