#ifndef OPENJACK_RENDER_H
#define OPENJACK_RENDER_H

#include "game.h"
#include "layout.h"
#include "platform.h"
#include "oj_types.h"
#include <stdbool.h>

// Window setup and teardown (window.c) plus the recorder's capture canvas. The
// table fits itself to whatever the window is (layout.c).
void render_init(void);
void render_cleanup(void);

// Scenes -------------------------------------------------------------------
// The table.
void render_frame(const Game* g);
// The table with a notice panel over it (the bankroll ran out).
void render_notice(const Game* g, const char* title);
// The family menu (menu.c) on the felt. gap_before, if >= 0, inserts a blank
// line before that item index. Hit-test its rows with menu_hit_test().
void render_menu(const char* title, const char* const* labels, int count,
                 int selected, int gap_before);

// Hit tests ----------------------------------------------------------------
// The enabled button at a window point, or BTN_NONE.
Button render_button_at(const Game* g, int x, int y);
// Bottom edge of the status line in the live window. On touch, a tap above it
// -- the wordmark bar and the status line -- opens the menu.
int render_chrome_bottom(void);

// Queries ------------------------------------------------------------------
// Card width in pixels for the current window. The touch layer scales its
// tap-movement tolerance from it.
int render_card_size(void);

#endif
