#ifndef OPENJACK_LAYOUT_H
#define OPENJACK_LAYOUT_H

// Table geometry, derived from the live view size every frame. Pure: no raylib,
// no globals, no caching -- which is what makes rotation free (the next frame
// simply fits the same table to the new shape) and makes the whole thing
// unit-testable without a window (tests/test_layout.c).
//
// The table, top to bottom: the wordmark bar, the status line (bankroll and
// bet), the dealer's hand under its total, a message band, the player's hands
// with their totals under them, and the action buttons. Cards are one size for
// the whole view, whatever is dealt, so nothing on the table jumps when a hand
// is split.
//
// Four arrangements are tried and the one that gives the biggest cards wins:
//   buttons  in a band along the bottom, or in a column down the right side
//            (a phone held sideways has width to spare and no height);
//   hands    up to four side by side, or two rows of two (a phone held
//            upright has height to spare and no width).

#include "game.h"
#include <stdbool.h>

// Card proportions: openklondike's 80x112.
#define CARD_ASPECT_W 5
#define CARD_ASPECT_H 7

typedef enum {
    BTN_NONE = 0,
    BTN_HIT,
    BTN_STAND,
    BTN_DOUBLE,
    BTN_SPLIT,
    BTN_SURRENDER,
    BTN_INSURE,      // "Insurance", or "Even Money" against a natural
    BTN_DECLINE,
    BTN_BET_DOWN,
    BTN_DEAL,
    BTN_BET_UP,
    BTN_NEXT,
} Button;

#define MAX_BUTTONS 5

typedef struct {
    Button id;
    int  x, y, w, h;
    bool on;         // legal right now; a disabled button is drawn dimmed
} Btn;

typedef struct {
    int view_w, view_h;

    int margin, gap;
    int titlebar_h, title_fs;          // wordmark bar, grown to clear a cutout
    int status_y, status_h, status_fs; // bankroll / bet line

    int table_x, table_w;              // horizontal extent of the hands
    int card_w, card_h;
    int fan;                           // natural step between cards in a hand

    int label_fs, label_h;             // hand totals
    int dealer_label_y, dealer_y;
    int msg_y, msg_h, msg_fs;          // prompt / result band
    int player_y;                      // top of the first row of hands
    int hand_rows;                     // 1, or 2 rows of two hands
    int row_step;                      // player_y of row 2 minus row 1

    bool side_buttons;                 // column on the right instead of a band
    int btn_fs, btn_h;
    int btn_x, btn_y, btn_w, btn_area_h; // the button area
    int btn_rows;                      // bottom band: rows reserved (1 or 2)

    int shoe_x, shoe_y;                // where dealt cards come from
} Layout;

Layout layout_for(int view_w, int view_h);

// The layout for a table holding `nhands` hands. A view that keeps a second
// row for split hands leaves it empty for most rounds; while it is, the table
// moves down half a row so it sits centred instead of leaving a gap above the
// buttons. Card size and buttons are the same as layout_for().
Layout layout_for_hands(int view_w, int view_h, int nhands);

// The horizontal slot and top edge of hand `i` of `nhands`.
void layout_hand_slot(const Layout* l, int nhands, int i, int* x, int* y, int* w);
// Left edge of card `i` of an `n`-card hand in a slot. The fan compresses when
// a long hand would overflow its slot; the hand is centred in it.
int  layout_card_x(const Layout* l, int slot_x, int slot_w, int n, int i);
// Top-left of card `i` of the dealer's `n`-card hand.
int  layout_dealer_card_x(const Layout* l, int n, int i);

// The buttons for the game's current state. None while cards are still landing.
int    layout_buttons(const Layout* l, const Game* g, Btn* out);
// The enabled button at a point, or BTN_NONE.
Button layout_button_at(const Layout* l, const Game* g, int x, int y);

#endif
