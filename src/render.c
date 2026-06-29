#include "render.h"
#include "recorder.h"
#include <raylib.h>
#include <rlgl.h>
#include <stdio.h>
#include <string.h>
#include <math.h>

#define TITLEBAR_H 44
#define STATUS_H   28
#define BTN_H      42
#define SS         2     // recorder supersampling factor

// --------------------------------------------------------------------------
// Colors
// --------------------------------------------------------------------------
static const Color FELT       = { 12,  92,  52, 255};
static const Color FELT_DARK  = { 10,  76,  44, 255};
static const Color CARD_FACE  = {248, 248, 242, 255};
static const Color CARD_EDGE  = { 40,  40,  40, 255};
static const Color CARD_BACK  = { 36,  72, 156, 255};
static const Color CARD_BACK2 = { 80, 130, 220, 255};
static const Color RED_PIP    = {200,  30,  40, 255};
static const Color BLACK_PIP  = { 20,  20,  24, 255};
static const Color HILITE     = {255, 235, 120, 255};
static const Color MENU_BG    = { 16,  40,  28, 255};
static const Color TEXT_LIGHT = {235, 235, 225, 255};
static const Color TEXT_DIM   = {170, 190, 175, 255};
static const Color BTN_FILL   = { 22, 120,  72, 255};
static const Color BTN_EDGE   = {120, 200, 150, 255};
static const Color BTN_OFF    = { 16,  70,  48, 255};
static const Color WIN_COL    = {120, 240, 150, 255};
static const Color LOSE_COL   = {236, 110, 100, 255};

// --------------------------------------------------------------------------
// Card art (vector-drawn, reused from openklondike)
// --------------------------------------------------------------------------
static const char* RANK_STR[14] = {
    "", "A", "2", "3", "4", "5", "6", "7", "8", "9", "10", "J", "Q", "K"
};

static void fill_fan(Vector2 c, const Vector2* p, int n, Color col) {
    for (int i = 0; i < n; i++) {
        Vector2 a = p[i], b = p[(i + 1) % n];
        DrawTriangle(c, a, b, col);
        DrawTriangle(c, b, a, col);
    }
}

#define PIP_SEG 30

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

static void draw_stem(float cx, float topy, float s, Color col) {
    float nw = s * 0.05f, fw = s * 0.34f, h = s * 0.26f;
    Vector2 tl = {cx - nw, topy},          tr = {cx + nw, topy};
    Vector2 ml = {cx - nw * 1.4f, topy + h * 0.55f};
    Vector2 mr = {cx + nw * 1.4f, topy + h * 0.55f};
    Vector2 bl = {cx - fw, topy + h},      br = {cx + fw, topy + h};
    DrawTriangle(tl, ml, mr, col); DrawTriangle(tl, mr, ml, col);
    DrawTriangle(tl, mr, tr, col); DrawTriangle(tl, tr, mr, col);
    DrawTriangle(ml, bl, br, col); DrawTriangle(ml, br, bl, col);
    DrawTriangle(ml, br, mr, col); DrawTriangle(ml, mr, br, col);
}

static void draw_pip(int cx, int cy, int s, int suit) {
    Color col = (suit == 1 || suit == 2) ? RED_PIP : BLACK_PIP;
    Vector2 pts[PIP_SEG];
    switch (suit) {
        case 1: {
            float hw = s * 0.34f, hh = s * 0.5f;
            Vector2 p[4] = {{cx, cy - hh}, {cx + hw, cy}, {cx, cy + hh}, {cx - hw, cy}};
            fill_fan((Vector2){cx, cy}, p, 4, col);
            break;
        }
        case 2:
            heart_outline(cx, cy, s, 0.86f, false, pts);
            fill_fan((Vector2){cx, cy + s * 0.10f}, pts, PIP_SEG, col);
            break;
        case 3: {
            float body = s * 0.74f, byc = cy - s * 0.10f;
            heart_outline(cx, byc, body, 0.96f, true, pts);
            fill_fan((Vector2){cx, byc - body * 0.10f}, pts, PIP_SEG, col);
            draw_stem(cx, byc + body * 0.30f, s, col);
            break;
        }
        default: {
            float cr = s * 0.255f;
            DrawCircle(cx,                   cy - (int)(s * 0.25f), cr, col);
            DrawCircle(cx - (int)(s*0.245f), cy + (int)(s * 0.11f), cr, col);
            DrawCircle(cx + (int)(s*0.245f), cy + (int)(s * 0.11f), cr, col);
            draw_stem(cx, cy + s * 0.16f, s, col);
            break;
        }
    }
}

static void draw_card_back(int x, int y) {
    DrawRectangleRounded((Rectangle){x, y, CARD_W, CARD_H}, 0.12f, 6, CARD_BACK);
    DrawRectangleRoundedLines((Rectangle){x, y, CARD_W, CARD_H}, 0.12f, 6, CARD_EDGE);
    int m = 8, ix = x + m, iy = y + m, iw = CARD_W - 2 * m, ih = CARD_H - 2 * m;
    DrawRectangleLines(ix, iy, iw, ih, CARD_BACK2);
    for (int gx = ix + 8; gx < ix + iw; gx += 8) DrawLine(gx, iy, gx, iy + ih, CARD_BACK2);
    for (int gy = iy + 8; gy < iy + ih; gy += 8) DrawLine(ix, gy, ix + iw, gy, CARD_BACK2);
}

static void draw_card_face(int x, int y, Card c) {
    DrawRectangleRounded((Rectangle){x, y, CARD_W, CARD_H}, 0.12f, 6, CARD_FACE);
    DrawRectangleRoundedLines((Rectangle){x, y, CARD_W, CARD_H}, 0.12f, 6, CARD_EDGE);
    Color col = card_is_red(c) ? RED_PIP : BLACK_PIP;
    const char* rs = RANK_STR[c.rank];
    int fs = 18;
    DrawText(rs, x + 6, y + 4, fs, col);
    draw_pip(x + 12, y + 4 + fs + 8, 12, c.suit);
    int tw = MeasureText(rs, fs);
    DrawText(rs, x + CARD_W - 6 - tw, y + CARD_H - 4 - fs, fs, col);
    draw_pip(x + CARD_W - 12, y + CARD_H - 4 - fs - 8, 12, c.suit);
    draw_pip(x + CARD_W / 2, y + CARD_H / 2, 34, c.suit);
}

// --------------------------------------------------------------------------
// Table layout
// --------------------------------------------------------------------------
#define DEALER_Y 76
#define PLAYER_Y 260

static void draw_hand(const Card* h, int n, int y, int view_w, int hide_index) {
    if (n <= 0) return;
    float fan = CARD_W * 0.42f;
    float width = CARD_W + (n - 1) * fan;
    float maxw = view_w - 48;
    if (width > maxw && n > 1) { fan = (maxw - CARD_W) / (n - 1); width = CARD_W + (n - 1) * fan; }
    int x0 = (int)((view_w - width) / 2);
    for (int i = 0; i < n; i++) {
        int x = x0 + (int)(i * fan);
        if (i == hide_index) draw_card_back(x, y);
        else                 draw_card_face(x, y, h[i]);
    }
}

// Phase-dependent action buttons. Returns count; fills ids/rects/labels/enabled.
typedef struct { Button id; Rectangle r; const char* label; bool on; } Btn;

static int build_buttons(const Game* g, int view_w, int view_h, Btn* out) {
    int y = view_h - STATUS_H - 14 - BTN_H;
    int n = 0;
    Btn b[4];
    if (g->phase == PHASE_BET) {
        b[n++] = (Btn){ BTN_BET_DOWN, { 0, 0, 48,  BTN_H }, "-",    g->bet > MIN_BET };
        b[n++] = (Btn){ BTN_DEAL,     { 0, 0, 150, BTN_H }, "Deal", g->bankroll >= MIN_BET };
        b[n++] = (Btn){ BTN_BET_UP,   { 0, 0, 48,  BTN_H }, "+",    g->bet < g->bankroll };
    } else if (g->phase == PHASE_PLAYER) {
        b[n++] = (Btn){ BTN_HIT,    { 0, 0, 120, BTN_H }, "Hit",    true };
        b[n++] = (Btn){ BTN_STAND,  { 0, 0, 120, BTN_H }, "Stand",  true };
        b[n++] = (Btn){ BTN_DOUBLE, { 0, 0, 120, BTN_H }, "Double", game_can_double(g) };
    } else if (g->phase == PHASE_RESULT) {
        b[n++] = (Btn){ BTN_DEAL, { 0, 0, 180, BTN_H }, "Next Hand", true };
    }
    int gap = 16, total = 0;
    for (int i = 0; i < n; i++) total += (int)b[i].r.width + (i ? gap : 0);
    int x = (view_w - total) / 2;
    for (int i = 0; i < n; i++) {
        b[i].r.x = x; b[i].r.y = y;
        x += (int)b[i].r.width + gap;
        out[i] = b[i];
    }
    return n;
}

static void draw_button(Btn b) {
    DrawRectangleRounded(b.r, 0.25f, 6, b.on ? BTN_FILL : BTN_OFF);
    DrawRectangleRoundedLines(b.r, 0.25f, 6, b.on ? BTN_EDGE : FELT_DARK);
    int fs = 20, tw = MeasureText(b.label, fs);
    DrawText(b.label, (int)(b.r.x + (b.r.width - tw) / 2),
             (int)(b.r.y + (b.r.height - fs) / 2), fs, b.on ? TEXT_LIGHT : TEXT_DIM);
}

static void result_text(const Game* g, char* buf, int cap, Color* col) {
    int d = g->last_delta < 0 ? -g->last_delta : g->last_delta;
    *col = (g->last_delta > 0) ? WIN_COL : (g->last_delta < 0 ? LOSE_COL : TEXT_LIGHT);
    switch (g->result) {
        case RES_BLACKJACK:   snprintf(buf, cap, "Blackjack!  +$%d", d); break;
        case RES_WIN:         snprintf(buf, cap, "You win  +$%d", d); break;
        case RES_DEALER_BUST: snprintf(buf, cap, "Dealer busts  +$%d", d); break;
        case RES_PUSH:        snprintf(buf, cap, "Push"); break;
        case RES_BUST:        snprintf(buf, cap, "Bust  -$%d", d); break;
        case RES_LOSE:        snprintf(buf, cap, "You lose  -$%d", d); break;
        default:              buf[0] = 0; break;
    }
}

static void draw_table(void* vctx, int view_w, int view_h) {
    const Game* g = (const Game*)vctx;
    ClearBackground(FELT);

    // title
    DrawRectangle(0, 0, view_w, TITLEBAR_H, FELT_DARK);
    DrawLine(0, TITLEBAR_H, view_w, TITLEBAR_H, (Color){40, 40, 40, 255});
    const char* title = "OPENJACK";
    DrawText(title, view_w / 2 - MeasureText(title, 22) / 2, (TITLEBAR_H - 22) / 2, 22, TEXT_LIGHT);

    bool playing = (g->phase != PHASE_BET) || g->pn > 0;
    char buf[64];

    // dealer
    if (playing) {
        int shown = g->reveal ? game_dealer_value(g) : game_dealer_shown(g);
        snprintf(buf, sizeof buf, g->reveal ? "Dealer  %d" : "Dealer  %d +", shown);
        DrawText(buf, 24, TITLEBAR_H + 8, 20, TEXT_DIM);
        draw_hand(g->dealer, g->dn, DEALER_Y, view_w, g->reveal ? -1 : 1);
    } else {
        DrawText("Place your bet", view_w / 2 - MeasureText("Place your bet", 20) / 2,
                 DEALER_Y + CARD_H / 2 - 10, 20, TEXT_DIM);
    }

    // result message (center)
    if (g->phase == PHASE_RESULT) {
        char rt[64]; Color rc;
        result_text(g, rt, sizeof rt, &rc);
        DrawText(rt, view_w / 2 - MeasureText(rt, 28) / 2, PLAYER_Y - 56, 28, rc);
    }

    // player
    if (playing) {
        snprintf(buf, sizeof buf, "You  %d", game_player_value(g));
        DrawText(buf, 24, PLAYER_Y - 28, 20, TEXT_LIGHT);
        draw_hand(g->player, g->pn, PLAYER_Y, view_w, -1);
    }

    // buttons
    Btn btns[4];
    int nb = build_buttons(g, view_w, view_h, btns);
    for (int i = 0; i < nb; i++) draw_button(btns[i]);

    // status bar
    int sy = view_h - STATUS_H + 4;
    DrawRectangle(0, view_h - STATUS_H, view_w, STATUS_H, FELT_DARK);
    snprintf(buf, sizeof buf, "Bankroll $%d", g->bankroll);
    DrawText(buf, 24, sy, 18, TEXT_LIGHT);
    snprintf(buf, sizeof buf, "Bet $%d", g->bet);
    DrawText(buf, view_w / 2 - MeasureText(buf, 18) / 2, sy, 18, HILITE);
    const char* hint = (g->phase == PHASE_PLAYER) ? "H Hit   S Stand   D Double"
                     : (g->phase == PHASE_BET)    ? "Enter Deal"
                     : "Enter Next";
    DrawText(hint, view_w - 24 - MeasureText(hint, 18), sy, 18, TEXT_DIM);
}

// --------------------------------------------------------------------------
// Presentation: window + SSAA-supersampled capture canvas
// --------------------------------------------------------------------------
typedef void (*SceneFn)(void* ctx, int w, int h);

static RenderTexture2D rec_canvas, rec_super;
static bool rec_ready = false;

static void emit(SceneFn fn, void* ctx) {
    BeginDrawing();
    fn(ctx, GetScreenWidth(), GetScreenHeight());
    EndDrawing();

    if (recorder_active() && rec_ready) {
        BeginTextureMode(rec_super);
        rlPushMatrix();
        rlScalef((float)SS, (float)SS, 1.0f);
        fn(ctx, MIN_W, MIN_H);
        rlPopMatrix();
        EndTextureMode();

        BeginTextureMode(rec_canvas);
        Rectangle src = {0, 0, (float)(SS * MIN_W), -(float)(SS * MIN_H)};
        Rectangle dst = {0, 0, (float)MIN_W, (float)MIN_H};
        DrawTexturePro(rec_super.texture, src, dst, (Vector2){0, 0}, 0.0f, WHITE);
        EndTextureMode();
        recorder_capture(&rec_canvas);
    }
}

// --------------------------------------------------------------------------
// Lifecycle
// --------------------------------------------------------------------------
void render_init(void) {
    SetConfigFlags(FLAG_WINDOW_RESIZABLE | FLAG_MSAA_4X_HINT);
    InitWindow(MIN_W, MIN_H, "openjack");
    SetWindowMinSize(MIN_W, MIN_H);
    SetTargetFPS(60);
    SetExitKey(KEY_NULL);
    rec_canvas = LoadRenderTexture(MIN_W, MIN_H);
    rec_super  = LoadRenderTexture(SS * MIN_W, SS * MIN_H);
    SetTextureFilter(rec_super.texture, TEXTURE_FILTER_BILINEAR);
    rec_ready = true;
}

void render_cleanup(void) {
    if (rec_ready) { UnloadRenderTexture(rec_canvas); UnloadRenderTexture(rec_super); }
    CloseWindow();
}

bool render_window_should_close(void) { return WindowShouldClose(); }

void render_toggle_fullscreen(void) {
    if (IsWindowFullscreen()) {
        ToggleFullscreen();
        SetWindowSize(MIN_W, MIN_H);
    } else {
        int m = GetCurrentMonitor();
        SetWindowSize(GetMonitorWidth(m), GetMonitorHeight(m));
        ToggleFullscreen();
    }
}

// --------------------------------------------------------------------------
// Public scenes
// --------------------------------------------------------------------------
void render_frame(const Game* g) { emit(draw_table, (void*)g); }

typedef struct {
    const char* title; const char** labels;
    int count; int selected; int gap_before;
} MenuCtx;

static void draw_menu(void* vctx, int view_w, int view_h) {
    MenuCtx* m = (MenuCtx*)vctx;
    ClearBackground(FELT);
    int cx = view_w / 2, line_h = 32, title_size = 46;
    int extra = (m->gap_before >= 0) ? 1 : 0;
    int panel_w = 400;
    int panel_h = title_size + 40 + (m->count + extra) * line_h + 60;
    int px = cx - panel_w / 2, py = (view_h - panel_h) / 2;
    DrawRectangleRounded((Rectangle){px, py, panel_w, panel_h}, 0.05f, 8, MENU_BG);
    DrawRectangleRoundedLines((Rectangle){px, py, panel_w, panel_h}, 0.05f, 8, TEXT_DIM);
    DrawText(m->title, cx - MeasureText(m->title, title_size) / 2, py + 26, title_size, TEXT_LIGHT);
    int y = py + 26 + title_size + 26;
    for (int i = 0; i < m->count; i++) {
        if (m->gap_before == i) y += line_h;
        int size = 22, lw = MeasureText(m->labels[i], size);
        Color col = (i == m->selected) ? HILITE : TEXT_DIM;
        if (i == m->selected) {
            DrawText(">", cx - lw / 2 - 28, y, size, HILITE);
            DrawText("<", cx + lw / 2 + 14, y, size, HILITE);
        }
        DrawText(m->labels[i], cx - lw / 2, y, size, col);
        y += line_h;
    }
}

void render_menu(const char* title, const char** labels, int count,
                 int selected, int gap_before) {
    MenuCtx ctx = { title, labels, count, selected, gap_before };
    emit(draw_menu, &ctx);
}

// --------------------------------------------------------------------------
// Hit test
// --------------------------------------------------------------------------
Button render_button_at(const Game* g, int mx, int my) {
    Btn btns[4];
    int nb = build_buttons(g, GetScreenWidth(), GetScreenHeight(), btns);
    for (int i = 0; i < nb; i++)
        if (btns[i].on && mx >= btns[i].r.x && mx < btns[i].r.x + btns[i].r.width
            && my >= btns[i].r.y && my < btns[i].r.y + btns[i].r.height)
            return btns[i].id;
    return BTN_NONE;
}
