// The whole renderer: chrome, the table, the cards, the buttons, the menu and
// the notice panel. There is no second layout to dispatch to -- layout.c fits
// the table to any window, so this file serves desktop, both phone
// orientations and an iPad, re-deriving everything from the live view size
// every frame. Every pixel goes through the gfx primitive layer (gfx.h), so the
// same drawing runs on raylib and on the iOS Metal backend.
#include "render.h"
#include "gfx.h"
#include "safe_area.h"
#include "menu.h"
#include "present.h"
#include "window.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

// --------------------------------------------------------------------------
// Palette: the openklondike set, value for value, so the card games in the
// family look like they come off the same table; plus the button and result
// colours openjack has always used.
// --------------------------------------------------------------------------
static const Color FELT       = { 12,  92,  52, 255};  // classic green table
static const Color FELT_DARK  = { 10,  76,  44, 255};  // title bar
static const Color SLOT_LINE  = { 30, 110,  66, 255};  // rule under the title bar
static const Color CARD_FACE  = {248, 248, 242, 255};
static const Color CARD_EDGE  = { 40,  40,  40, 255};
static const Color CARD_BACK  = { 36,  72, 156, 255};
static const Color CARD_BACK2 = { 80, 130, 220, 255};
static const Color RED_PIP    = {200,  30,  40, 255};
static const Color BLACK_PIP  = { 20,  20,  24, 255};
static const Color HILITE     = {255, 235, 120, 255};  // active hand, selection
static const Color MENU_BG    = { 16,  40,  28, 255};
static const Color TEXT_LIGHT = {235, 235, 225, 255};
static const Color TEXT_DIM   = {170, 190, 175, 255};
static const Color BTN_FILL   = { 22, 120,  72, 255};
static const Color BTN_EDGE   = {120, 200, 150, 255};
static const Color BTN_OFF    = { 16,  70,  48, 255};
static const Color WIN_COL    = {120, 240, 150, 255};
static const Color LOSE_COL   = {236, 110, 100, 255};

// Corner radius as a fraction of the card's short side. Scale-invariant, so
// every card size has the same silhouette.
#define CARD_ROUND 0.12f
#define BTN_ROUND  0.25f

static int imax(int a, int b) { return (a > b) ? a : b; }

// --------------------------------------------------------------------------
// Card art (all vector-drawn, no asset files): openklondike's, unchanged.
// --------------------------------------------------------------------------
static const char* RANK_STR[14] = {
    "", "A", "2", "3", "4", "5", "6", "7", "8", "9", "10", "J", "Q", "K"
};

// Fill a closed polygon as a triangle fan from `c`. Works for any polygon that
// is star-shaped about `c` (true for all of our pip shapes). gfx_triangle is
// winding-independent, so the traced direction does not matter.
static void fill_fan(Vector2 c, const Vector2* p, int n, Color col) {
    for (int i = 0; i < n; i++) gfx_triangle(c, p[i], p[(i + 1) % n], col);
}

#define PIP_SEG 30

// Build a heart outline of total height `s` centred on (cx,cy) into `out`.
// `xsquash` scales the width independently (1.0 = the curve's natural ~1.1:1
// width:height; <1 makes it taller and more upright). flip=true points it
// upward (the spade body). Classic heart curve:
//   x = 16 sin^3 t,  y = 13 cos t - 5 cos 2t - 2 cos 3t - cos 4t
static void heart_outline(float cx, float cy, float s, float xsquash, bool flip,
                          Vector2* out) {
    float xs[PIP_SEG], ys[PIP_SEG];
    float minx = 1e9f, maxx = -1e9f, miny = 1e9f, maxy = -1e9f;
    for (int i = 0; i < PIP_SEG; i++) {
        float t = (float)i / PIP_SEG * 2.0f * PI;
        float st = sinf(t);
        float x = 16.0f * st * st * st;
        float y = -(13.0f * cosf(t) - 5.0f * cosf(2*t) - 2.0f * cosf(3*t) - cosf(4*t));
        xs[i] = x; ys[i] = y;
        if (x < minx) minx = x;
        if (x > maxx) maxx = x;
        if (y < miny) miny = y;
        if (y > maxy) maxy = y;
    }
    float sc = s / (maxy - miny);
    float mx = (minx + maxx) * 0.5f, my = (miny + maxy) * 0.5f;
    for (int i = 0; i < PIP_SEG; i++) {
        float nx = (xs[i] - mx) * sc * xsquash;
        float ny = (ys[i] - my) * sc;
        if (flip) ny = -ny;
        out[i].x = cx + nx;
        out[i].y = cy + ny;
    }
}

// A flared pedestal/stem under the spade and club: a narrow neck widening to
// outward-kicked feet, like the base on a real card pip.
static void draw_stem(float cx, float topy, float s, Color col) {
    float nw = s * 0.05f;   // neck half-width
    float fw = s * 0.34f;   // foot half-width (flares past the body lobes)
    float h  = s * 0.26f;
    Vector2 tl = {cx - nw, topy},          tr = {cx + nw, topy};
    Vector2 ml = {cx - nw * 1.4f, topy + h * 0.55f};
    Vector2 mr = {cx + nw * 1.4f, topy + h * 0.55f};
    Vector2 bl = {cx - fw, topy + h},      br = {cx + fw, topy + h};
    gfx_triangle(tl, ml, mr, col);   // neck
    gfx_triangle(tl, mr, tr, col);
    gfx_triangle(ml, bl, br, col);   // flared foot
    gfx_triangle(ml, br, mr, col);
}

// Draw one suit pip centred at (cx,cy) with overall height s.
static void draw_pip(float cx, float cy, float s, int suit) {
    Color col = (suit == 1 || suit == 2) ? RED_PIP : BLACK_PIP;
    Vector2 pts[PIP_SEG];
    switch (suit) {
        case 1: { // diamond -- a filled rhombus, taller than wide
            float hw = s * 0.34f, hh = s * 0.5f;
            Vector2 p[4] = {{cx, cy - hh}, {cx + hw, cy}, {cx, cy + hh}, {cx - hw, cy}};
            fill_fan((Vector2){cx, cy}, p, 4, col);
            break;
        }
        case 2: { // heart -- upright, slightly taller than wide
            heart_outline(cx, cy, s, 0.86f, false, pts);
            fill_fan((Vector2){cx, cy + s * 0.10f}, pts, PIP_SEG, col);
            break;
        }
        case 3: { // spade -- a narrow upward heart on a flared pedestal
            float body = s * 0.74f;
            float byc  = cy - s * 0.10f;
            heart_outline(cx, byc, body, 0.96f, true, pts);
            fill_fan((Vector2){cx, byc - body * 0.10f}, pts, PIP_SEG, col);
            draw_stem(cx, byc + body * 0.30f, s, col);
            break;
        }
        default: { // clubs -- trefoil of three distinct lobes over a stem
            float cr = s * 0.255f;
            gfx_circle(cx,                cy - s * 0.25f, cr, col);
            gfx_circle(cx - s * 0.245f,   cy + s * 0.11f, cr, col);
            gfx_circle(cx + s * 0.245f,   cy + s * 0.11f, cr, col);
            draw_stem(cx, cy + s * 0.16f, s, col);
            break;
        }
    }
}

// Card-relative metrics, all proportional to the card width so the art is the
// same design at any scale. The floors keep the smallest card legible.
static int card_index_fs(const Layout* L) { return imax(L->card_w * 18 / 80, 8); }
static int card_pad(const Layout* L)      { return imax(L->card_w *  6 / 80, 2); }
static int card_pip_small(const Layout* L){ return imax(L->card_w * 12 / 80, 5); }
static int card_pip_big(const Layout* L)  { return imax(L->card_w * 34 / 80, 12); }

static void draw_card_back(const Layout* L, int x, int y) {
    gfx_rect_rounded(x, y, L->card_w, L->card_h, CARD_ROUND, CARD_BACK);
    gfx_rect_rounded_lines(x, y, L->card_w, L->card_h, CARD_ROUND, CARD_EDGE);
    // A bounded plaid panel inside the card (never spills past its edges).
    int m = imax(L->card_w * 8 / 80, 3);
    int ix = x + m, iy = y + m, iw = L->card_w - 2 * m, ih = L->card_h - 2 * m;
    if (iw <= 0 || ih <= 0) return;
    gfx_rect_lines(ix, iy, iw, ih, CARD_BACK2);
    int step = m;
    for (int gx = ix + step; gx < ix + iw; gx += step)
        gfx_line(gx, iy, gx, iy + ih, CARD_BACK2);
    for (int gy = iy + step; gy < iy + ih; gy += step)
        gfx_line(ix, gy, ix + iw, gy, CARD_BACK2);
}

static void draw_card_face(const Layout* L, int x, int y, Card c, bool hilite) {
    gfx_rect_rounded(x, y, L->card_w, L->card_h, CARD_ROUND, CARD_FACE);
    Color edge = hilite ? HILITE : CARD_EDGE;
    gfx_rect_rounded_lines(x, y, L->card_w, L->card_h, CARD_ROUND, edge);
    if (hilite)
        gfx_rect_rounded_lines(x + 1, y + 1, L->card_w - 2, L->card_h - 2,
                               CARD_ROUND, edge);

    Color col = card_is_red(c) ? RED_PIP : BLACK_PIP;
    const char* rs = RANK_STR[c.rank];
    int fs   = card_index_fs(L);
    int pad  = card_pad(L);
    int psm  = card_pip_small(L);
    int gapy = imax(L->card_w * 8 / 80, 3);

    // Top-left corner index, and the matching bottom-right one (upright; simple
    // and readable at every size).
    gfx_text(rs, x + pad, y + pad * 2 / 3, fs, col);
    draw_pip(x + pad + psm * 0.5f, y + pad * 2 / 3 + fs + gapy, (float)psm, c.suit);
    int tw = gfx_measure_text(rs, fs);
    gfx_text(rs, x + L->card_w - pad - tw, y + L->card_h - pad * 2 / 3 - fs, fs, col);
    draw_pip(x + L->card_w - pad - psm * 0.5f,
             y + L->card_h - pad * 2 / 3 - fs - gapy, (float)psm, c.suit);

    // Large centre pip.
    draw_pip(x + L->card_w / 2.0f, y + L->card_h / 2.0f, (float)card_pip_big(L), c.suit);
}

// --------------------------------------------------------------------------
// Card motion. A dealt card slides in from the shoe over DEAL_STEPS fixed
// steps, easing out; the hole card turns over FLIP_STEPS, squeezed
// horizontally through the middle of the turn. Both are timed from the game's
// own step counter, so pausing in the menu pauses them too.
// --------------------------------------------------------------------------
static float progress(const Game* g, int since, int steps) {
    float p = (float)(g->ticks - since) / (float)steps;
    return (p < 0.0f) ? 0.0f : (p > 1.0f) ? 1.0f : p;
}

static void slide(const Layout* L, const Game* g, int at, int* x, int* y) {
    float p = progress(g, at, DEAL_STEPS);
    float e = 1.0f - (1.0f - p) * (1.0f - p);
    *x = (int)(L->shoe_x + (*x - L->shoe_x) * e);
    *y = (int)(L->shoe_y + (*y - L->shoe_y) * e);
}

// The hole card: a back until it turns, then the squeeze, then the face.
static void draw_hole_card(const Layout* L, const Game* g, int x, int y, Card c) {
    if (!g->hole_shown) { draw_card_back(L, x, y); return; }
    float q = progress(g, g->hole_at, FLIP_STEPS);
    if (q >= 1.0f) { draw_card_face(L, x, y, c, false); return; }
    float squeeze = fabsf(q * 2.0f - 1.0f);
    int w = (int)(L->card_w * squeeze);
    if (w < 2) w = 2;
    int dx = x + (L->card_w - w) / 2;
    gfx_rect_rounded(dx, y, w, L->card_h, CARD_ROUND, (q < 0.5f) ? CARD_BACK : CARD_FACE);
    gfx_rect_rounded_lines(dx, y, w, L->card_h, CARD_ROUND, CARD_EDGE);
}

// --------------------------------------------------------------------------
// Text helpers
// --------------------------------------------------------------------------
// Largest size <= fs at which `text` fits `max_w`.
static int fit_fs(const char* text, int fs, int max_w) {
    while (fs > 8 && gfx_measure_text(text, fs) > max_w) fs--;
    return fs;
}

static void text_centered(const char* text, int cx, int y, int fs, int max_w, Color c) {
    fs = fit_fs(text, fs, max_w);
    gfx_text(text, cx - gfx_measure_text(text, fs) / 2, y, fs, c);
}

// A total as the player reads it: "7/17" while a soft hand can still take a
// card, "Blackjack" for a natural, otherwise the number.
static void format_total(char* buf, int cap, int total, bool soft, bool natural, bool open) {
    if (natural)                          snprintf(buf, cap, "Blackjack");
    else if (soft && total < 21 && open)  snprintf(buf, cap, "%d/%d", total - 10, total);
    else                                  snprintf(buf, cap, "%d", total);
}

static const char* result_word(Result r) {
    switch (r) {
    case RES_BLACKJACK:   return "Blackjack";
    case RES_EVEN_MONEY:  return "Even money";
    case RES_WIN:         return "Win";
    case RES_DEALER_BUST: return "Win";
    case RES_PUSH:        return "Push";
    case RES_LOSE:        return "Lose";
    case RES_BUST:        return "Bust";
    case RES_SURRENDER:   return "Surrender";
    default:              return "";
    }
}

static Color delta_color(int d) { return (d > 0) ? WIN_COL : (d < 0) ? LOSE_COL : TEXT_LIGHT; }

// --------------------------------------------------------------------------
// Chrome
// --------------------------------------------------------------------------
static void draw_title_bar(const Layout* l) {
    gfx_rect(0, 0, l->view_w, l->titlebar_h, FELT_DARK);
    gfx_line(0, l->titlebar_h, l->view_w, l->titlebar_h, SLOT_LINE);
    const char* title = "OPENJACK";
    int fs = l->title_fs;
    int tw = gfx_measure_text(title, fs);
    int ty = (l->titlebar_h - fs) / 2;

    // Keep the wordmark clear of a camera cutout: if the centre is taken, put
    // it on whichever side has room, and if neither has, leave the bar bare.
    SafeArea sa = safe_area_get();
    int cx = (l->view_w - tw) / 2;
    if (sa.cutout_right > sa.cutout_left) {
        int pad = fs / 2;
        bool clash = !(cx + tw + pad <= sa.cutout_left || cx >= sa.cutout_right + pad);
        if (clash) {
            if (sa.cutout_left >= tw + pad) cx = sa.cutout_left - pad - tw;
            else if (l->view_w - sa.cutout_right >= tw + pad) cx = sa.cutout_right + pad;
            else return;
        }
    }
    gfx_text(title, cx, ty, fs, TEXT_LIGHT);
}

// Chips at risk this round: every hand's wager plus any insurance.
static int at_risk(const Game* g) {
    if (g->phase == PHASE_BET) return g->bet;
    int sum = g->insurance;
    for (int i = 0; i < g->nhands; i++) sum += g->hands[i].wager;
    return sum;
}

static void draw_status(const Game* g, const Layout* l) {
    char buf[48];
    int fs = l->status_fs;
    int y = l->status_y + (l->status_h - fs) / 2;
    SafeArea sa = safe_area_get();
    int left = l->margin + sa.left;
    int right = l->view_w - l->margin - sa.right;

    snprintf(buf, sizeof buf, "Bankroll %d", g->shown_bankroll);
    gfx_text(buf, left, y, fs, TEXT_LIGHT);

    snprintf(buf, sizeof buf, "Bet %d", at_risk(g));
    gfx_text(buf, right - gfx_measure_text(buf, fs), y, fs, TEXT_DIM);
}

// --------------------------------------------------------------------------
// The table
// --------------------------------------------------------------------------
static bool round_over(const Game* g) { return g->phase == PHASE_RESULT && g->settled_shown; }

static void draw_dealer(const Game* g, const Layout* l) {
    const Hand* d = &g->dealer;
    if (d->vis == 0) return;

    char tot[24], buf[48];
    bool soft;
    int t = game_visible_total(g, d, &soft);
    bool natural = g->hole_shown && game_hand_is_blackjack(d);
    format_total(tot, sizeof tot, t, false, natural, false);
    snprintf(buf, sizeof buf, "Dealer  %s", tot);
    text_centered(buf, l->table_x + l->table_w / 2,
                  l->dealer_label_y + (l->label_h - l->label_fs) / 2,
                  l->label_fs, l->table_w, TEXT_DIM);

    for (int i = 0; i < d->vis; i++) {
        int x = layout_dealer_card_x(l, d->n, i), y = l->dealer_y;
        slide(l, g, d->at[i], &x, &y);
        if (i == 1) draw_hole_card(l, g, x, y, d->cards[1]);
        else        draw_card_face(l, x, y, d->cards[i], false);
    }
}

static void draw_hands(const Game* g, const Layout* l) {
    for (int h = 0; h < g->nhands; h++) {
        const Hand* hand = &g->hands[h];
        int sx, sy, sw;
        layout_hand_slot(l, g->nhands, h, &sx, &sy, &sw);
        bool active = (g->phase == PHASE_PLAYER && g->nhands > 1 && h == g->active);

        for (int i = 0; i < hand->vis; i++) {
            int x = layout_card_x(l, sx, sw, hand->n, i), y = sy;
            slide(l, g, hand->at[i], &x, &y);
            draw_card_face(l, x, y, hand->cards[i], active);
        }
        if (hand->vis == 0) continue;

        // The total under the hand; once the round is shown, its result too
        // when there is more than one hand (a single hand's result is in the
        // message band).
        char tot[24], buf[64];
        bool soft;
        int t = game_visible_total(g, hand, &soft);
        bool natural = (hand->vis == hand->n) && game_hand_is_blackjack(hand);
        format_total(tot, sizeof tot, t, soft, natural, !hand->done);
        Color c = active ? HILITE : TEXT_LIGHT;
        if (round_over(g) && g->nhands > 1) {
            snprintf(buf, sizeof buf, "%s  %s", tot, result_word(hand->result));
            c = delta_color(hand->delta);
        } else if (hand->doubled) {
            snprintf(buf, sizeof buf, "%s  x2", tot);
        } else {
            snprintf(buf, sizeof buf, "%s", tot);
        }
        text_centered(buf, sx + sw / 2, sy + l->card_h + (l->label_h - l->label_fs) / 2,
                      l->label_fs, sw, c);
    }
}

// Whether to draw the menu hint (see render.h). On by default, so a build that
// only ever sees touch shows it.
static bool s_menu_hint = true;

void render_set_menu_hint(bool show) { s_menu_hint = show; }

// One line between the dealer and the player: what to do, or what happened.
static void draw_message(const Game* g, const Layout* l) {
    char buf[96];
    Color c = TEXT_DIM;
    buf[0] = 0;

    if (g->phase == PHASE_BET) {
        snprintf(buf, sizeof buf, "Place your bet");
    } else if (g->phase == PHASE_INSURANCE && !game_busy(g)) {
        snprintf(buf, sizeof buf, game_offers_even_money(g) ? "Even money?" : "Insurance?");
        c = HILITE;
    } else if (g->phase == PHASE_PLAYER && g->nhands > 1) {
        snprintf(buf, sizeof buf, "Hand %d of %d", g->active + 1, g->nhands);
    } else if (round_over(g)) {
        int d = g->last_delta;
        c = delta_color(d);
        const Hand* h = &g->hands[0];
        bool dealer_bj = game_hand_is_blackjack(&g->dealer);
        char head[48];
        if (g->nhands > 1) {
            snprintf(head, sizeof head, (d > 0) ? "You win %+d" : (d < 0) ? "You lose %+d" : "Even", d);
        } else {
            int hd = h->delta;
            switch (h->result) {
            case RES_BLACKJACK:   snprintf(head, sizeof head, "Blackjack! %+d", hd); break;
            case RES_EVEN_MONEY:  snprintf(head, sizeof head, "Even money %+d", hd); break;
            case RES_WIN:         snprintf(head, sizeof head, "You win %+d", hd); break;
            case RES_DEALER_BUST: snprintf(head, sizeof head, "Dealer busts %+d", hd); break;
            case RES_PUSH:        snprintf(head, sizeof head, "Push"); break;
            case RES_BUST:        snprintf(head, sizeof head, "Bust %+d", hd); break;
            case RES_SURRENDER:   snprintf(head, sizeof head, "Surrendered %+d", hd); break;
            default:              snprintf(head, sizeof head, dealer_bj ? "Dealer blackjack %+d"
                                                                        : "Dealer wins %+d", hd); break;
            }
        }
        if (g->insurance > 0)
            snprintf(buf, sizeof buf, "%s  Insurance %+d", head, g->insurance_delta);
        else
            snprintf(buf, sizeof buf, "%s", head);
    }
    if (buf[0])
        text_centered(buf, l->table_x + l->table_w / 2, l->msg_y + (l->msg_h - l->msg_fs) / 2,
                      l->msg_fs, l->table_w, c);

#ifdef OJ_TOUCH
    // The menu gesture is not discoverable on its own, so the first hands say
    // where it is, in the empty row where the player's cards will land.
    if (s_menu_hint && g->phase == PHASE_BET && g->hands_played < 3)
        text_centered("Tap the title bar or two-finger tap for the menu",
                      l->table_x + l->table_w / 2, l->player_y + (l->card_h - l->label_fs) / 2,
                      l->label_fs, l->table_w, TEXT_DIM);
#endif
}

static const char* button_label(const Game* g, Button id) {
    switch (id) {
    case BTN_HIT:       return "Hit";
    case BTN_STAND:     return "Stand";
    case BTN_DOUBLE:    return "Double";
    case BTN_SPLIT:     return "Split";
    case BTN_SURRENDER: return "Surrender";
    case BTN_INSURE:    return game_offers_even_money(g) ? "Even Money" : "Insurance";
    case BTN_DECLINE:   return "No Thanks";
    case BTN_BET_DOWN:  return "-";
    case BTN_DEAL:      return "Deal";
    case BTN_BET_UP:    return "+";
    case BTN_NEXT:      return "Next Hand";
    default:            return "";
    }
}

static void draw_buttons(const Game* g, const Layout* l) {
    Btn b[MAX_BUTTONS];
    int n = layout_buttons(l, g, b);
    for (int i = 0; i < n; i++) {
        gfx_rect_rounded(b[i].x, b[i].y, b[i].w, b[i].h, BTN_ROUND, b[i].on ? BTN_FILL : BTN_OFF);
        gfx_rect_rounded_lines(b[i].x, b[i].y, b[i].w, b[i].h, BTN_ROUND, b[i].on ? BTN_EDGE : FELT_DARK);
        const char* label = button_label(g, b[i].id);
        int fs = fit_fs(label, l->btn_fs, b[i].w - l->btn_fs);
        gfx_text(label, b[i].x + (b[i].w - gfx_measure_text(label, fs)) / 2,
                 b[i].y + (b[i].h - fs) / 2, fs, b[i].on ? TEXT_LIGHT : TEXT_DIM);
    }
}

// --------------------------------------------------------------------------
// Menu + notice panel (menu.c, the same in every game in this family)
// --------------------------------------------------------------------------
static MenuTheme menu_theme(void) {
    MenuTheme t = { .background = FELT, .panel = MENU_BG, .edge = TEXT_DIM,
                    .title = TEXT_LIGHT, .item = TEXT_DIM, .selected = HILITE };
    return t;
}

// --------------------------------------------------------------------------
// Scenes
// --------------------------------------------------------------------------
typedef struct {
    const Game* g;
    const char* panel_title;
} TableCtx;

static void draw_table_scene(void* vctx, int view_w, int view_h) {
    TableCtx* ctx = (TableCtx*)vctx;
    const Game* g = ctx->g;
    Layout l = layout_for_hands(view_w, view_h, g->nhands);

    gfx_clear(FELT);
    draw_title_bar(&l);
    draw_status(g, &l);
    draw_dealer(g, &l);
    draw_message(g, &l);
    draw_hands(g, &l);
    if (!ctx->panel_title) draw_buttons(g, &l);

    if (ctx->panel_title) {
        MenuTheme t = menu_theme();
#ifdef OJ_TOUCH
        const char* sub = "Tap to continue";
#else
        const char* sub = "Press any key";
#endif
        menu_draw_notice(&t, view_w, view_h, ctx->panel_title, sub);
    }
}

// --------------------------------------------------------------------------
// Public entry points
// --------------------------------------------------------------------------
void render_frame(const Game* g) {
    TableCtx ctx = { g, NULL };
    present(draw_table_scene, &ctx);
}

void render_notice(const Game* g, const char* title) {
    TableCtx ctx = { g, title };
    present(draw_table_scene, &ctx);
}

void render_menu(const char* title, const char* const* labels, int count,
                 int selected, int gap_before) {
    MenuTheme t = menu_theme();
    menu_show(&t, title, labels, count, selected, gap_before);
}

Button render_button_at(const Game* g, int x, int y) {
    Layout l = layout_for(GetScreenWidth(), GetScreenHeight());
    return layout_button_at(&l, g, x, y);
}

int render_chrome_bottom(void) {
    Layout l = layout_for(GetScreenWidth(), GetScreenHeight());
    return l.status_y + l.status_h;
}

int render_card_size(void) {
    return layout_for(GetScreenWidth(), GetScreenHeight()).card_w;
}

// --------------------------------------------------------------------------
// Lifecycle
// --------------------------------------------------------------------------
void render_init(void) {
    window_init(GAME_NAME);
    present_init();
}

void render_cleanup(void) {
    present_cleanup();
    window_close();
}
