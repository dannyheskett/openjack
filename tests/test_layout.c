// Unit tests for the table layout: no raylib, no window. layout.c is included
// directly, with a stub safe area, so every device shape can be asserted
// without a device.
//
// The invariants that matter here are the ones a rotating, phone-to-iPad game
// gets wrong: cards must stay readable, nothing may overlap or leave the view,
// chrome must not resize when the device has not, and a tap must land on the
// button drawn under it.
//
// Built and run by `make test`. A non-zero exit means a failure.
#include "../src/layout.c"
#include "../src/game.c"

// No device here, so no insets: the stub stands in for safe_area.c, which on
// Android is fed by the Activity and everywhere else returns zeros anyway.
SafeArea safe_area_get(void) { SafeArea s = {0}; return s; }

#include <stdio.h>

static int failures = 0;

#define CHECK(cond)                                                          \
    do {                                                                     \
        if (!(cond)) {                                                       \
            printf("  FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);         \
            failures++;                                                      \
        }                                                                    \
    } while (0)

// Device shapes, in the device pixels the game is handed.
typedef struct { const char* name; int w, h; } Shape;
static const Shape SHAPES[] = {
    { "iPhone SE",         750, 1334 },
    { "iPhone 15",        1179, 2556 },
    { "iPhone 15 Pro Max",1290, 2796 },
    { "Pixel 7",          1080, 2400 },
    { "iPad 10.9",        1640, 2360 },
    { "iPad Pro 13",      2064, 2752 },
    { "phone browser",     390,  844 },
    { "desktop default",   960,  720 },
    { "desktop min",       640,  480 },
    { "desktop 1080p",    1920, 1080 },
    { "desktop 4K",       3840, 2160 },
};
static const int SHAPE_COUNT = (int)(sizeof SHAPES / sizeof SHAPES[0]);

typedef struct { int x, y, w, h; } Box;

static bool overlaps(Box a, Box b) {
    return a.x < b.x + b.w && b.x < a.x + a.w && a.y < b.y + b.h && b.y < a.y + a.h;
}

static bool inside(Box a, int view_w, int view_h) {
    return a.x >= 0 && a.y >= 0 && a.x + a.w <= view_w && a.y + a.h <= view_h;
}

// A game in each phase, with every button the phase can show enabled.
static Game* game_in(GamePhase phase) {
    Game* g = game_create(7, rules_default());
    g->phase = phase;
    if (phase != PHASE_READY) {
        g->nhands = 1;
        g->hands[0].cards[0] = (Card){ 8, 0 };
        g->hands[0].cards[1] = (Card){ 8, 1 };
        g->hands[0].n = g->hands[0].vis = 2;
        g->dealer.cards[0] = (Card){ 1, 2 };
        g->dealer.cards[1] = (Card){ 9, 3 };
        g->dealer.n = g->dealer.vis = 2;
    }
    return g;
}

// --- Cards are readable everywhere, in both orientations -------------------
static void test_cards_readable(void) {
    for (int s = 0; s < SHAPE_COUNT; s++) {
        for (int rot = 0; rot < 2; rot++) {
            int w = rot ? SHAPES[s].h : SHAPES[s].w;
            int h = rot ? SHAPES[s].w : SHAPES[s].h;
            Layout l = layout_for(w, h);
            int shortd = (w < h) ? w : h;
            int floor_px = shortd / 8;
            if (floor_px < 40) floor_px = 40;
            if (l.card_w < floor_px) {
                printf("  FAIL %s %dx%d: card %d < %d\n", SHAPES[s].name, w, h, l.card_w, floor_px);
                failures++;
            }
            CHECK(l.card_h == l.card_w * CARD_ASPECT_H / CARD_ASPECT_W);
        }
    }
}

// --- Chrome does not change size when the device turns ---------------------
static void test_chrome_stable_on_rotation(void) {
    for (int s = 0; s < SHAPE_COUNT; s++) {
        Layout a = layout_for(SHAPES[s].w, SHAPES[s].h);
        Layout b = layout_for(SHAPES[s].h, SHAPES[s].w);
        CHECK(a.title_fs == b.title_fs);
        CHECK(a.titlebar_h == b.titlebar_h);
        CHECK(a.status_fs == b.status_fs);
        CHECK(a.label_fs == b.label_fs);
        CHECK(a.btn_fs == b.btn_fs && a.btn_h == b.btn_h);
    }
}

// --- Everything stays in the view and nothing overlaps ---------------------
static void check_table(const char* name, int w, int h) {
    Layout l = layout_for(w, h);
    int chrome = l.status_y + l.status_h;

    Box dealer_row = { l.table_x, l.dealer_label_y, l.table_w, l.label_h + l.card_h };
    CHECK(dealer_row.y >= chrome);
    CHECK(inside(dealer_row, w, h));
    Box msg = { l.table_x, l.msg_y, l.table_w, l.msg_h };
    CHECK(!overlaps(msg, dealer_row));

    // Four hands of six cards: every card inside its slot, the slots apart,
    // everything under the message band.
    Box slots[MAX_HANDS];
    for (int n = 1; n <= MAX_HANDS; n++) {
        for (int i = 0; i < n; i++) {
            int sx, sy, sw;
            layout_hand_slot(&l, n, i, &sx, &sy, &sw);
            slots[i] = (Box){ sx, sy, sw, l.card_h + l.label_h };
            CHECK(inside(slots[i], w, h));
            CHECK(sy >= msg.y + msg.h);
            CHECK(sx >= l.table_x && sx + sw <= l.table_x + l.table_w);
            for (int c = 0; c < 6; c++) {
                int cx = layout_card_x(&l, sx, sw, 6, c);
                CHECK(cx >= sx && cx + l.card_w <= sx + sw);
            }
            // A two-card hand never closes tighter than 2/5 of a card, what
            // the cards are sized against, however many hands share the row.
            int step2 = layout_card_x(&l, sx, sw, 2, 1) - layout_card_x(&l, sx, sw, 2, 0);
            if (step2 < l.card_w * 2 / 5) {
                printf("  FAIL %s %dx%d: %d hands, fan %d < 2/5 of %d\n", name, w, h, n, step2, l.card_w);
                failures++;
            }
            if (n == 1) CHECK(step2 == l.fan);   // a lone hand gets the full fan
            for (int j = 0; j < i; j++) CHECK(!overlaps(slots[i], slots[j]));
        }
    }

    // Buttons, in every phase: inside the view, clear of the cards, and each
    // one's centre hits it.
    GamePhase phases[3] = { PHASE_READY, PHASE_PLAYER, PHASE_RESULT };
    for (int p = 0; p < 3; p++) {
        Game* g = game_in(phases[p]);
        Btn b[MAX_BUTTONS];
        int nb = layout_buttons(&l, g, b);
        CHECK(nb > 0);
        for (int i = 0; i < nb; i++) {
            Box bb = { b[i].x, b[i].y, b[i].w, b[i].h };
            if (!inside(bb, w, h)) {
                printf("  FAIL %s %dx%d: button %d outside the view\n", name, w, h, b[i].id);
                failures++;
            }
            CHECK(!overlaps(bb, dealer_row));
            CHECK(!overlaps(bb, msg));
            for (int n = 1; n <= MAX_HANDS; n++)
                for (int k = 0; k < n; k++) {
                    int sx, sy, sw;
                    layout_hand_slot(&l, n, k, &sx, &sy, &sw);
                    Box slot = { sx, sy, sw, l.card_h + l.label_h };
                    if (overlaps(bb, slot)) {
                        printf("  FAIL %s %dx%d: button %d overlaps hand %d/%d\n", name, w, h, b[i].id, k, n);
                        failures++;
                    }
                }
            for (int j = 0; j < i; j++) CHECK(!overlaps(bb, (Box){ b[j].x, b[j].y, b[j].w, b[j].h }));
            if (b[i].on)
                CHECK(layout_button_at(&l, g, b[i].x + b[i].w / 2, b[i].y + b[i].h / 2) == b[i].id);
            CHECK(b[i].h >= l.btn_h);
        }
        CHECK(layout_button_at(&l, g, -5, -5) == BTN_NONE);
        game_destroy(g);
    }
}

// With one or two hands, a view that keeps a second row of hands centres the
// table in the space instead; it must still clear the chrome and the buttons.
static void check_shifted(const char* name, int w, int h) {
    Layout l = layout_for_hands(w, h, 1);
    Layout full = layout_for(w, h);
    CHECK(l.card_w == full.card_w && l.btn_y == full.btn_y);
    CHECK(l.dealer_label_y >= l.status_y + l.status_h);
    int bottom = l.player_y + l.card_h + l.label_h;
    int limit = l.side_buttons ? h : l.btn_y;
    if (bottom > limit) {
        printf("  FAIL %s %dx%d: shifted hands run into the buttons\n", name, w, h);
        failures++;
    }
}

static void test_table_fits_everywhere(void) {
    for (int s = 0; s < SHAPE_COUNT; s++) {
        check_table(SHAPES[s].name, SHAPES[s].w, SHAPES[s].h);
        check_table(SHAPES[s].name, SHAPES[s].h, SHAPES[s].w);
        check_shifted(SHAPES[s].name, SHAPES[s].w, SHAPES[s].h);
        check_shifted(SHAPES[s].name, SHAPES[s].h, SHAPES[s].w);
    }
}

// --- The phase decides the buttons ------------------------------------------
static void test_buttons_follow_the_game(void) {
    Layout l = layout_for(960, 720);
    Btn b[MAX_BUTTONS];

    Game* g = game_in(PHASE_PLAYER);
    CHECK(layout_buttons(&l, g, b) == 3);
    CHECK(b[0].id == BTN_HIT && b[1].id == BTN_STAND);
    CHECK(b[2].id == BTN_SPLIT && b[2].on);       // 8 8
    g->hands[0].cards[1] = (Card){ 9, 1 };        // 8 9: no split
    layout_buttons(&l, g, b);
    CHECK(b[2].id == BTN_SPLIT && !b[2].on);
    CHECK(layout_button_at(&l, g, b[2].x + 1, b[2].y + 1) == BTN_NONE);   // disabled

    g->q_len = 1;                                 // a card still landing
    CHECK(layout_buttons(&l, g, b) == 0);
    g->q_len = 0;
    game_destroy(g);

    g = game_in(PHASE_READY);
    CHECK(layout_buttons(&l, g, b) == 1 && b[0].id == BTN_DEAL && b[0].on);
    game_destroy(g);

    g = game_in(PHASE_RESULT);
    CHECK(layout_buttons(&l, g, b) == 1 && b[0].id == BTN_DEAL && b[0].on);
    game_destroy(g);
}

// --- Long hands compress instead of overflowing -----------------------------
static void test_fan_compresses(void) {
    Layout l = layout_for(960, 720);
    int sx, sy, sw;
    layout_hand_slot(&l, 4, 0, &sx, &sy, &sw);
    int natural = layout_card_x(&l, 0, 100000, 2, 1) - layout_card_x(&l, 0, 100000, 2, 0);
    CHECK(natural == l.fan);
    int n = 12;
    int step = layout_card_x(&l, sx, sw, n, 1) - layout_card_x(&l, sx, sw, n, 0);
    CHECK(step < l.fan);
    CHECK(layout_card_x(&l, sx, sw, n, n - 1) + l.card_w <= sx + sw);
}

int main(void) {
    printf("test_layout: readable cards, stable chrome, no overlaps, hit tests\n");
    test_cards_readable();
    test_chrome_stable_on_rotation();
    test_table_fits_everywhere();
    test_buttons_follow_the_game();
    test_fan_compresses();
    if (failures == 0) { printf("OK: all checks passed\n"); return 0; }
    printf("FAILED: %d check(s)\n", failures);
    return 1;
}
