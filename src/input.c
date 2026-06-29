#include "input.h"
#include <raylib.h>

Input input_poll(void) {
    Input in = {0};

    Vector2 mp = GetMousePosition();
    in.mouse_x = (int)mp.x;
    in.mouse_y = (int)mp.y;
    in.left_pressed = IsMouseButtonPressed(MOUSE_BUTTON_LEFT);

    in.hit   = IsKeyPressed(KEY_H);
    in.stand = IsKeyPressed(KEY_S);
    in.dbl   = IsKeyPressed(KEY_D);

    bool alt = IsKeyDown(KEY_LEFT_ALT) || IsKeyDown(KEY_RIGHT_ALT);
    in.fullscreen_toggle = alt && IsKeyPressed(KEY_ENTER);
    in.confirm = (IsKeyPressed(KEY_ENTER) && !in.fullscreen_toggle) || IsKeyPressed(KEY_SPACE);

    in.bet_up   = IsKeyPressed(KEY_RIGHT) || IsKeyPressed(KEY_EQUAL) || IsKeyPressed(KEY_KP_ADD);
    in.bet_down = IsKeyPressed(KEY_LEFT)  || IsKeyPressed(KEY_MINUS) || IsKeyPressed(KEY_KP_SUBTRACT);

    in.escape_pressed = IsKeyPressed(KEY_ESCAPE);

    in.menu_up   = IsKeyPressed(KEY_UP)   || IsKeyPressed(KEY_W);
    in.menu_down = IsKeyPressed(KEY_DOWN) || IsKeyPressed(KEY_S);
    in.select_pressed = in.confirm;

    in.any_pressed = in.left_pressed || in.escape_pressed || in.confirm
                  || in.menu_up || in.menu_down;

    return in;
}
