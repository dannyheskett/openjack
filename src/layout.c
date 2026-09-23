#include "layout.h"
#include "safe_area.h"

static int imax(int a, int b) { return (a > b) ? a : b; }
static int imin(int a, int b) { return (a < b) ? a : b; }

// Chrome is sized from the device's LONG edge, never from the live height.
// This game rotates, and height-based divisors halve every number the moment
// the phone turns sideways -- the wordmark and status text would resize on a
// screen that has not changed. Same rule as openklondike's chrome_ref().
static int chrome_ref(int view_w, int view_h) { return imax(view_w, view_h); }

static int title_fs_of(int ref)  { int fs = ref / 45; return (fs < 10) ? 10 : fs; }
static int title_bar_of(int ref) { int fs = title_fs_of(ref); return fs + fs / 2; }
static int status_fs_of(int ref) { int fs = ref / 38; return (fs < 9) ? 9 : fs; }

// The wordmark bar, grown to clear a display cutout when the surface draws
// under one. iOS hands the game a viewport that already excludes the notch, so
// this only fires on Android.
static int top_bar_of(int ref) {
    int bar = title_bar_of(ref);
    SafeArea s = safe_area_get();
    return (s.top > bar) ? s.top : bar;
}

// Cards in a hand overlap by all but 2/5 of a card: enough to read each
// card's corner index.
#define FAN_NUM 2
#define FAN_DEN 5

#define MIN_CARD_W 16

// A button fits "Next Hand" at the button font.
static int button_w_of(int fs) { return fs * 6; }

// Fit the table below the chrome with the buttons in a bottom band or a right
// column, and the hands in one row of four or two rows of two.
static Layout arrange(const Layout* base, bool side, int hand_rows) {
    Layout l = *base;
    SafeArea sa = safe_area_get();
    int left   = l.margin + sa.left;
    int right  = l.view_w - l.margin - sa.right;
    int top    = l.status_y + l.status_h + l.margin;
    int bottom = l.view_h - l.margin - sa.bottom;

    l.side_buttons = side;
    l.hand_rows = hand_rows;
    if (side) {
        l.btn_w = button_w_of(l.btn_fs);
        l.btn_x = right - l.btn_w;
        l.btn_y = top;
        l.btn_area_h = bottom - top;
        l.table_x = left;
        l.table_w = l.btn_x - l.margin - left;
    } else {
        l.table_x = left;
        l.table_w = right - left;
        l.btn_area_h = l.btn_h;
        l.btn_x = left;
        l.btn_w = l.table_w;
        l.btn_y = bottom - l.btn_area_h;
        bottom = l.btn_y - l.margin;
    }
    if (l.table_w < 1) l.table_w = 1;
    int avail_h = bottom - top;

    // Width: the most hands a row can hold, two fanned cards each.
    int per_row = MAX_HANDS / hand_rows;
    int slot_gap = 2 * l.gap;
    int slot = (l.table_w - (per_row - 1) * slot_gap) / per_row;
    int cw_w = slot * FAN_DEN / (FAN_DEN + FAN_NUM);

    // Height: the dealer's row and every row of hands, each with its total,
    // around the message band.
    int fixed = l.label_h + l.gap + l.msg_h + l.gap
              + hand_rows * l.label_h + (hand_rows - 1) * l.gap;
    int ch = (avail_h - fixed) / (1 + hand_rows);
    int cw_h = ch * CARD_ASPECT_W / CARD_ASPECT_H;

    l.card_w = imax(imin(cw_w, cw_h), MIN_CARD_W);
    l.card_h = l.card_w * CARD_ASPECT_H / CARD_ASPECT_W;
    l.fan = l.card_w * FAN_NUM / FAN_DEN;

    int block = l.label_h + l.card_h + l.gap + l.msg_h + l.gap
              + hand_rows * (l.card_h + l.label_h) + (hand_rows - 1) * l.gap;
    int y0 = top + (avail_h - block) / 2;
    if (y0 < top) y0 = top;
    l.dealer_label_y = y0;
    l.dealer_y = y0 + l.label_h;
    l.msg_y = l.dealer_y + l.card_h + l.gap;
    l.player_y = l.msg_y + l.msg_h + l.gap;
    l.row_step = l.card_h + l.label_h + l.gap;

    // Cards arrive from off the top-right corner, where a shoe would sit.
    l.shoe_x = l.view_w;
    l.shoe_y = -l.card_h;
    return l;
}

Layout layout_for(int view_w, int view_h) {
    Layout b = (Layout){0};
    b.view_w = view_w;
    b.view_h = view_h;

    int shortd = imin(view_w, view_h);
    int ref    = chrome_ref(view_w, view_h);

    b.margin     = imax(shortd / 28, 6);
    b.gap        = imax(shortd / 90, 4);
    b.titlebar_h = top_bar_of(ref);
    b.title_fs   = title_fs_of(ref);
    b.status_fs  = status_fs_of(ref);
    b.status_h   = b.status_fs * 3 / 2;
    b.status_y   = b.titlebar_h + b.margin / 2;

    b.label_fs = b.status_fs;
    b.label_h  = b.label_fs * 3 / 2;
    b.msg_fs   = b.status_fs * 5 / 4;
    b.msg_h    = b.msg_fs * 3 / 2;
    b.btn_fs   = b.status_fs;
    b.btn_h    = b.btn_fs * 9 / 4;

    // Biggest cards win; ties keep the earlier (bottom band, one row) choice.
    Layout best = arrange(&b, false, 1);
    const bool sides[3] = { false, true, true };
    const int  rows[3]  = { 2, 1, 2 };
    for (int i = 0; i < 3; i++) {
        Layout t = arrange(&b, sides[i], rows[i]);
        if (t.card_w > best.card_w) best = t;
    }
    return best;
}

Layout layout_for_hands(int view_w, int view_h, int nhands) {
    Layout l = layout_for(view_w, view_h);
    if (l.hand_rows == 2 && nhands <= 2) {
        int dy = l.row_step / 2;
        l.dealer_label_y += dy;
        l.dealer_y += dy;
        l.msg_y += dy;
        l.player_y += dy;
    }
    return l;
}

// --------------------------------------------------------------------------
// Hands
// --------------------------------------------------------------------------
void layout_hand_slot(const Layout* l, int nhands, int i, int* x, int* y, int* w) {
    if (nhands < 1) nhands = 1;
    int rows    = (l->hand_rows == 2 && nhands > 2) ? 2 : 1;
    int per_row = (rows == 2) ? 2 : nhands;
    int row = i / per_row, col = i % per_row;
    int in_row = (row == rows - 1) ? nhands - row * per_row : per_row;

    int slot_gap = 2 * l->gap;
    int sw = (l->table_w - (per_row - 1) * slot_gap) / per_row;
    int row_w = in_row * sw + (in_row - 1) * slot_gap;
    int x0 = l->table_x + (l->table_w - row_w) / 2;   // a short last row is centred
    *x = x0 + col * (sw + slot_gap);
    *y = l->player_y + row * l->row_step;
    *w = sw;
}

int layout_card_x(const Layout* l, int slot_x, int slot_w, int n, int i) {
    if (n <= 1) return slot_x + (slot_w - l->card_w) / 2;
    int step = l->fan;
    if (l->card_w + (n - 1) * step > slot_w)
        step = imax((slot_w - l->card_w) / (n - 1), 1);
    int width = l->card_w + (n - 1) * step;
    return slot_x + (slot_w - width) / 2 + i * step;
}

int layout_dealer_card_x(const Layout* l, int n, int i) {
    return layout_card_x(l, l->table_x, l->table_w, n, i);
}

// --------------------------------------------------------------------------
// Buttons
// --------------------------------------------------------------------------
static int phase_buttons(const Game* g, Btn* b) {
    int n = 0;
    if (!g || game_busy(g)) return 0;
    switch (g->phase) {
    case PHASE_READY:
    case PHASE_RESULT:
        b[n++] = (Btn){ .id = BTN_DEAL, .on = true };
        break;
    case PHASE_PLAYER:
        b[n++] = (Btn){ .id = BTN_HIT,   .on = true };
        b[n++] = (Btn){ .id = BTN_STAND, .on = true };
        b[n++] = (Btn){ .id = BTN_SPLIT, .on = game_can_split(g) };
        break;
    }
    return n;
}

int layout_buttons(const Layout* l, const Game* g, Btn* out) {
    int n = phase_buttons(g, out);
    if (n == 0) return 0;
    int h = l->btn_h, gap = l->gap;

    if (l->side_buttons) {
        // One button per row, centred down the column.
        int total = n * h + (n - 1) * gap;
        int y = l->btn_y + (l->btn_area_h - total) / 2;
        for (int i = 0; i < n; i++) {
            out[i].x = l->btn_x;
            out[i].y = y + i * (h + gap);
            out[i].w = l->btn_w;
            out[i].h = h;
        }
        return n;
    }

    // Bottom band: one row, centred.
    int w = imin(button_w_of(l->btn_fs), (l->btn_w - (n - 1) * gap) / n);
    int x = l->btn_x + (l->btn_w - (n * w + (n - 1) * gap)) / 2;
    for (int i = 0; i < n; i++) {
        out[i].w = w;
        out[i].h = h;
        out[i].x = x + i * (w + gap);
        out[i].y = l->btn_y;
    }
    return n;
}

Button layout_button_at(const Layout* l, const Game* g, int x, int y) {
    Btn b[MAX_BUTTONS];
    int n = layout_buttons(l, g, b);
    for (int i = 0; i < n; i++)
        if (b[i].on && x >= b[i].x && x < b[i].x + b[i].w && y >= b[i].y && y < b[i].y + b[i].h)
            return b[i].id;
    return BTN_NONE;
}
