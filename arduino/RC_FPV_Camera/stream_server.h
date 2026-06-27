// stream_server.h - low-latency TCP JPEG stream + optional HTTP debug endpoints
#pragma once
#include <esp_camera.h>
#include <stdbool.h>

// Start the TCP video server (port VIDEO_TCP_PORT) and the optional HTTP debug
// server (port HTTP_DEBUG_PORT, /status + /snapshot).
void stream_begin();

// True while a receiver is connected to the video port.
bool stream_has_client();

// Send one JPEG frame (with FrameHeader) to the connected client, if any.
// Non-blocking-friendly: if no client, returns immediately. Builds the header
// from g_status so telemetry is piggybacked. Returns true if a frame was sent.
bool stream_send_frame(camera_fb_t* fb);

// Service accept()/timeout bookkeeping and the HTTP debug server. Call often.
void stream_loop();
