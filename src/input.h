#ifndef OPENJACK_INPUT_H
#define OPENJACK_INPUT_H

#include <stdbool.h>

typedef struct {
    int  mouse_x, mouse_y;
    bool left_pressed;

    bool hit, stand, dbl;     // H / S / D
    bool confirm;             // Enter / Space (deal in BET, next in RESULT)
    bool bet_up, bet_down;    // arrows / +/-

    bool escape_pressed;
    bool fullscreen_toggle;

    bool menu_up, menu_down, select_pressed, any_pressed;
} Input;

Input input_poll(void);

#endif
