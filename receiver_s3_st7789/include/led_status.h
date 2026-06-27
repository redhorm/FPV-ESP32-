// led_status.h - onboard RGB status LED (GPIO48), compile-out friendly
#pragma once

void led_begin();
// Reflect g_rx.state / recording into the LED colour. Handles blink timing.
void led_loop();
