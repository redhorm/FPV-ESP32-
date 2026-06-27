// telemetry.h - FPS accounting, header/status builders, heap sampling
#pragma once
#include <stdint.h>
#include "protocol.h"

// Call once per successfully captured frame to drive the FPS estimator.
void telemetry_on_frame();

// Periodically (TELEMETRY_PERIOD_MS) refresh heap/psram/uptime and error flags
// in g_status. Cheap; call every loop.
void telemetry_loop();

// Populate a FrameHeader from current g_status + the given jpeg length.
void telemetry_fill_header(FrameHeader* h, uint32_t jpeg_len);

// Build the single-line STATUS reply (for UDP REQUEST_STATUS and HTTP /status).
// Writes a NUL-terminated string into `out`; returns its length.
int  telemetry_build_status(char* out, int out_size);
