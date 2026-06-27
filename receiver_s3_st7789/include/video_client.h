// video_client.h - TCP client for the low-latency JPEG stream (port 81)
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "protocol.h"

void video_begin();

// Pump the socket: connect if needed, read header(s)+JPEG, keeping only the
// freshest complete frame. Non-blocking. Returns true when a NEW frame became
// available since the last call (caller should render it).
bool video_loop();

// Access the most recently received complete frame. Valid until next video_loop.
const uint8_t* video_frame_data();
uint32_t       video_frame_len();
const FrameHeader* video_frame_header();

bool video_is_connected();

// Force a reconnect (used by the "Reconnect Stream" menu action).
void video_request_reconnect();
