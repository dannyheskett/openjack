#include "game.h"
#include "render.h"
#include "input.h"
#include "sound.h"
#include "recorder.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

typedef enum { STATE_MENU, STATE_PLAYING } AppState;

typedef enum { ACT_RESUME, ACT_NEW, ACT_SOUND, ACT_RECORD, ACT_EXIT } MenuAction;

#define MAX_MENU_ITEMS 8

static void play_event_sounds(unsigned ev) {
    if (ev & EV_BLACKJACK) sound_play(SFX_BLACKJACK);
    else if (ev & EV_WIN)  sound_play(SFX_WIN);
    else if (ev & EV_BUST) sound_play(SFX_BUST);
    else if (ev & EV_LOSE) sound_play(SFX_LOSE);
    else if (ev & EV_PUSH) sound_play(SFX_PUSH);
    if (ev & EV_DEAL) sound_play(SFX_DEAL);
    if (ev & EV_HIT)  sound_play(SFX_HIT);
    if (ev & EV_CHIP) sound_play(SFX_CHIP);
}

static int build_menu(bool resumable, const char** labels, MenuAction* actions) {
    int n = 0;
    if (resumable) { labels[n] = "Resume"; actions[n++] = ACT_RESUME; }
    labels[n] = "New Game";                                       actions[n++] = ACT_NEW;
    labels[n] = sound_is_enabled() ? "Sound: On" : "Sound: Off";  actions[n++] = ACT_SOUND;
    labels[n] = recorder_active()  ? "Record: On" : "Record: Off"; actions[n++] = ACT_RECORD;
    labels[n] = "Exit";                                           actions[n++] = ACT_EXIT;
    return n;
}

// Apply a button press (mouse or keyboard) to the game.
static void do_action(Game* g, Button b) {
    switch (b) {
        case BTN_HIT:      game_hit(g); break;
        case BTN_STAND:    game_stand(g); break;
        case BTN_DOUBLE:   game_double(g); break;
        case BTN_BET_DOWN: game_bet_change(g, -BET_STEP); break;
        case BTN_BET_UP:   game_bet_change(g, +BET_STEP); break;
        case BTN_DEAL:
            if (g->phase == PHASE_RESULT) game_next(g);
            else                          game_deal(g);
            break;
        default: break;
    }
}

int main(int argc, char** argv) {
    srand((unsigned int)time(NULL));

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

    Game* game = NULL;
    AppState state = STATE_MENU;
    int selected = 0;

    while (!render_window_should_close()) {
        Input in = input_poll();
        if (in.fullscreen_toggle) render_toggle_fullscreen();

        bool resumable = (game != NULL);
        const char* labels[MAX_MENU_ITEMS];
        MenuAction actions[MAX_MENU_ITEMS];
        int menu_count = build_menu(resumable, labels, actions);
        if (selected >= menu_count) selected = 0;

        switch (state) {
        case STATE_MENU:
            if (in.escape_pressed) {
                if (resumable) { state = STATE_PLAYING; break; }
                goto quit;
            }
            if (in.menu_up)   { selected = (selected + menu_count - 1) % menu_count; sound_play(SFX_MENU_MOVE); }
            if (in.menu_down) { selected = (selected + 1) % menu_count;             sound_play(SFX_MENU_MOVE); }
            if (in.select_pressed) {
                sound_play(SFX_MENU_SELECT);
                switch (actions[selected]) {
                case ACT_RESUME: state = STATE_PLAYING; break;
                case ACT_NEW:
                    if (game) game_destroy(game);
                    game = game_create();
                    if (recorder_active()) { recorder_stop(); recorder_start(NULL); }
                    state = STATE_PLAYING;
                    break;
                case ACT_SOUND:  sound_toggle(); sound_play(SFX_MENU_SELECT); break;
                case ACT_RECORD: recorder_toggle(); break;
                case ACT_EXIT:   goto quit;
                }
            }
            break;

        case STATE_PLAYING:
            if (!game) { state = STATE_MENU; break; }
            if (in.escape_pressed) { state = STATE_MENU; selected = 0; break; }

            game_frame_begin(game);

            // Mouse on buttons.
            if (in.left_pressed) {
                Button b = render_button_at(game, in.mouse_x, in.mouse_y);
                if (b != BTN_NONE) do_action(game, b);
            }
            // Keyboard, by phase.
            if (game->phase == PHASE_BET) {
                if (in.bet_up)   game_bet_change(game, +BET_STEP);
                if (in.bet_down) game_bet_change(game, -BET_STEP);
                if (in.confirm)  game_deal(game);
            } else if (game->phase == PHASE_PLAYER) {
                if (in.hit)   game_hit(game);
                if (in.stand) game_stand(game);
                if (in.dbl)   game_double(game);
            } else if (game->phase == PHASE_RESULT) {
                if (in.confirm) game_next(game);
            }

            play_event_sounds(game->events);
            break;
        }

        // Render once per frame, after the update.
        if (state == STATE_MENU)
            render_menu("OPENJACK", labels, menu_count, selected, menu_count - 1);
        else
            render_frame(game);
    }

quit:
    recorder_stop();
    if (game) game_destroy(game);
    sound_shutdown();
    render_cleanup();
    return 0;
}
