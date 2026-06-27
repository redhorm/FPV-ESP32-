// ui_menu.h - boot screen, quick menu, system info, error screens
#pragma once
#include <stdint.h>
#include "button_handler.h"

// Animated boot screen (neon wireframe skull HUD). Blocking-ish but bounded:
// it runs for ~`duration_ms`, animating in small non-blocking steps.
void menu_boot_screen(uint32_t duration_ms);

// Returns true while the quick menu / a sub-screen is active (receiver in MENU).
bool menu_is_active();

// Open / close the quick menu.
void menu_open();
void menu_close();

// Feed a button event to the menu. Returns true if the event was consumed.
bool menu_handle_button(ButtonEvent ev);

// Draw the active menu screen (throttled internally). Call from loop while MENU.
void menu_draw();

// Full-screen error page (used by the ERROR_RECONNECTING state).
void menu_draw_error();
