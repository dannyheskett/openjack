#include "game.h"
#include <stdlib.h>
#include <string.h>

Rules rules_default(void) {
    Rules r = { .decks = 6, .h17 = false };
    return r;
}

bool result_is_win(Result r) {
    return r == RES_BLACKJACK || r == RES_WIN || r == RES_DEALER_BUST;
}

bool result_is_loss(Result r) { return r == RES_LOSE || r == RES_BUST; }

// --------------------------------------------------------------------------
// Random numbers: xorshift64*, seeded by the caller, so a seed always deals the
// same shoe. rng_below() rejects the top sliver of the range instead of taking
// a plain modulo, which would favour the low cards.
// --------------------------------------------------------------------------
static uint64_t rng_next(Game* g) {
    uint64_t x = g->rng;
    x ^= x >> 12;
    x ^= x << 25;
    x ^= x >> 27;
    g->rng = x;
    return x * 0x2545F4914F6CDD1DULL;
}

static int rng_below(Game* g, int n) {
    uint64_t lim = UINT64_MAX - (UINT64_MAX % (uint64_t)n);  // a multiple of n
    uint64_t r;
    do r = rng_next(g); while (r >= lim);
    return (int)(r % (uint64_t)n);
}

// --------------------------------------------------------------------------
// Totals
// --------------------------------------------------------------------------
static int card_points(Card c) {
    if (c.rank >= 10) return 10;   // 10, J, Q, K
    if (c.rank == 1)  return 11;   // ace, demoted to 1 below when it must be
    return c.rank;
}

int hand_total(const Card* cards, int n, bool* soft) {
    int sum = 0, aces = 0;
    for (int i = 0; i < n; i++) {
        sum += card_points(cards[i]);
        if (cards[i].rank == 1) aces++;
    }
    while (sum > 21 && aces > 0) { sum -= 10; aces--; }
    if (soft) *soft = (aces > 0);
    return sum;
}

static int total_of(const Hand* h) { return hand_total(h->cards, h->n, NULL); }

bool game_hand_is_blackjack(const Hand* h) {
    return h->n == 2 && !h->from_split && total_of(h) == 21;
}

int game_visible_total(const Game* g, const Hand* h, bool* soft) {
    if (h != &g->dealer || g->hole_shown)
        return hand_total(h->cards, h->vis, soft);
    // The dealer's hole card (index 1) stays out of the total until it turns.
    // Nothing else is dealt to the dealer before then, so that leaves the up
    // card alone.
    return hand_total(h->cards, h->vis < 1 ? h->vis : 1, soft);
}

// --------------------------------------------------------------------------
// The shoe
// --------------------------------------------------------------------------
static void build_shoe(Game* g, int decks) {
    int k = 0;
    for (int d = 0; d < decks; d++)
        for (int s = 0; s < 4; s++)
            for (int r = 1; r <= 13; r++)
                g->shoe[k++] = (Card){ (uint8_t)r, (uint8_t)s };
    g->shoe_n = k;
    g->shoe_decks = decks;
}

static void shuffle_shoe(Game* g) {
    for (int i = g->shoe_n - 1; i > 0; i--) {
        int j = rng_below(g, i + 1);
        Card t = g->shoe[i]; g->shoe[i] = g->shoe[j]; g->shoe[j] = t;
    }
    g->draw = 0;
}

static void new_shoe(Game* g) {
    build_shoe(g, g->rules.decks);
    shuffle_shoe(g);
    g->short_shoe = false;
}

// Reshuffle between rounds once three quarters of the shoe is gone. For one
// deck that leaves 13 cards, which covers nearly every round; the rare round
// that needs more gets the backstop below.
static int cut_point(const Game* g) { return g->shoe_n * 3 / 4; }

static void remove_one(Game* g, Card c) {
    for (int i = 0; i < g->shoe_n; i++) {
        if (g->shoe[i].rank == c.rank && g->shoe[i].suit == c.suit) {
            g->shoe[i] = g->shoe[--g->shoe_n];
            return;
        }
    }
}

// The shoe ran out mid-round. Rebuild it from the cards that are NOT on the
// table and shuffle those, so no card can appear twice in one round. The next
// deal shuffles a full shoe again.
static void rebuild_without_table(Game* g) {
    build_shoe(g, g->shoe_decks);
    for (int h = 0; h < g->nhands; h++)
        for (int i = 0; i < g->hands[h].n; i++) remove_one(g, g->hands[h].cards[i]);
    for (int i = 0; i < g->dealer.n; i++) remove_one(g, g->dealer.cards[i]);
    if (g->shoe_n == 0) build_shoe(g, g->shoe_decks);   // cannot happen; stay safe
    shuffle_shoe(g);
    g->short_shoe = true;
}

static Card draw_card(Game* g) {
    if (g->draw >= g->shoe_n) rebuild_without_table(g);
    return g->shoe[g->draw++];
}

// --------------------------------------------------------------------------
// Presentation queue
// --------------------------------------------------------------------------
static void play_entry(Game* g, int e, bool quiet) {
    if (e < MAX_HANDS || e == Q_DEALER) {
        Hand* h = (e == Q_DEALER) ? &g->dealer : &g->hands[e];
        if (h->vis < h->n) h->at[h->vis++] = g->ticks;
        if (!quiet) {
            g->events |= EV_DEAL;
            if (e != Q_DEALER && hand_total(h->cards, h->vis, NULL) > 21) g->events |= EV_BUST;
        }
    } else if (e == Q_FLIP) {
        g->hole_shown = true;
        g->hole_at = g->ticks;
        if (!quiet) g->events |= EV_FLIP;
    } else if (e == Q_SETTLE) {
        g->shown_wins = g->wins;
        g->shown_losses = g->losses;
        g->settled_shown = true;
        if (!quiet) g->events |= g->outcome_ev;
        g->outcome_ev = 0;
    }
}

static int q_pop(Game* g) {
    int e = g->queue[g->q_head];
    g->q_head = (g->q_head + 1) % QUEUE_MAX;
    g->q_len--;
    return e;
}

// Play everything still queued, instantly and silently. Used before any change
// that would leave the queue pointing at stale cards; in the real game the
// queue is already empty by then, because input waits for it.
static void flush_queue(Game* g) {
    while (g->q_len > 0) play_entry(g, q_pop(g), true);
    g->timer = 0;
}

static void q_push(Game* g, int e) {
    if (g->q_len >= QUEUE_MAX) flush_queue(g);
    g->queue[(g->q_head + g->q_len) % QUEUE_MAX] = e;
    g->q_len++;
}

void game_update(Game* g) {
    g->ticks++;
    if (g->timer > 0) g->timer--;
    while (g->timer == 0 && g->q_len > 0) {
        int e = q_pop(g);
        play_entry(g, e, false);
        g->timer = (e == Q_FLIP) ? FLIP_STEPS : (e == Q_SETTLE) ? 0 : DEAL_STEPS;
    }
}

bool game_busy(const Game* g) { return g->q_len > 0 || g->timer > 0; }

// --------------------------------------------------------------------------
// Lifecycle
// --------------------------------------------------------------------------
Game* game_create(uint64_t seed, Rules rules) {
    Game* g = calloc(1, sizeof(Game));
    if (!g) return NULL;
    g->rng = seed ? seed : 0x9E3779B97F4A7C15ULL;   // xorshift never leaves 0
    if (rules.decks != 1) rules.decks = MAX_DECKS;
    g->rules = g->next_rules = rules;
    new_shoe(g);
    g->phase = PHASE_READY;
    return g;
}

void game_destroy(Game* g) { free(g); }

void game_step_begin(Game* g) { g->events = 0; }

void game_set_rules(Game* g, Rules r) {
    if (r.decks != 1) r.decks = MAX_DECKS;
    g->next_rules = r;
}

// --------------------------------------------------------------------------
// Round flow
// --------------------------------------------------------------------------
static void deal_to(Game* g, Hand* h, int target) {
    if (h->n >= MAX_HAND_CARDS) return;   // unreachable: see MAX_HAND_CARDS
    h->cards[h->n++] = draw_card(g);
    q_push(g, target);
}

static void reveal_hole(Game* g) {
    if (g->hole_up) return;
    g->hole_up = true;
    q_push(g, Q_FLIP);
}

// Close the round: count each hand's win or loss and queue the outcome, which
// the player sees (and hears) once every card before it has landed.
static void finish_round(Game* g) {
    g->phase = PHASE_RESULT;
    g->round_wins = g->round_losses = 0;

    bool natural = false, all_bust = true;
    for (int i = 0; i < g->nhands; i++) {
        Result r = g->hands[i].result;
        if (r == RES_BLACKJACK) natural = true;
        if (r != RES_BUST) all_bust = false;
        if (result_is_win(r))  g->round_wins++;
        if (result_is_loss(r)) g->round_losses++;
    }
    g->wins += g->round_wins;
    g->losses += g->round_losses;

    // A hand that busts already made its sound as the card landed.
    if (natural)                                  g->outcome_ev = EV_BLACKJACK;
    else if (all_bust)                            g->outcome_ev = 0;
    else if (g->round_wins > g->round_losses)     g->outcome_ev = EV_WIN;
    else if (g->round_wins < g->round_losses)     g->outcome_ev = EV_LOSE;
    else                                          g->outcome_ev = EV_PUSH;
    q_push(g, Q_SETTLE);
}

static bool dealer_should_hit(const Game* g) {
    bool soft;
    int t = hand_total(g->dealer.cards, g->dealer.n, &soft);
    return t < 17 || (t == 17 && soft && g->rules.h17);
}

// Every hand has finished. The hole card turns; if any hand is still in play
// the dealer draws, then each live hand is settled against the dealer.
static void dealer_turn(Game* g) {
    reveal_hole(g);
    bool live = false;
    for (int i = 0; i < g->nhands; i++)
        if (g->hands[i].result == RES_NONE) live = true;
    if (live)
        while (dealer_should_hit(g)) deal_to(g, &g->dealer, Q_DEALER);

    int dv = total_of(&g->dealer);
    for (int i = 0; i < g->nhands; i++) {
        Hand* h = &g->hands[i];
        if (h->result != RES_NONE) continue;
        int pv = total_of(h);
        if (dv > 21)       h->result = RES_DEALER_BUST;
        else if (pv > dv)  h->result = RES_WIN;
        else if (pv == dv) h->result = RES_PUSH;
        else               h->result = RES_LOSE;
    }
    finish_round(g);
}

// Move to the next hand that still has to act. A split hand holds one card
// until play reaches it, so it gets its second card here, the order a dealer
// deals it at the table.
static void advance(Game* g) {
    while (g->active < g->nhands && g->hands[g->active].done) {
        g->active++;
        if (g->active >= g->nhands) break;
        Hand* h = &g->hands[g->active];
        if (h->n == 1) {
            deal_to(g, h, g->active);
            if (h->split_aces || total_of(h) == 21) h->done = true;
        }
    }
    if (g->active >= g->nhands) dealer_turn(g);
}

// After a card lands on the active hand: over 21 busts, 21 stands by itself.
static void after_card(Game* g) {
    Hand* h = &g->hands[g->active];
    int t = total_of(h);
    if (t > 21) {
        h->result = RES_BUST;
        h->done = true;
    } else if (t == 21 || h->split_aces) {
        h->done = true;
    }
    if (h->done) advance(g);
}

// The dealer checks the hole card for blackjack under an ace or a ten, then
// the naturals are settled. Otherwise play passes to the player.
static void peek_and_continue(Game* g) {
    Hand* p = &g->hands[0];
    bool pbj = game_hand_is_blackjack(p);
    bool dbj = hand_total(g->dealer.cards, 2, NULL) == 21;

    if (dbj || pbj) {
        reveal_hole(g);
        p->result = (dbj && pbj) ? RES_PUSH : dbj ? RES_LOSE : RES_BLACKJACK;
        p->done = true;
        finish_round(g);
        return;
    }
    g->phase = PHASE_PLAYER;
    g->active = 0;
}

bool game_deal(Game* g) {
    if (g->phase != PHASE_READY && g->phase != PHASE_RESULT) return false;
    flush_queue(g);
    g->rules = g->next_rules;

    if (g->short_shoe || g->rules.decks != g->shoe_decks || g->draw >= cut_point(g))
        new_shoe(g);

    memset(g->hands, 0, sizeof g->hands);
    memset(&g->dealer, 0, sizeof g->dealer);
    g->nhands = 1;
    g->active = 0;
    g->hole_up = g->hole_shown = false;
    g->round_wins = g->round_losses = 0;
    g->settled_shown = false;
    g->outcome_ev = 0;
    g->hands_played++;

    // Player, dealer up card, player, dealer hole card.
    deal_to(g, &g->hands[0], 0);
    deal_to(g, &g->dealer, Q_DEALER);
    deal_to(g, &g->hands[0], 0);
    deal_to(g, &g->dealer, Q_DEALER);

    peek_and_continue(g);
    return true;
}

static Hand* acting(Game* g) {
    if (g->phase != PHASE_PLAYER || g->active >= g->nhands) return NULL;
    Hand* h = &g->hands[g->active];
    return h->done ? NULL : h;
}

static const Hand* acting_c(const Game* g) { return acting((Game*)g); }

void game_hit(Game* g) {
    Hand* h = acting(g);
    if (!h) return;
    deal_to(g, h, g->active);
    after_card(g);
}

void game_stand(Game* g) {
    Hand* h = acting(g);
    if (!h) return;
    h->done = true;
    advance(g);
}

bool game_can_split(const Game* g) {
    const Hand* h = acting_c(g);
    return h && h->n == 2 && g->nhands < MAX_HANDS
        && card_points(h->cards[0]) == card_points(h->cards[1]);
}

void game_split(Game* g) {
    if (!game_can_split(g)) return;
    flush_queue(g);
    int i = g->active;
    for (int k = g->nhands; k > i + 1; k--) g->hands[k] = g->hands[k - 1];

    Hand* h  = &g->hands[i];
    Hand* nh = &g->hands[i + 1];
    bool aces = (h->cards[0].rank == 1);
    memset(nh, 0, sizeof *nh);
    nh->cards[0] = h->cards[1];
    nh->n = nh->vis = 1;
    nh->at[0] = h->at[1];
    nh->from_split = true;
    nh->split_aces = aces;
    h->n = h->vis = 1;
    h->from_split = true;
    h->split_aces = aces;
    g->nhands++;

    deal_to(g, h, i);
    after_card(g);   // split aces stop at one card; a 21 stands
}
