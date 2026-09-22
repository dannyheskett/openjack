#ifndef OPENJACK_SOUND_H
#define OPENJACK_SOUND_H

#include <stdbool.h>

// Procedural sound effects, synthesized at startup from square / swept
// waveforms (no audio files). Sound is disabled by default.

typedef enum {
    SFX_DEAL = 0,    // a card lands
    SFX_FLIP,        // the dealer's hole card turns
    SFX_CHIP,        // the bet changes, or chips go down
    SFX_WIN,
    SFX_LOSE,
    SFX_PUSH,
    SFX_BLACKJACK,
    SFX_BUST,
    SFX_MENU_MOVE,   // menu cursor moved
    SFX_MENU_SELECT, // menu item chosen
    SFX_COUNT,
} SfxId;

void sound_init(void);       // open the audio device and synthesize all effects
void sound_shutdown(void);   // free effects and close the audio device
bool sound_is_enabled(void);
void sound_toggle(void);
void sound_play(SfxId id);   // no-op when sound is disabled

#endif
