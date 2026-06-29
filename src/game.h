#ifndef OPENJACK_GAME_H
#define OPENJACK_GAME_H

#include <stdint.h>
#include <stdbool.h>

// --------------------------------------------------------------------------
// Blackjack / 21 against the house.
//   - rank 1..13 (1 = Ace, 11/12/13 = J/Q/K); suit 0..3 (clubs, diamonds,
//     hearts, spades) — same shape as openklondike so its card art is reused.
//   - Hit / Stand / Double. Dealer draws to 17 (stands on all 17s).
//   - Blackjack pays 3:2; ties push. No insurance, no split.
//   - Bankroll with a per-hand bet; a fresh stake is granted if you go broke.
// --------------------------------------------------------------------------
typedef struct { uint8_t rank, suit; } Card;

static inline bool card_is_red(Card c) { return c.suit == 1 || c.suit == 2; }

typedef enum {
    PHASE_BET = 0,   // choosing the wager
    PHASE_PLAYER,    // hit / stand / double
    PHASE_DEALER,    // dealer drawing (transient)
    PHASE_RESULT,    // round settled, showing the outcome
} GamePhase;

typedef enum {
    RES_NONE = 0,
    RES_BLACKJACK,   // player natural, paid 3:2
    RES_WIN,
    RES_PUSH,
    RES_LOSE,
    RES_BUST,        // player busted
    RES_DEALER_BUST, // dealer busted
} Result;

enum {
    EV_DEAL      = 1 << 0,
    EV_HIT       = 1 << 1,
    EV_WIN       = 1 << 2,
    EV_LOSE      = 1 << 3,
    EV_PUSH      = 1 << 4,
    EV_BLACKJACK = 1 << 5,
    EV_BUST      = 1 << 6,
    EV_CHIP      = 1 << 7,
    EV_MENU_MOVE = 1 << 8,
    EV_MENU_SEL  = 1 << 9,
};

#define START_BANKROLL 500
#define MIN_BET         25
#define BET_STEP        25

typedef struct {
    Card deck[52];
    int  draw;            // index of the next card to deal

    Card player[12]; int pn;
    Card dealer[12]; int dn;
    bool reveal;          // dealer hole card shown

    GamePhase phase;
    Result    result;

    int  bankroll;
    int  bet;             // wager chosen in PHASE_BET
    int  wager;           // amount actually at risk this round (doubles)
    int  last_delta;      // net bankroll change of the settled round
    bool doubled;
    bool refilled;        // a fresh stake was just granted

    unsigned events;
    unsigned rng;
} Game;

Game* game_create(void);
void  game_destroy(Game* g);
void  game_frame_begin(Game* g);

void  game_bet_change(Game* g, int delta);   // adjust the wager (PHASE_BET)
bool  game_deal(Game* g);                     // BET -> PLAYER (or RESULT on naturals)
void  game_hit(Game* g);                      // PLAYER
void  game_stand(Game* g);                    // PLAYER -> dealer plays -> RESULT
void  game_double(Game* g);                   // PLAYER (first two cards)
void  game_next(Game* g);                     // RESULT -> BET (refill if broke)

int   game_player_value(const Game* g);
int   game_dealer_value(const Game* g);       // full dealer total
int   game_dealer_shown(const Game* g);       // up-card only until revealed
bool  game_can_double(const Game* g);

#endif
