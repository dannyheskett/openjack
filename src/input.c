#include "input.h"
#include "render.h"
#include "platform.h"
#if !defined(PLATFORM_IOS)
#include <raylib.h>  // keyboard/mouse; iOS is touch-only (queries come from plat_ios)
#endif

// input_poll() composes up to two sources into one Input:
//   - mouse + keyboard: desktop native builds and the web build (PC browsers)
//   - touch:            Android, iOS, and the web build (mobile browsers)
// The web build runs both, so a phone uses taps while a desktop browser uses
// the mouse — same binary. Android and iOS run only touch; desktop native runs
// only mouse + keyboard.

#if !defined(PLATFORM_ANDROID) && !defined(PLATFORM_IOS)
// Mouse + keyboard source: sets the base field values.
static void poll_keyboard(Input* in) {
    Vector2 mp = GetMousePosition();
    in->mouse_x = (int)mp.x;
    in->mouse_y = (int)mp.y;
    in->left_pressed = IsMouseButtonPressed(MOUSE_BUTTON_LEFT);

    in->escape_pressed = IsKeyPressed(KEY_ESCAPE);

    bool alt = IsKeyDown(KEY_LEFT_ALT) || IsKeyDown(KEY_RIGHT_ALT);
    in->fullscreen_toggle = alt && IsKeyPressed(KEY_ENTER);

    in->menu_up   = IsKeyPressed(KEY_UP)   || IsKeyPressed(KEY_W);
    in->menu_down = IsKeyPressed(KEY_DOWN) || IsKeyPressed(KEY_S);
    in->menu_left  = IsKeyPressed(KEY_LEFT)  || IsKeyPressed(KEY_A);
    in->menu_right = IsKeyPressed(KEY_RIGHT) || IsKeyPressed(KEY_D);
    in->select_pressed = (IsKeyPressed(KEY_ENTER) && !in->fullscreen_toggle)
                       || IsKeyPressed(KEY_SPACE);

    // Table keys. Enter / Space (select_pressed) deals and moves to the next
    // hand; the arrows and + / - change the bet.
    in->key_hit       = IsKeyPressed(KEY_H);
    in->key_stand     = IsKeyPressed(KEY_S);
    in->key_double    = IsKeyPressed(KEY_D);
    in->key_split     = IsKeyPressed(KEY_P);
    in->key_surrender = IsKeyPressed(KEY_R);
    in->key_insure    = IsKeyPressed(KEY_I) || IsKeyPressed(KEY_Y);
    in->key_decline   = IsKeyPressed(KEY_N);
    in->bet_up   = IsKeyPressed(KEY_RIGHT) || IsKeyPressed(KEY_UP)
                || IsKeyPressed(KEY_EQUAL) || IsKeyPressed(KEY_KP_ADD);
    in->bet_down = IsKeyPressed(KEY_LEFT)  || IsKeyPressed(KEY_DOWN)
                || IsKeyPressed(KEY_MINUS) || IsKeyPressed(KEY_KP_SUBTRACT);

    in->any_pressed = in->left_pressed || in->escape_pressed || in->menu_up
                   || in->menu_down || in->select_pressed;
}
#endif // !PLATFORM_ANDROID && !PLATFORM_IOS

#ifdef OJ_TOUCH
// Tap recognizer state that persists across frames for the current touch
// sequence (first finger down to last finger up), kept in one module-owned
// value so the hidden state is explicit and resettable.
typedef struct {
    Vector2 last_pos;  // last single-finger position (source of tap coords)
    bool    active;    // a touch sequence is in progress
    Vector2 origin;    // where the sequence started (px)
    double  t0;        // sequence start time (s)
    int     max_np;    // most simultaneous fingers seen during the sequence
    bool    moved;     // travelled past the tap slop: a drag or swipe, not a tap
} TouchState;

static TouchState s_touch;

// Longest contact that still counts as a tap. Blackjack is unhurried, so this
// is more forgiving than an action game's, but a finger resting on the screen
// is not a tap.
#define TAP_MAX_S       0.5
#define TWO_FINGER_MAX_S 0.5

// Touch source: one-finger taps (buttons and menu), two-finger tap = menu, and
// swipe menus (vertical to move, horizontal to cycle an Options value). Only
// ever sets fields true, so it composes over the keyboard source on web without
// clobbering it. Native-resolution rendering means touch coords map 1:1 to the
// on-screen geometry (buttons, menu rows).
static void poll_touch(Input* in) {
    // Active pointers: touch points, or the mouse while its button is held.
    // Desktop browsers report no touch points for a mouse, so without this a
    // click would never register in the portrait layout.
    int n = GetTouchPointCount();
    Vector2 pts[8];
    int np = 0;
    for (int i = 0; i < n && np < 8; i++) pts[np++] = GetTouchPosition(i);
    // A mouse is handled by poll_keyboard above (it fills mouse_x/left_pressed
    // directly), so the touch recognizer only ever sees real touch points.

    double now = GetTime();

    // Movement tolerance: a third of a card width, so a tap that rolls a little
    // still lands, but a deliberate swipe is never read as a tap.
    float slop = (float)render_card_size() / 3.0f;
    if (slop < 12.0f) slop = 12.0f;

    if (np > 0) {
        Vector2 p = pts[0];
        if (!s_touch.active) {
            s_touch.active = true;
            s_touch.origin = p;
            s_touch.last_pos = p;
            s_touch.t0 = now;
            s_touch.max_np = 0;
            s_touch.moved = false;
        }
        if (np > s_touch.max_np) s_touch.max_np = np;
        // pts[0] can jump when a second finger lands or lifts, so position is
        // only tracked while the sequence is still single-finger.
        if (s_touch.max_np < 2) {
            float dx = p.x - s_touch.origin.x, dy = p.y - s_touch.origin.y;
            if (dx * dx + dy * dy > slop * slop) s_touch.moved = true;
            s_touch.last_pos = p;
        }
    } else if (s_touch.active) {
        // Touch ended: decide the discrete action on RELEASE. (raylib's
        // GESTURE_TAP fires on touch-DOWN, before a swipe can be ruled out.)
        double dur = now - s_touch.t0;
        if (s_touch.max_np >= 2) {
            // Two-finger tap: menu (the game stays resumable). Long
            // multi-finger contact is ignored.
            if (dur < TWO_FINGER_MAX_S) {
                in->escape_pressed = true;
                in->any_pressed = true;
            }
        } else if (!s_touch.moved && dur < TAP_MAX_S) {
            in->touch_tap = true;
            in->tap_x = s_touch.last_pos.x;
            in->tap_y = s_touch.last_pos.y;
            in->any_pressed = true;
        }
        s_touch.active = false;
    }

    // Swipe gestures drive menu navigation (taps are decided on release, above):
    // up / down move the highlight, left / right cycle an Options value.
    int g = GetGestureDetected();
    if (g == GESTURE_SWIPE_UP)    in->menu_up    = true;
    if (g == GESTURE_SWIPE_DOWN)  in->menu_down  = true;
    if (g == GESTURE_SWIPE_LEFT)  in->menu_left  = true;
    if (g == GESTURE_SWIPE_RIGHT) in->menu_right = true;

#if !defined(PLATFORM_IOS)
    // Android hardware/gesture Back button (KEY_BACK); harmless no-op on web.
    // iOS has no key events (and no hardware back button).
    if (IsKeyPressed(KEY_BACK)) {
        in->escape_pressed = true;
        in->any_pressed    = true;
    }
#endif
}
#endif // OJ_TOUCH

Input input_poll(void) {
    Input in = {0};
#if !defined(PLATFORM_ANDROID) && !defined(PLATFORM_IOS)
    poll_keyboard(&in);   // desktop native + web (PC browsers)
#endif
#ifdef OJ_TOUCH
    poll_touch(&in);      // Android + iOS + web (mobile browsers)
#endif
    return in;
}
