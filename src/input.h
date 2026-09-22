#ifndef OPENJACK_INPUT_H
#define OPENJACK_INPUT_H

#include <stdbool.h>

typedef struct {
    // Mouse (desktop, and the web build in a desktop browser)
    int  mouse_x, mouse_y;
    bool left_pressed;        // left button just went down

    // Menu / overlays
    bool escape_pressed;      // Escape, a two-finger tap, or Android Back
    bool menu_up, menu_down;  // arrow keys / W S, or a vertical swipe
    bool menu_left, menu_right; // Left / Right / A D, or a horizontal swipe:
                                // cycles a value on the Options screen
    bool select_pressed;      // Enter or Space
    bool any_pressed;         // any of the above this frame (dismisses a notice)

    // Table keys (keyboard only; touch uses the on-screen buttons)
    bool key_hit, key_stand, key_double, key_split, key_surrender;  // H S D P R
    bool key_insure, key_decline;   // I or Y / N
    bool bet_up, bet_down;          // Right Up + / Left Down -

    // Touch: a completed one-finger tap. tap_x/tap_y are valid only when
    // touch_tap is set. Presses a button, or picks a menu item.
    bool  touch_tap;
    float tap_x, tap_y;

    // Window
    bool fullscreen_toggle;   // Alt+Enter
} Input;

Input input_poll(void);

#endif
