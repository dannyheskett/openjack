#ifndef OPENJACK_PLATFORM_H
#define OPENJACK_PLATFORM_H

// The game's name: window title and recording file prefix.
#define GAME_NAME "openjack"

// Recording size (recorder.c): the smallest desktop window. Both are multiples
// of 16 for the H.264 encoder.
#define REC_W 640
#define REC_H 480

// OJ_TOUCH selects the touch frontend: tap-driven buttons and menus, and no
// on-screen chrome that assumes a mouse. It is enabled on Android, iOS, and the
// WebAssembly build (which targets mobile browsers but also accepts a mouse and
// keyboard for desktop browsers). Desktop native builds leave it unset.
//
// raylib defines PLATFORM_ANDROID / PLATFORM_WEB for its own sources; our build
// passes the matching -D for the game translation units.
#if defined(PLATFORM_ANDROID) || defined(PLATFORM_WEB) || defined(PLATFORM_IOS)
#define OJ_TOUCH 1
#endif

// There is one renderer and one layout. A blackjack table is two rows of cards
// and a row of buttons, which fits any window: src/layout.c derives every
// metric from the live view size each frame, so the desktop, both phone
// orientations, and an iPad all run the same table, and rotating re-fits it.

#endif // OPENJACK_PLATFORM_H
