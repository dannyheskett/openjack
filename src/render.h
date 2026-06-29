#ifndef OPENJACK_RENDER_H
#define OPENJACK_RENDER_H

#include "game.h"
#include <stdbool.h>

// Fixed card metrics (reused from openklondike) — cards never scale.
#define CARD_W   80
#define CARD_H   112

// Minimum window, just big enough for both hands, the HUD and the action row.
// Both multiples of 16 so the recorder can capture them.
#define MIN_W    624   // 39 * 16
#define MIN_H    528   // 33 * 16

// On-screen action buttons (phase-dependent).
typedef enum {
    BTN_NONE = 0,
    BTN_HIT,
    BTN_STAND,
    BTN_DOUBLE,
    BTN_DEAL,        // "Deal" in BET, "Next Hand" in RESULT
    BTN_BET_DOWN,
    BTN_BET_UP,
} Button;

void render_init(void);
void render_cleanup(void);
bool render_window_should_close(void);
void render_toggle_fullscreen(void);

void render_frame(const Game* g);
void render_menu(const char* title, const char** labels, int count,
                 int selected, int gap_before);

// Which active button is under the pixel (BTN_NONE if none / disabled).
Button render_button_at(const Game* g, int mx, int my);

#endif
