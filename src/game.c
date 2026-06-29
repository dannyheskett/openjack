#include "game.h"
#include <stdlib.h>
#include <string.h>

#define RESHUFFLE_AT 40   // reshuffle once fewer than (52-40) cards remain

// --------------------------------------------------------------------------
// Cards / hand value
// --------------------------------------------------------------------------
static int card_points(Card c) {
    if (c.rank >= 10) return 10;   // 10, J, Q, K
    if (c.rank == 1)  return 11;   // ace (high; demoted below if needed)
    return c.rank;
}

// Best total <= 21 if possible; *soft set when an ace still counts as 11.
static int hand_value(const Card* h, int n, bool* soft) {
    int sum = 0, aces = 0;
    for (int i = 0; i < n; i++) {
        sum += card_points(h[i]);
        if (h[i].rank == 1) aces++;
    }
    while (sum > 21 && aces > 0) { sum -= 10; aces--; }
    if (soft) *soft = (aces > 0);
    return sum;
}

static bool is_blackjack(const Card* h, int n) {
    return n == 2 && hand_value(h, n, NULL) == 21;
}

// --------------------------------------------------------------------------
// Deck
// --------------------------------------------------------------------------
static void shuffle(Game* g) {
    int k = 0;
    for (int s = 0; s < 4; s++)
        for (int r = 1; r <= 13; r++)
            g->deck[k++] = (Card){ (uint8_t)r, (uint8_t)s };
    for (int i = 51; i > 0; i--) {
        int j = rand() % (i + 1);
        Card t = g->deck[i]; g->deck[i] = g->deck[j]; g->deck[j] = t;
    }
    g->draw = 0;
}

static Card deal_card(Game* g) {
    if (g->draw >= 52) shuffle(g);
    return g->deck[g->draw++];
}

// --------------------------------------------------------------------------
// Lifecycle
// --------------------------------------------------------------------------
Game* game_create(void) {
    Game* g = calloc(1, sizeof(Game));
    if (!g) return NULL;
    g->rng = (unsigned)rand();
    shuffle(g);
    g->bankroll = START_BANKROLL;
    g->bet = MIN_BET * 2;          // default wager
    g->phase = PHASE_BET;
    g->result = RES_NONE;
    return g;
}

void game_destroy(Game* g) { free(g); }

void game_frame_begin(Game* g) { g->events = 0; g->refilled = false; }

int game_player_value(const Game* g) { return hand_value(g->player, g->pn, NULL); }
int game_dealer_value(const Game* g) { return hand_value(g->dealer, g->dn, NULL); }

int game_dealer_shown(const Game* g) {
    if (g->reveal || g->dn == 0) return hand_value(g->dealer, g->dn, NULL);
    return hand_value(g->dealer, 1, NULL);   // up card only
}

bool game_can_double(const Game* g) {
    return g->phase == PHASE_PLAYER && g->pn == 2 && g->bankroll >= g->bet;
}

// --------------------------------------------------------------------------
// Betting
// --------------------------------------------------------------------------
void game_bet_change(Game* g, int delta) {
    if (g->phase != PHASE_BET) return;
    g->bet += delta;
    if (g->bet < MIN_BET) g->bet = MIN_BET;
    if (g->bet > g->bankroll) g->bet = g->bankroll;
    if (g->bet < MIN_BET) g->bet = MIN_BET;   // bankroll below min (guarded by refill)
    g->events |= EV_CHIP;
}

// --------------------------------------------------------------------------
// Round flow
// --------------------------------------------------------------------------
static void settle_naturals(Game* g) {
    bool pbj = is_blackjack(g->player, g->pn);
    bool dbj = is_blackjack(g->dealer, g->dn);
    g->reveal = true;
    g->phase = PHASE_RESULT;
    if (pbj && dbj) {
        g->bankroll += g->wager; g->last_delta = 0; g->result = RES_PUSH;  g->events |= EV_PUSH;
    } else if (pbj) {
        int win = (g->wager * 3) / 2;
        g->bankroll += g->wager + win; g->last_delta = win;
        g->result = RES_BLACKJACK; g->events |= EV_BLACKJACK;
    } else { // dbj
        g->last_delta = -g->wager; g->result = RES_LOSE; g->events |= EV_LOSE;
    }
}

bool game_deal(Game* g) {
    if (g->phase != PHASE_BET) return false;
    if (g->bet < MIN_BET) g->bet = MIN_BET;
    if (g->bet > g->bankroll) g->bet = g->bankroll;
    if (g->bankroll < g->bet || g->bet < MIN_BET) return false;

    if (g->draw > RESHUFFLE_AT) shuffle(g);

    g->bankroll -= g->bet;
    g->wager = g->bet;
    g->pn = g->dn = 0;
    g->reveal = false;
    g->doubled = false;
    g->result = RES_NONE;
    g->last_delta = 0;

    g->player[g->pn++] = deal_card(g);
    g->dealer[g->dn++] = deal_card(g);   // up card
    g->player[g->pn++] = deal_card(g);
    g->dealer[g->dn++] = deal_card(g);   // hole card
    g->events |= EV_DEAL;

    if (is_blackjack(g->player, g->pn) || is_blackjack(g->dealer, g->dn))
        settle_naturals(g);
    else
        g->phase = PHASE_PLAYER;
    return true;
}

static void dealer_play_and_settle(Game* g) {
    g->reveal = true;
    g->phase = PHASE_DEALER;
    while (hand_value(g->dealer, g->dn, NULL) < 17 && g->dn < 12)
        g->dealer[g->dn++] = deal_card(g);

    int pv = hand_value(g->player, g->pn, NULL);
    int dv = hand_value(g->dealer, g->dn, NULL);
    g->phase = PHASE_RESULT;
    if (dv > 21) {
        g->bankroll += g->wager * 2; g->last_delta = g->wager;
        g->result = RES_DEALER_BUST; g->events |= EV_WIN;
    } else if (pv > dv) {
        g->bankroll += g->wager * 2; g->last_delta = g->wager;
        g->result = RES_WIN; g->events |= EV_WIN;
    } else if (pv == dv) {
        g->bankroll += g->wager; g->last_delta = 0;
        g->result = RES_PUSH; g->events |= EV_PUSH;
    } else {
        g->last_delta = -g->wager; g->result = RES_LOSE; g->events |= EV_LOSE;
    }
}

void game_hit(Game* g) {
    if (g->phase != PHASE_PLAYER) return;
    if (g->pn < 12) g->player[g->pn++] = deal_card(g);
    g->events |= EV_HIT;
    if (hand_value(g->player, g->pn, NULL) > 21) {
        g->reveal = true; g->result = RES_BUST; g->last_delta = -g->wager;
        g->phase = PHASE_RESULT; g->events |= EV_BUST;
    }
}

void game_double(Game* g) {
    if (!game_can_double(g)) return;
    g->bankroll -= g->bet;
    g->wager += g->bet;
    g->doubled = true;
    if (g->pn < 12) g->player[g->pn++] = deal_card(g);
    g->events |= EV_HIT;
    if (hand_value(g->player, g->pn, NULL) > 21) {
        g->reveal = true; g->result = RES_BUST; g->last_delta = -g->wager;
        g->phase = PHASE_RESULT; g->events |= EV_BUST;
        return;
    }
    dealer_play_and_settle(g);
}

void game_stand(Game* g) {
    if (g->phase != PHASE_PLAYER) return;
    dealer_play_and_settle(g);
}

void game_next(Game* g) {
    if (g->phase != PHASE_RESULT) return;
    g->pn = g->dn = 0;
    g->reveal = false;
    g->doubled = false;
    g->result = RES_NONE;
    g->wager = 0;
    if (g->bankroll < MIN_BET) { g->bankroll = START_BANKROLL; g->refilled = true; }
    if (g->bet > g->bankroll) g->bet = g->bankroll;
    if (g->bet < MIN_BET) g->bet = MIN_BET;
    g->phase = PHASE_BET;
}
