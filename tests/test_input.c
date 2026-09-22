// Unit tests for the touch tap recognizer in input.c. Like test_game.c, the
// code under test runs in isolation — no raylib, no window — by compiling with
// -DPLATFORM_IOS, the raylib-free configuration input.c already supports:
// oj_types.h supplies the types and declares the touch/clock queries, and this
// file provides scripted fakes of them. The recognizer under test is the same
// C compiled into every touch platform (Android / web / iOS); only the poll
// surface behind it differs.
//
// Frames are driven at exactly 60 Hz: the recognizer's thresholds are in
// seconds (tap and two-finger durations) and card widths (the tap
// movement slop), so the fake clock advances 1/60 s per polled frame.
//
// Built and run by `make test`. A non-zero exit means a failure.

#include "input.c"

#include <stdio.h>
#include <string.h>

static int failures = 0;

#define CHECK(cond)                                                          \
    do {                                                                     \
        if (!(cond)) {                                                       \
            printf("  FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);         \
            failures++;                                                      \
        }                                                                    \
    } while (0)

// --- Scripted fake of the poll surface --------------------------------------
// input.c reads fingers, swipe gestures, layout, and the clock through these.
// Tests set the state, then call one of the frame helpers to advance 1/60 s
// and poll.

static double  fake_now;       // GetTime()
static int     fake_np;        // GetTouchPointCount()
static Vector2 fake_pts[8];    // GetTouchPosition(i)
static int     fake_gesture;   // GetGestureDetected() (swipes; 0 = none)
static int     fake_card;      // render_card_size() (px per card)

double  GetTime(void)                { return fake_now; }
int     GetTouchPointCount(void)     { return fake_np; }
Vector2 GetTouchPosition(int i)      { return fake_pts[i]; }
int     GetGestureDetected(void)     { return fake_gesture; }
int     render_card_size(void)       { return fake_card; }

#define DT (1.0 / 60.0)
// 72 px cards => tap slop of 24 px.
#define CARD 72

// Fresh recognizer + fake surface for each test.
static void reset(void) {
    memset(&s_touch, 0, sizeof s_touch);
    fake_now      = 100.0;
    fake_np       = 0;
    fake_gesture  = 0;
    fake_card   = CARD;
}

// Advance one 60 Hz frame and poll with the fingers currently set.
static Input frame(void) {
    fake_now += DT;
    return input_poll();
}

// One frame with a single finger at (x, y).
static Input frame_touch(float x, float y) {
    fake_np = 1;
    fake_pts[0] = (Vector2){x, y};
    return frame();
}

// One frame with two fingers down.
static Input frame_touch2(float x0, float y0, float x1, float y1) {
    fake_np = 2;
    fake_pts[0] = (Vector2){x0, y0};
    fake_pts[1] = (Vector2){x1, y1};
    return frame();
}

// One frame with every finger lifted (the release the recognizer decides on).
static Input frame_release(void) {
    fake_np = 0;
    return frame();
}

// --- Tap, decided on release --------------------------------------------------
static void test_tap_fires_on_release(void) {
    reset();

    // While the finger is down nothing fires — a tap must not trigger on
    // touch-DOWN or a swipe would begin by selecting whatever it started on.
    Input in = frame_touch(200, 400);
    CHECK(!in.touch_tap && !in.any_pressed);
    in = frame_touch(200, 400);
    CHECK(!in.touch_tap);

    // Release after ~50 ms: a tap, at the finger's position.
    in = frame_release();
    CHECK(in.touch_tap);
    CHECK(in.any_pressed);
    CHECK(in.tap_x == 200 && in.tap_y == 400);
    CHECK(!in.escape_pressed && !in.menu_up && !in.menu_down);

    // The release is an edge: the next empty frame is silent.
    in = frame_release();
    CHECK(!in.touch_tap && !in.any_pressed);
}

// --- A resting finger is not a tap ---------------------------------------------
static void test_long_press_is_not_a_tap(void) {
    reset();

    frame_touch(200, 400);
    for (int i = 0; i < 32; i++) frame_touch(200, 400); // 33/60 s > 0.5 s cap
    Input in = frame_release();
    CHECK(!in.touch_tap && !in.any_pressed && !in.escape_pressed);
}

// --- Jitter inside the slop still taps, at the final position -------------------
static void test_tap_survives_jitter(void) {
    reset();

    // 10 px sideways and 5 px down: well inside a third of a square.
    frame_touch(200, 400);
    frame_touch(210, 405);
    Input in = frame_release();
    CHECK(in.touch_tap);
    CHECK(in.tap_x == 210 && in.tap_y == 405);
}

// --- A drag is not a tap ----------------------------------------------------------
static void test_drag_is_not_a_tap(void) {
    reset();

    frame_touch(200, 400);
    frame_touch(200, 440);   // 40 px: past the 24 px slop
    Input in = frame_release();
    CHECK(!in.touch_tap && !in.any_pressed);

    // Wandering back to the start does not turn it back into a tap.
    reset();
    frame_touch(200, 400);
    frame_touch(260, 400);
    frame_touch(200, 400);
    in = frame_release();
    CHECK(!in.touch_tap);
}

// --- Two-finger tap opens the menu ------------------------------------------------
static void test_two_finger_tap_opens_menu(void) {
    reset();

    frame_touch(200, 300);
    Input in = frame_touch2(200, 300, 260, 300);
    CHECK(!in.escape_pressed);              // decided on release, like the tap
    in = frame_release();
    CHECK(in.escape_pressed);
    CHECK(in.any_pressed);
    CHECK(!in.touch_tap);
}

static void test_two_finger_hold_ignored(void) {
    reset();

    frame_touch(200, 300);
    for (int i = 0; i < 32; i++) frame_touch2(200, 300, 260, 300); // > 0.5 s
    Input in = frame_release();
    CHECK(!in.escape_pressed && !in.touch_tap && !in.any_pressed);
}

// Once a second finger has landed, pointer-0 can jump between fingers; the
// recognizer must neither misread the jump as movement nor report a tap at the
// jumped-to position.
static void test_second_finger_freezes_tracking(void) {
    reset();

    frame_touch(200, 400);
    frame_touch2(400, 400, 210, 410); // pts[0] jumps 200 px
    Input in = frame_release();
    CHECK(in.escape_pressed);          // still a two-finger tap
    CHECK(!in.touch_tap);

    // The next single-finger tap starts clean at its own position.
    frame_touch(90, 120);
    in = frame_release();
    CHECK(in.touch_tap && in.tap_x == 90 && in.tap_y == 120);
}

// --- Swipes drive the menus; raylib's touch-down TAP gesture is ignored -------
static void test_swipes_drive_menus(void) {
    reset();

    fake_gesture = GESTURE_SWIPE_UP;
    Input in = frame();
    CHECK(in.menu_up && !in.menu_down);

    fake_gesture = GESTURE_SWIPE_DOWN;
    in = frame();
    CHECK(in.menu_down && !in.menu_up);

    // GESTURE_TAP fires on touch-DOWN, so the recognizer deliberately ignores
    // it (taps are decided on release, above).
    fake_gesture = GESTURE_TAP;
    in = frame();
    CHECK(!in.touch_tap && !in.menu_up && !in.menu_down);

    fake_gesture = GESTURE_SWIPE_LEFT;
    in = frame();
    CHECK(in.menu_left && !in.menu_right && !in.menu_up && !in.menu_down);

    fake_gesture = GESTURE_SWIPE_RIGHT;
    in = frame();
    CHECK(in.menu_right && !in.menu_left && !in.menu_up && !in.menu_down);

    fake_gesture = 0;
    in = frame();
    CHECK(!in.menu_up && !in.menu_down && !in.menu_left && !in.menu_right);
}

// --- The slop scales with the card size -----------------------------------------
static void test_slop_scales_with_card(void) {
    // Big cards (a tablet): 40 px of roll is still a tap at 150 px cards.
    reset();
    fake_card = 150;
    frame_touch(100, 400);
    frame_touch(140, 400);
    Input in = frame_release();
    CHECK(in.touch_tap);

    // A degenerate square size (layout not measured yet) floors the slop at
    // 12 px instead of rejecting every tap.
    reset();
    fake_card = 0;
    frame_touch(100, 400);
    frame_touch(108, 400);
    in = frame_release();
    CHECK(in.touch_tap);

    reset();
    fake_card = 0;
    frame_touch(100, 400);
    frame_touch(115, 400);
    in = frame_release();
    CHECK(!in.touch_tap);
}

int main(void) {
    printf("test_input: touch taps — release-decided tap, drag rejection,\n");
    printf("            two-finger menu, swipe menus, slop scaling\n");
    test_tap_fires_on_release();
    test_long_press_is_not_a_tap();
    test_tap_survives_jitter();
    test_drag_is_not_a_tap();
    test_two_finger_tap_opens_menu();
    test_two_finger_hold_ignored();
    test_second_finger_freezes_tracking();
    test_swipes_drive_menus();
    test_slop_scales_with_card();
    if (failures == 0) {
        printf("OK: all checks passed\n");
        return 0;
    }
    printf("FAILED: %d check(s)\n", failures);
    return 1;
}
