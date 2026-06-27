// wifi_client.h - join the camera AP, monitor link, auto-reconnect
#pragma once
#include <stdbool.h>

void wifi_begin();          // start connecting (non-blocking)
void wifi_loop();           // maintain connection, update g_rx.wifi_connected/rssi
bool wifi_is_connected();
