#ifndef OPENJACK_SOUND_H
#define OPENJACK_SOUND_H

#include <stdbool.h>

typedef enum {
    SFX_DEAL = 0,
    SFX_HIT,
    SFX_WIN,
    SFX_LOSE,
    SFX_PUSH,
    SFX_BLACKJACK,
    SFX_BUST,
    SFX_CHIP,
    SFX_MENU_MOVE,
    SFX_MENU_SELECT,
    SFX_COUNT,
} SfxId;

void sound_init(void);
void sound_shutdown(void);
bool sound_is_enabled(void);
void sound_toggle(void);
void sound_play(SfxId id);

#endif
