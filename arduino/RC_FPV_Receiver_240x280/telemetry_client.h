// telemetry_client.h - send UDP commands to the camera, poll/parse status
#pragma once
#include <stdbool.h>

void telemetry_begin();

// Fire-and-forget a command token (see protocol.h CMD_*). Optionally with an
// integer argument, e.g. send_cmd(CMD_SET_QUALITY, 10).
void telemetry_send(const char* cmd);
void telemetry_send_arg(const char* cmd, long arg);

// Poll for status replies and periodically request a fresh STATUS. Parses
// replies into g_rx. Non-blocking.
void telemetry_loop();
