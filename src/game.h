#ifndef OPENJACK_GAME_H
#define OPENJACK_GAME_H

#include <stdbool.h>
#include <stdint.h>

// --------------------------------------------------------------------------
// Blackjack (21) against the house.
//
//   - rank 1..13 (1 = Ace, 11/12/13 = J/Q/K); suit 0..3 (clubs, diamonds,
//     hearts, spades): the same card shape as openklondike, whose card art the
//     renderer draws.
//   - Hit, Stand, Double (any first two cards, and after a split), Split (up to
//     four hands; split aces take one card each), late Surrender (an Options
//     rule), and Insurance / Even Money when the dealer shows an ace.
//   - The dealer peeks for blackjack under an ace or a ten, then draws to 17.
//     Options choose whether the dealer stands on or hits a soft 17, and a one-
//     or six-deck shoe.
//   - Blackjack pays 3:2, insurance 2:1, a tie is a push.
//
// Pure logic: no drawing, no input, no raylib. Every rule resolves the moment
// the player acts, so tests read the outcome straight away. What the player
// SEES is paced separately: each dealt card and the hole-card flip go into a
// queue that game_update() plays out one card at a time on the fixed 60 Hz
// clock. The renderer draws only the cards that have come off that queue, and
// input waits until it is empty (game_busy).
// --------------------------------------------------------------------------
typedef struct { uint8_t rank, suit; } Card;

static inline bool card_is_red(Card c) { return c.suit == 1 || c.suit == 2; }

// Table limits. Chips move in whole units: bets are multiples of BET_STEP, an
// even number, so a 3:2 blackjack payout and a half-bet insurance or surrender
// are always whole.
#define START_BANKROLL 1000
#define MIN_BET          50
#define BET_STEP         50
#define DEFAULT_BET     100

#define MAX_DECKS       6
#define SHOE_MAX        (52 * MAX_DECKS)
#define MAX_HANDS       4
// A hand that has not busted totals at most 21 and every card counts at least
// one, so 21 cards plus the one that busts it is the most any hand can hold.
#define MAX_HAND_CARDS  22

// Fixed 60 Hz steps between two dealt cards, and for the hole card to turn.
#define DEAL_STEPS      15
#define FLIP_STEPS      12

typedef enum {
    PHASE_BET = 0,    // choosing the wager
    PHASE_INSURANCE,  // dealer shows an ace: insurance or even money offered
    PHASE_PLAYER,     // the active hand acts
    PHASE_RESULT,     // round settled
} GamePhase;

typedef enum {
    RES_NONE = 0,
    RES_BLACKJACK,    // player natural, paid 3:2
    RES_EVEN_MONEY,   // natural against an ace, paid 1:1 on request
    RES_WIN,
    RES_DEALER_BUST,
    RES_PUSH,
    RES_LOSE,
    RES_BUST,         // this hand went over 21
    RES_SURRENDER,    // half the bet returned
} Result;

// Per-step event flags consumed by main.c to drive sound. Card and outcome
// events fire when the player sees them (as the queue plays out), not when the
// rules decide them.
enum {
    EV_DEAL      = 1 << 0,  // a card lands
    EV_FLIP      = 1 << 1,  // the hole card turns
    EV_CHIP      = 1 << 2,  // the bet changed, or chips went down
    EV_WIN       = 1 << 3,
    EV_LOSE      = 1 << 4,
    EV_PUSH      = 1 << 5,
    EV_BLACKJACK = 1 << 6,
    EV_BUST      = 1 << 7,
};

typedef struct {
    int  decks;       // 1 or 6
    bool h17;         // dealer hits soft 17 (false: stands on all 17s)
    bool surrender;   // late surrender offered
} Rules;

Rules rules_default(void);

typedef struct {
    Card cards[MAX_HAND_CARDS];
    int  n;               // cards in the hand
    int  vis;             // of those, how many the player has seen land
    int  at[MAX_HAND_CARDS]; // game tick each card landed (renderer animation)
    int  wager;
    bool doubled;
    bool split_aces;      // one card only, and 21 is not blackjack
    bool from_split;      // 21 on two cards is not blackjack
    bool done;            // stood, busted, doubled, surrendered or 21
    Result result;
    int  delta;           // net chips won or lost by this hand
} Hand;

// Queue entries: a hand index 0..MAX_HANDS-1, or one of these.
enum { Q_DEALER = MAX_HANDS, Q_FLIP, Q_SETTLE };

#define QUEUE_MAX 128

typedef struct {
    // The shoe.
    Card shoe[SHOE_MAX];
    int  shoe_n;          // cards in the shoe
    int  draw;            // index of the next card
    int  shoe_decks;      // decks the shoe was built from
    bool short_shoe;      // rebuilt mid-round; reshuffle in full before the next

    Rules rules;          // in force this round
    Rules next_rules;     // Options changes, applied at the next deal

    Hand hands[MAX_HANDS];
    int  nhands;
    int  active;          // hand acting in PHASE_PLAYER
    Hand dealer;          // cards[1] is the hole card
    bool hole_up;         // the rules have revealed the hole card
    bool hole_shown;      // ...and the player has seen it turn
    int  hole_at;         // tick the hole card turned (renderer animation)

    GamePhase phase;
    int  bankroll;
    int  shown_bankroll;  // what the status line shows: catches up with the
                          // bankroll once the player has seen the outcome
    int  bet;             // wager chosen in PHASE_BET
    int  insurance;       // insurance stake this round (0 when declined)
    int  insurance_delta; // net chips from insurance once settled
    bool even_money;      // the natural was paid 1:1 against the ace
    int  round_start;     // bankroll before this round's bet went down
    int  last_delta;      // net change of the settled round
    bool refilled;        // the bankroll ran dry and was reset; main shows it
    int  hands_played;

    // Presentation queue (see the header comment).
    int  queue[QUEUE_MAX];
    int  q_head, q_len;
    int  timer;           // steps until the next queue entry plays
    int  ticks;           // fixed steps since the game began
    unsigned outcome_ev;  // sounds to fire when the settle entry plays
    bool settled_shown;   // the result has been shown to the player

    unsigned events;      // EV_* set this step, cleared by game_step_begin
    uint64_t rng;
} Game;

// Lifecycle ----------------------------------------------------------------
// The same seed always deals the same shoe, which is what the tests rely on.
Game* game_create(uint64_t seed, Rules rules);
void  game_destroy(Game* g);
void  game_step_begin(Game* g);          // clear per-step events
void  game_set_rules(Game* g, Rules r);  // takes effect at the next deal

// Advance one fixed 60 Hz step: plays the presentation queue.
void  game_update(Game* g);
// True while dealt cards are still landing. Input waits for it.
bool  game_busy(const Game* g);

// Actions. Each is ignored unless it is legal right now.
void  game_bet_change(Game* g, int steps);  // PHASE_BET, +/- BET_STEP each
bool  game_deal(Game* g);                   // BET -> INSURANCE / PLAYER / RESULT
void  game_insurance(Game* g, bool take);   // INSURANCE; even money on a natural
void  game_hit(Game* g);
void  game_stand(Game* g);
void  game_double(Game* g);
void  game_split(Game* g);
void  game_surrender(Game* g);
void  game_next(Game* g);                   // RESULT -> BET; refills when broke

bool  game_can_double(const Game* g);
bool  game_can_split(const Game* g);
bool  game_can_surrender(const Game* g);
bool  game_can_insure(const Game* g);       // bankroll covers the insurance
bool  game_offers_even_money(const Game* g);

// Totals. `soft` is set when an ace still counts 11.
int   hand_total(const Card* cards, int n, bool* soft);
// The total of the cards the player can see: landed cards, less the hole card
// until it has turned.
int   game_visible_total(const Game* g, const Hand* h, bool* soft);
bool  game_hand_is_blackjack(const Hand* h);

#endif
