#include "game.h"
#include "layout.h"
#include "render.h"
#include "input.h"
#include "sound.h"
#include "recorder.h"
#include "app.h"
#include "tick.h"
#include "menu.h"
#include "window.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifdef PLATFORM_WEB
#include <emscripten/emscripten.h>
#endif

typedef enum { STATE_MENU, STATE_OPTIONS, STATE_PLAYING, STATE_NOTICE } AppState;

typedef enum {
    ACT_RESUME, ACT_NEW, ACT_OPTIONS, ACT_SOUND, ACT_RECORD, ACT_EXIT,
} MenuAction;

#define MAX_MENU_ITEMS 6

static void play_event_sounds(unsigned ev) {
    if (ev & EV_BLACKJACK)  sound_play(SFX_BLACKJACK);
    else if (ev & EV_WIN)   sound_play(SFX_WIN);
    else if (ev & EV_LOSE)  sound_play(SFX_LOSE);
    else if (ev & EV_PUSH)  sound_play(SFX_PUSH);
    if (ev & EV_BUST) sound_play(SFX_BUST);
    if (ev & EV_FLIP) sound_play(SFX_FLIP);
    if (ev & EV_DEAL) sound_play(SFX_DEAL);
    if (ev & EV_CHIP) sound_play(SFX_CHIP);
}

// Fill labels[]/actions[] with the current menu. Returns the item count and
// sets *gap_before to the index that should have a blank line above it -- Exit,
// which is set apart from the rest -- or -1 when this build has no Exit item at
// all (mobile and web, where the OS or the browser tab owns the lifecycle).
static int build_menu(bool resumable, const char** labels, MenuAction* actions,
                      int* gap_before) {
    int n = 0;
    *gap_before = -1;
    if (resumable) { labels[n] = "Resume Game";                     actions[n++] = ACT_RESUME; }
    labels[n] = "New Game";                                         actions[n++] = ACT_NEW;
    labels[n] = "Options";                                          actions[n++] = ACT_OPTIONS;
    labels[n] = sound_is_enabled() ? "Sound: On" : "Sound: Off";    actions[n++] = ACT_SOUND;
#ifndef OJ_TOUCH
    // The mp4 recorder is a desktop-only feature (stubbed out on mobile/web), so
    // the toggle would do nothing there -- omit it.
    labels[n] = recorder_active()  ? "Record: On" : "Record: Off";  actions[n++] = ACT_RECORD;
#endif
#if defined(PLATFORM_WEB)
    // A browser tab can't be closed from code, so no Exit on web.
#elif !defined(PLATFORM_IOS) && !defined(PLATFORM_ANDROID)
    // Mobile apps don't self-terminate (the OS owns the lifecycle: home gesture /
    // back button on Android, Apple guidelines on iOS), so no Exit on either.
    *gap_before = n;
    labels[n] = "Exit";                                             actions[n++] = ACT_EXIT;
#endif
    return n;
}

// The Options screen: the house rules, plus Back. A change applies from the
// next hand, never to one already dealt.
#define OPT_ITEMS 4
enum { OPT_DECKS, OPT_SOFT17, OPT_SURRENDER, OPT_BACK };

static int build_options(Rules r, const char** labels) {
    labels[OPT_DECKS]     = (r.decks == 1) ? "Decks: 1" : "Decks: 6";
    labels[OPT_SOFT17]    = r.h17 ? "Soft 17: Dealer Hits" : "Soft 17: Dealer Stands";
    labels[OPT_SURRENDER] = r.surrender ? "Surrender: On" : "Surrender: Off";
    labels[OPT_BACK]      = "Back";
    return OPT_ITEMS;
}

static void cycle_option(Rules* r, int item) {
    if (item == OPT_DECKS)     r->decks = (r->decks == 1) ? MAX_DECKS : 1;
    if (item == OPT_SOFT17)    r->h17 = !r->h17;
    if (item == OPT_SURRENDER) r->surrender = !r->surrender;
}

// App state carried across frames. Kept in one struct so the web and iOS builds
// can drive the loop from a per-frame callback (neither can block).
typedef struct {
    Game* game;
    AppState state;
    int  selected;     // menu / options cursor
    Rules rules;       // the Options screen's choices
    bool quit;
    SimClock clock;    // fixed-timestep accumulator (only advanced while playing)
    double prev_time;  // GetTime() at the previous frame; 0 before the first
} AppCtx;

static void app_ctx_init(AppCtx* c) {
    c->game = NULL;
    c->state = STATE_MENU;
    c->selected = 0;
    c->rules = rules_default();
    c->quit = false;
    sim_clock_reset(&c->clock);
    c->prev_time = 0.0;
}

static void start_new_game(AppCtx* c) {
    if (c->game) game_destroy(c->game);
    // One statement per rand() call: C leaves the order of two calls in one
    // expression unspecified, and a fixed order keeps a given clock reproducible.
    uint64_t seed = (uint64_t)time(NULL);
    seed ^= (uint64_t)rand() << 16;
    seed ^= (uint64_t)rand();
    c->game = game_create(seed, c->rules);
    c->state = STATE_PLAYING;
}

// A menu row picked by the pointer: a completed tap, or a mouse click.
static bool menu_pointer(const Input* in, Vector2* p) {
    if (in->touch_tap) { *p = (Vector2){in->tap_x, in->tap_y}; return true; }
    if (in->left_pressed) {
        *p = (Vector2){(float)in->mouse_x, (float)in->mouse_y};
        return true;
    }
    return false;
}

static void apply(Game* g, Button b) {
    switch (b) {
    case BTN_HIT:       game_hit(g); break;
    case BTN_STAND:     game_stand(g); break;
    case BTN_DOUBLE:    game_double(g); break;
    case BTN_SPLIT:     game_split(g); break;
    case BTN_SURRENDER: game_surrender(g); break;
    case BTN_INSURE:    game_insurance(g, true); break;
    case BTN_DECLINE:   game_insurance(g, false); break;
    case BTN_BET_DOWN:  game_bet_change(g, -1); break;
    case BTN_BET_UP:    game_bet_change(g, +1); break;
    case BTN_DEAL:      game_deal(g); break;
    case BTN_NEXT:      game_next(g); break;
    default: break;
    }
}

// The keyboard's action for the current phase. Only one action is taken per
// frame, whether it comes from here or from a button.
static Button key_action(const Game* g, const Input* in) {
    switch (g->phase) {
    case PHASE_BET:
        if (in->select_pressed) return BTN_DEAL;
        if (in->bet_up)         return BTN_BET_UP;
        if (in->bet_down)       return BTN_BET_DOWN;
        break;
    case PHASE_INSURANCE:
        if (in->key_insure)     return BTN_INSURE;
        if (in->key_decline)    return BTN_DECLINE;
        break;
    case PHASE_PLAYER:
        if (in->key_hit)        return BTN_HIT;
        if (in->key_stand)      return BTN_STAND;
        if (in->key_double)     return BTN_DOUBLE;
        if (in->key_split)      return BTN_SPLIT;
        if (in->key_surrender)  return BTN_SURRENDER;
        break;
    case PHASE_RESULT:
        if (in->select_pressed) return BTN_NEXT;
        break;
    }
    return BTN_NONE;
}

// One iteration of the game loop. `arg` is an AppCtx* (void* to match the
// emscripten_set_main_loop callback signature).
static void frame_step(void* arg) {
    AppCtx* c = (AppCtx*)arg;

    double now = GetTime();
    double dt = (c->prev_time > 0.0) ? now - c->prev_time : SIM_DT;
    c->prev_time = now;
    if (c->state != STATE_PLAYING) sim_clock_reset(&c->clock);

    // Sampled every frame, not only while playing, so a stale "was focused"
    // cannot survive a menu visit and fire on the first frame of the next game.
    bool focus_lost = window_focus_lost();

    Input in = input_poll();
    if (in.fullscreen_toggle) window_toggle_fullscreen();

    bool resumable = (c->game != NULL);
    const char* labels[MAX_MENU_ITEMS];
    MenuAction actions[MAX_MENU_ITEMS];
    int gap_before = -1;
    int menu_count = build_menu(resumable, labels, actions, &gap_before);

    switch (c->state) {
    case STATE_MENU: {
        if (c->selected >= menu_count) c->selected = 0;
        if (in.escape_pressed) {
            if (resumable) { c->state = STATE_PLAYING; break; }
            c->quit = true; return;
        }
        if (in.menu_up)   { c->selected = (c->selected + menu_count - 1) % menu_count; sound_play(SFX_MENU_MOVE); }
        if (in.menu_down) { c->selected = (c->selected + 1) % menu_count;              sound_play(SFX_MENU_MOVE); }
        bool do_select = in.select_pressed;
        Vector2 p;
        if (menu_pointer(&in, &p)) {
            int hit = menu_hit_test(p);
            if (hit >= 0 && hit < menu_count) { c->selected = hit; do_select = true; }
        }
        if (do_select) {
            sound_play(SFX_MENU_SELECT);
            switch (actions[c->selected]) {
            case ACT_RESUME: c->state = STATE_PLAYING; break;
            case ACT_NEW:
                start_new_game(c);
                if (recorder_active()) { recorder_stop(); recorder_start(NULL); }
                break;
            case ACT_OPTIONS: c->state = STATE_OPTIONS; c->selected = 0; break;
            case ACT_SOUND:   sound_toggle(); sound_play(SFX_MENU_SELECT); break;
            case ACT_RECORD:  recorder_toggle(); break;
            case ACT_EXIT:    c->quit = true; return;
            }
        }
        break;
    }

    case STATE_OPTIONS: {
        const char* opt_labels[OPT_ITEMS];
        int opt_count = build_options(c->rules, opt_labels);
        if (c->selected >= opt_count) c->selected = 0;
        if (in.escape_pressed) { c->state = STATE_MENU; c->selected = 0; break; }
        if (in.menu_up)   { c->selected = (c->selected + opt_count - 1) % opt_count; sound_play(SFX_MENU_MOVE); }
        if (in.menu_down) { c->selected = (c->selected + 1) % opt_count;             sound_play(SFX_MENU_MOVE); }
        bool cycle = in.menu_left || in.menu_right;
        bool do_select = in.select_pressed;
        Vector2 p;
        if (menu_pointer(&in, &p)) {
            int hit = menu_hit_test(p);
            if (hit >= 0 && hit < opt_count) { c->selected = hit; do_select = true; }
        }
        if (do_select && c->selected == OPT_BACK) {
            c->state = STATE_MENU;
            c->selected = 0;
            sound_play(SFX_MENU_SELECT);
        } else if (cycle || do_select) {
            // Every option has two values, so left and right both toggle it.
            cycle_option(&c->rules, c->selected);
            if (c->game) game_set_rules(c->game, c->rules);
            sound_play(SFX_MENU_SELECT);
        }
        break;
    }

    case STATE_PLAYING: {
        Game* g = c->game;
        if (!g) { c->state = STATE_MENU; break; }
        // Losing focus (app backgrounded, tab hidden, window deactivated)
        // returns to the menu; the game stays resumable.
        if (focus_lost) { c->state = STATE_MENU; c->selected = 0; break; }
        if (in.escape_pressed) { c->state = STATE_MENU; c->selected = 0; break; }

        game_step_begin(g);
        int steps = sim_clock_advance(&c->clock, dt);
        for (int s = 0; s < steps; s++) game_update(g);

        // Input waits until the dealt cards have landed, so the player acts on
        // what they can see.
        if (!game_busy(g)) {
            Button act = BTN_NONE;
            if (in.touch_tap || in.left_pressed) {
                int x = in.touch_tap ? (int)in.tap_x : in.mouse_x;
                int y = in.touch_tap ? (int)in.tap_y : in.mouse_y;
                act = render_button_at(g, x, y);
                if (act == BTN_NONE && in.touch_tap && y < render_chrome_bottom()) {
                    // On touch, a tap on the wordmark bar or the status line
                    // opens the menu: a button the player can see, where the
                    // two-finger tap is not. Desktop has Escape, so a stray
                    // click up there does nothing.
                    c->state = STATE_MENU;
                    c->selected = 0;
                    sound_play(SFX_MENU_SELECT);
                    break;
                }
            }
            if (act == BTN_NONE) act = key_action(g, &in);
            apply(g, act);
            if (g->refilled) { g->refilled = false; c->state = STATE_NOTICE; }
        }

        play_event_sounds(g->events);
        break;
    }

    case STATE_NOTICE:
        if (in.escape_pressed || (in.any_pressed && !in.fullscreen_toggle))
            c->state = STATE_PLAYING;
        break;
    }

    // Render for the current state.
    if (c->state == STATE_MENU) {
        render_menu("OPENJACK", labels, menu_count, c->selected, gap_before);
    } else if (c->state == STATE_OPTIONS) {
        const char* opt_labels[OPT_ITEMS];
        int opt_count = build_options(c->rules, opt_labels);
        render_menu("OPTIONS", opt_labels, opt_count, c->selected, OPT_BACK);
    } else if (c->state == STATE_NOTICE) {
        render_notice(c->game, "OUT OF CHIPS");
    } else {
        render_frame(c->game);
    }
}

#if defined(PLATFORM_IOS)

// iOS: UIKit provides main() and the run loop, so the main() below is compiled
// out. The app shell (ios/ios_main.mm) sets up the Metal layer, calls
// app_init() once, then app_frame() from a CADisplayLink each frame.
static AppCtx ios_ctx;

void app_init(void) {
    srand((unsigned int)time(NULL));
    render_init();   // no-op on iOS (UIKit owns the window)
    sound_init();
    app_ctx_init(&ios_ctx);
}

void app_frame(void) { frame_step(&ios_ctx); }

#else

int main(int argc, char** argv) {
    srand((unsigned int)time(NULL));

    // CLI: --record [path] starts recording immediately (auto-named if no path).
    bool cli_record = false;
    const char* cli_record_path = NULL;
    for (int i = 1; i < argc; i++)
        if (strcmp(argv[i], "--record") == 0) {
            cli_record = true;
            if (i + 1 < argc && argv[i + 1][0] != '-') cli_record_path = argv[++i];
        }

    render_init();
    sound_init();
    if (cli_record) recorder_start(cli_record_path);

    // Static so the pointer handed to emscripten stays valid after main()'s
    // stack is unwound on the web build.
    static AppCtx ctx;
    app_ctx_init(&ctx);

#ifdef PLATFORM_WEB
    emscripten_set_main_loop_arg(frame_step, &ctx, 0, 1);
#else
    while (!window_should_close() && !ctx.quit) {
        frame_step(&ctx);
    }
    recorder_stop();
    if (ctx.game) game_destroy(ctx.game);
    sound_shutdown();
    render_cleanup();
#endif
    return 0;
}

#endif // PLATFORM_IOS
