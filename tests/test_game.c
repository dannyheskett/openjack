// Unit tests for openjack's rules and its fixed-timestep clock: no raylib, no
// window. game.c and tick.c are included directly to reach their static
// helpers. The rules resolve synchronously, so most tests read the outcome
// straight after the action; the ones about what the player sees drain the
// presentation queue with game_update().
//
// Built and run by `make test`. A non-zero exit means a failure.
#include "../src/game.c"
#include "../src/tick.c"

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

static Card mk(int rank) { return (Card){ (uint8_t)rank, 0 }; }

// Overwrite the shoe from the next card on, so deals are known. The order is
// player, dealer up, player, dealer hole, then every later draw.
static void stack(Game* g, const int* ranks, int n) {
    for (int i = 0; i < n; i++) g->shoe[g->draw + i] = (Card){ (uint8_t)ranks[i], (uint8_t)(i % 4) };
}
#define STACK(g, ...) do { int r_[] = { __VA_ARGS__ }; stack(g, r_, (int)(sizeof r_ / sizeof r_[0])); } while (0)

static Game* fresh(void) { return game_create(12345, rules_default()); }

static Game* fresh_rules(bool h17, bool surrender, int decks) {
    Rules r = { .decks = decks, .h17 = h17, .surrender = surrender };
    return game_create(12345, r);
}

// Play the presentation queue out. Returns every event it raised.
static unsigned drain(Game* g) {
    unsigned ev = 0;
    for (int i = 0; i < 10000 && game_busy(g); i++) {
        game_step_begin(g);
        game_update(g);
        ev |= g->events;
    }
    return ev;
}

// --- Totals -----------------------------------------------------------------
static void test_totals(void) {
    bool soft;
    Card a6[] = { mk(1), mk(6) };
    CHECK(hand_total(a6, 2, &soft) == 17 && soft);
    Card a6t[] = { mk(1), mk(6), mk(10) };
    CHECK(hand_total(a6t, 3, &soft) == 17 && !soft);
    Card aa[] = { mk(1), mk(1) };
    CHECK(hand_total(aa, 2, NULL) == 12);
    Card aa9[] = { mk(1), mk(1), mk(9) };
    CHECK(hand_total(aa9, 3, NULL) == 21);
    Card kq5[] = { mk(13), mk(12), mk(5) };
    CHECK(hand_total(kq5, 3, NULL) == 25);

    Hand h = {0};
    h.cards[0] = mk(1); h.cards[1] = mk(13); h.n = 2;
    CHECK(game_hand_is_blackjack(&h));
    h.from_split = true;
    CHECK(!game_hand_is_blackjack(&h));       // 21 after a split is just 21
    Hand t = {0};
    t.cards[0] = mk(7); t.cards[1] = mk(7); t.cards[2] = mk(7); t.n = 3;
    CHECK(!game_hand_is_blackjack(&t));
}

// --- Naturals, insurance, even money ----------------------------------------
static void test_blackjack_pays_3_to_2(void) {
    Game* g = fresh();
    STACK(g, 1, 5, 13, 6);                    // player A K, dealer 5 6
    CHECK(game_deal(g));
    CHECK(g->phase == PHASE_RESULT);
    CHECK(g->hands[0].result == RES_BLACKJACK);
    CHECK(g->last_delta == 150);
    CHECK(g->bankroll == START_BANKROLL + 150);
    game_destroy(g);
}

static void test_blackjack_payout_is_whole_at_every_bet(void) {
    for (int bet = MIN_BET; bet <= 500; bet += BET_STEP) {
        Game* g = fresh();
        g->bet = bet;
        STACK(g, 1, 5, 13, 6);
        game_deal(g);
        CHECK(g->last_delta * 2 == bet * 3);  // exact 3:2, nothing rounded away
        game_destroy(g);
    }
}

static void test_dealer_ten_up_blackjack(void) {
    Game* g = fresh();
    STACK(g, 5, 10, 6, 1);                    // dealer 10 A: peeked, no insurance
    game_deal(g);
    CHECK(g->phase == PHASE_RESULT);
    CHECK(g->hands[0].result == RES_LOSE);
    CHECK(g->bankroll == START_BANKROLL - DEFAULT_BET);
    CHECK(g->hole_up);
    game_destroy(g);
}

static void test_even_money(void) {
    Game* g = fresh();
    STACK(g, 1, 1, 13, 9);                    // player A K, dealer A 9
    game_deal(g);
    CHECK(g->phase == PHASE_INSURANCE);
    CHECK(game_offers_even_money(g));
    game_insurance(g, true);
    CHECK(g->hands[0].result == RES_EVEN_MONEY);
    CHECK(g->last_delta == DEFAULT_BET);
    game_destroy(g);

    g = fresh();                              // declined, dealer has it: push
    STACK(g, 1, 1, 13, 13);
    game_deal(g);
    game_insurance(g, false);
    CHECK(g->hands[0].result == RES_PUSH);
    CHECK(g->bankroll == START_BANKROLL);
    game_destroy(g);

    g = fresh();                              // declined, dealer does not: 3:2
    STACK(g, 1, 1, 13, 9);
    game_deal(g);
    game_insurance(g, false);
    CHECK(g->hands[0].result == RES_BLACKJACK);
    CHECK(g->last_delta == 150);
    game_destroy(g);
}

static void test_insurance_wins_against_blackjack(void) {
    Game* g = fresh();
    STACK(g, 5, 1, 6, 13);                    // player 11, dealer A K
    game_deal(g);
    CHECK(g->phase == PHASE_INSURANCE);
    CHECK(!game_offers_even_money(g));
    CHECK(game_can_insure(g));
    game_insurance(g, true);
    CHECK(g->phase == PHASE_RESULT);
    CHECK(g->hands[0].result == RES_LOSE);
    CHECK(g->insurance_delta == 100);         // 2:1 on 50
    CHECK(g->last_delta == 0);                // the bet lost, insurance covered it
    CHECK(g->bankroll == START_BANKROLL);
    game_destroy(g);
}

static void test_insurance_lost(void) {
    Game* g = fresh();
    STACK(g, 5, 1, 6, 7);                     // player 11, dealer A 7 (soft 18)
    game_deal(g);
    game_insurance(g, true);
    CHECK(g->phase == PHASE_PLAYER);
    CHECK(g->bankroll == START_BANKROLL - 150);
    CHECK(g->insurance_delta == -50);
    game_stand(g);
    CHECK(g->hands[0].result == RES_LOSE);
    CHECK(g->last_delta == -150);
    game_destroy(g);
}

static void test_insurance_needs_the_chips(void) {
    Game* g = fresh();
    g->bankroll = g->shown_bankroll = 120;    // 100 bet leaves 20 < 50
    STACK(g, 5, 1, 6, 7);
    game_deal(g);
    CHECK(!game_can_insure(g));
    game_insurance(g, true);                  // refused, play goes on uninsured
    CHECK(g->insurance == 0);
    CHECK(g->phase == PHASE_PLAYER);
    game_destroy(g);
}

// --- Standing, hitting, the dealer ------------------------------------------
static void test_stand_and_win_after_dealer_draws(void) {
    Game* g = fresh();
    STACK(g, 10, 6, 9, 10, 2);                // player 19, dealer 16 draws 2
    game_deal(g);
    game_stand(g);
    CHECK(g->dealer.n == 3);
    CHECK(g->hands[0].result == RES_WIN);
    CHECK(g->bankroll == START_BANKROLL + DEFAULT_BET);
    game_destroy(g);
}

static void test_dealer_bust(void) {
    Game* g = fresh();
    STACK(g, 10, 6, 9, 10, 10);
    game_deal(g);
    game_stand(g);
    CHECK(g->hands[0].result == RES_DEALER_BUST);
    CHECK(g->last_delta == DEFAULT_BET);
    game_destroy(g);
}

static void test_push_after_standing(void) {
    Game* g = fresh();
    STACK(g, 10, 10, 8, 8);
    game_deal(g);
    game_stand(g);
    CHECK(g->hands[0].result == RES_PUSH);
    CHECK(g->bankroll == START_BANKROLL);
    game_destroy(g);
}

static void test_player_bust_dealer_does_not_draw(void) {
    Game* g = fresh();
    STACK(g, 10, 6, 6, 10, 10);               // player 16 hits a 10
    game_deal(g);
    game_hit(g);
    CHECK(g->hands[0].result == RES_BUST);
    CHECK(g->phase == PHASE_RESULT);
    CHECK(g->dealer.n == 2);
    CHECK(g->hole_up);                        // the hole card still turns
    CHECK(g->bankroll == START_BANKROLL - DEFAULT_BET);
    game_destroy(g);
}

static void test_21_stands_by_itself(void) {
    Game* g = fresh();
    STACK(g, 5, 10, 6, 7, 10);                // player 11 hits 10: 21
    game_deal(g);
    game_hit(g);
    CHECK(g->phase == PHASE_RESULT);          // no chance to hit a 21
    CHECK(g->hands[0].result == RES_WIN);
    game_destroy(g);
}

static void test_soft_17(void) {
    Game* g = fresh_rules(false, false, 6);   // S17: stands
    STACK(g, 10, 6, 8, 1, 4);
    game_deal(g);
    game_stand(g);
    CHECK(g->dealer.n == 2);
    CHECK(g->hands[0].result == RES_WIN);
    game_destroy(g);

    g = fresh_rules(true, false, 6);          // H17: hits the soft 17
    STACK(g, 10, 6, 8, 1, 4);
    game_deal(g);
    game_stand(g);
    CHECK(g->dealer.n == 3);
    CHECK(g->hands[0].result == RES_LOSE);    // 21 beats 18
    game_destroy(g);
}

// --- Doubling ---------------------------------------------------------------
static void test_double(void) {
    Game* g = fresh();
    STACK(g, 5, 10, 6, 7, 10);                // 11 doubles into 21
    game_deal(g);
    CHECK(game_can_double(g));
    game_double(g);
    CHECK(g->hands[0].doubled && g->hands[0].wager == 200);
    CHECK(g->hands[0].result == RES_WIN);
    CHECK(g->bankroll == START_BANKROLL + 200);
    game_destroy(g);

    g = fresh();
    STACK(g, 10, 10, 6, 7, 10);               // 16 doubles into 26
    game_deal(g);
    game_double(g);
    CHECK(g->hands[0].result == RES_BUST);
    CHECK(g->bankroll == START_BANKROLL - 200);
    game_destroy(g);
}

static void test_double_refused(void) {
    Game* g = fresh();
    STACK(g, 2, 10, 3, 7, 2);
    game_deal(g);
    game_hit(g);                              // three cards
    CHECK(!game_can_double(g));
    int before = g->bankroll;
    game_double(g);
    CHECK(g->bankroll == before && g->hands[0].n == 3);
    game_destroy(g);

    g = fresh();
    g->bankroll = g->shown_bankroll = 150;    // 100 bet leaves 50 < 100
    STACK(g, 5, 10, 6, 7);
    game_deal(g);
    CHECK(!game_can_double(g));
    game_destroy(g);
}

// --- Splitting --------------------------------------------------------------
static void test_split_and_double_after(void) {
    Game* g = fresh();
    STACK(g, 8, 10, 8, 7, 3, 10, 10);         // 8 8 against 10 7
    game_deal(g);
    CHECK(game_can_split(g));
    game_split(g);
    CHECK(g->nhands == 2);
    CHECK(g->hands[0].n == 2 && g->hands[1].n == 1);   // second hand waits
    CHECK(g->bankroll == START_BANKROLL - 200);
    CHECK(game_can_double(g));                // double after split
    game_double(g);                           // 8 3 10 = 21
    CHECK(g->hands[0].result == RES_NONE);    // not settled until the dealer plays
    CHECK(g->active == 1);
    CHECK(g->hands[1].n == 2);                // dealt its second card on arrival
    game_stand(g);                            // 8 10 = 18
    CHECK(g->phase == PHASE_RESULT);
    CHECK(g->hands[0].result == RES_WIN && g->hands[0].delta == 200);
    CHECK(g->hands[1].result == RES_WIN && g->hands[1].delta == 100);
    CHECK(g->last_delta == 300);
    CHECK(g->bankroll == START_BANKROLL + 300);
    game_destroy(g);
}

static void test_split_ten_values(void) {
    Game* g = fresh();
    STACK(g, 10, 5, 11, 6);                   // 10 J
    game_deal(g);
    CHECK(game_can_split(g));
    game_destroy(g);

    g = fresh();
    STACK(g, 9, 5, 10, 6);                    // 9 10 is not a pair
    game_deal(g);
    CHECK(!game_can_split(g));
    game_destroy(g);
}

static void test_split_aces(void) {
    Game* g = fresh();
    STACK(g, 1, 10, 1, 7, 13, 9);             // A A against 10 7
    game_deal(g);
    game_split(g);
    CHECK(g->nhands == 2);
    CHECK(g->hands[0].n == 2 && g->hands[1].n == 2);   // one card each
    CHECK(g->phase == PHASE_RESULT);          // nothing left to play
    CHECK(!game_hand_is_blackjack(&g->hands[0]));
    CHECK(g->hands[0].result == RES_WIN);     // A K is 21, paid 1:1
    CHECK(g->hands[0].delta == DEFAULT_BET);
    CHECK(g->hands[1].result == RES_WIN);     // A 9 = 20 beats 17
    CHECK(g->bankroll == START_BANKROLL + 200);
    game_destroy(g);
}

static void test_split_limit(void) {
    Game* g = fresh();
    STACK(g, 8, 10, 8, 7, 8, 8, 8, 2, 2, 2, 2);
    game_deal(g);
    game_split(g);
    game_split(g);
    game_split(g);
    CHECK(g->nhands == MAX_HANDS);
    CHECK(g->hands[0].cards[0].rank == 8 && g->hands[0].cards[1].rank == 8);
    CHECK(!game_can_split(g));                // a pair, but four hands already
    CHECK(g->bankroll == START_BANKROLL - 4 * DEFAULT_BET);
    game_destroy(g);
}

// --- Surrender --------------------------------------------------------------
static void test_surrender(void) {
    Game* g = fresh();
    STACK(g, 10, 10, 6, 7);
    game_deal(g);
    CHECK(!game_can_surrender(g));            // off unless Options turn it on
    game_destroy(g);

    g = fresh_rules(false, true, 6);
    STACK(g, 10, 10, 6, 7);
    game_deal(g);
    CHECK(game_can_surrender(g));
    game_surrender(g);
    CHECK(g->phase == PHASE_RESULT);
    CHECK(g->hands[0].result == RES_SURRENDER);
    CHECK(g->last_delta == -DEFAULT_BET / 2);
    CHECK(g->dealer.n == 2);
    game_destroy(g);

    g = fresh_rules(false, true, 6);          // not after a split
    STACK(g, 8, 10, 8, 7, 3);
    game_deal(g);
    game_split(g);
    CHECK(!game_can_surrender(g));
    game_destroy(g);
}

// --- Betting and the bankroll -----------------------------------------------
static void test_bet_limits(void) {
    Game* g = fresh();
    game_bet_change(g, -10);
    CHECK(g->bet == MIN_BET);
    game_bet_change(g, 1);
    CHECK(g->bet == MIN_BET + BET_STEP);
    g->bankroll = 175;
    game_bet_change(g, 100);
    CHECK(g->bet == 150);                     // the most 175 covers, in steps
    CHECK(g->bet % BET_STEP == 0);
    game_destroy(g);
}

static void test_bet_clamped_after_a_loss(void) {
    Game* g = fresh();
    g->bet = 500;
    g->bankroll = g->shown_bankroll = 600;
    STACK(g, 10, 10, 6, 10);                  // 16 against 20
    game_deal(g);
    game_stand(g);
    game_next(g);
    CHECK(g->bankroll == 100);
    CHECK(g->bet == 100);
    game_destroy(g);
}

static void test_refill_is_reported(void) {
    Game* g = fresh();
    g->bet = 50;
    g->bankroll = g->shown_bankroll = 60;
    STACK(g, 10, 10, 6, 10);
    game_deal(g);
    game_stand(g);
    CHECK(g->bankroll == 10);
    game_next(g);
    CHECK(g->bankroll == START_BANKROLL);
    CHECK(g->refilled);
    game_step_begin(g);
    game_update(g);
    CHECK(g->refilled);                       // stays until main shows it
    game_destroy(g);
}

// --- Phases -----------------------------------------------------------------
static void test_actions_out_of_phase(void) {
    Game* g = fresh();
    game_hit(g); game_stand(g); game_double(g); game_split(g);
    game_surrender(g); game_insurance(g, true); game_next(g);
    CHECK(g->phase == PHASE_BET);
    CHECK(g->nhands == 0 && g->bankroll == START_BANKROLL);

    STACK(g, 10, 6, 9, 10, 2);
    CHECK(game_deal(g));
    CHECK(g->phase == PHASE_PLAYER);
    CHECK(!game_deal(g));
    int bet = g->bet;
    game_bet_change(g, 1);
    CHECK(g->bet == bet);
    game_insurance(g, true);
    CHECK(g->insurance == 0);
    game_next(g);
    CHECK(g->phase == PHASE_PLAYER);
    game_destroy(g);
}

static void test_rules_apply_at_next_deal(void) {
    Game* g = fresh();
    CHECK(g->shoe_decks == 6 && g->shoe_n == 312);
    Rules r = { .decks = 1, .h17 = true, .surrender = true };
    game_set_rules(g, r);
    CHECK(g->shoe_decks == 6 && !g->rules.h17);
    game_deal(g);
    CHECK(g->shoe_decks == 1 && g->shoe_n == 52);
    CHECK(g->rules.h17 && g->rules.surrender);
    game_destroy(g);
}

// --- What the player sees ---------------------------------------------------
static void test_hole_card_hidden(void) {
    Game* g = fresh();
    STACK(g, 10, 6, 9, 10, 2);
    game_deal(g);
    drain(g);
    CHECK(g->dealer.vis == 2);
    CHECK(!g->hole_shown);
    CHECK(game_visible_total(g, &g->dealer, NULL) == 6);   // up card only
    game_stand(g);
    CHECK(game_visible_total(g, &g->dealer, NULL) == 6);   // still until it turns
    drain(g);
    CHECK(g->hole_shown);
    CHECK(game_visible_total(g, &g->dealer, NULL) == 18);
    game_destroy(g);
}

static void test_deal_queue_order(void) {
    Game* g = fresh();
    STACK(g, 10, 6, 9, 10, 2);
    game_deal(g);
    CHECK(game_busy(g));
    CHECK(g->hands[0].vis == 0 && g->dealer.vis == 0);
    // Player, dealer, player, dealer: one card every DEAL_STEPS steps.
    int order[4][2] = { {1, 0}, {1, 1}, {2, 1}, {2, 2} };
    for (int k = 0; k < 4; k++) {
        game_step_begin(g);
        game_update(g);
        CHECK(g->events & EV_DEAL);
        CHECK(g->hands[0].vis == order[k][0] && g->dealer.vis == order[k][1]);
        for (int s = 1; s < DEAL_STEPS; s++) {
            game_step_begin(g);
            game_update(g);
            CHECK(!(g->events & EV_DEAL));
        }
    }
    CHECK(game_busy(g));                      // the last card is still landing
    game_update(g);
    CHECK(!game_busy(g));
    game_destroy(g);
}

static void test_outcome_waits_for_the_cards(void) {
    Game* g = fresh();
    STACK(g, 1, 5, 13, 6);                    // a natural, settled at the deal
    game_deal(g);
    CHECK(g->bankroll == START_BANKROLL + 150);
    CHECK(g->shown_bankroll == START_BANKROLL - DEFAULT_BET);
    CHECK(!(g->events & EV_BLACKJACK));
    unsigned ev = drain(g);
    CHECK(ev & EV_BLACKJACK);
    CHECK(g->settled_shown);
    CHECK(g->shown_bankroll == g->bankroll);
    game_destroy(g);
}

// --- The shoe ---------------------------------------------------------------
static int count_of(const Card* cards, int n, Card c) {
    int k = 0;
    for (int i = 0; i < n; i++) k += (cards[i].rank == c.rank && cards[i].suit == c.suit);
    return k;
}

static void test_shoe_contents(void) {
    for (int decks = 1; decks <= 6; decks += 5) {
        Game* g = fresh_rules(false, false, decks);
        CHECK(g->shoe_n == 52 * decks);
        for (int s = 0; s < 4; s++)
            for (int r = 1; r <= 13; r++)
                CHECK(count_of(g->shoe, g->shoe_n, (Card){ (uint8_t)r, (uint8_t)s }) == decks);
        game_destroy(g);
    }
}

static void test_reshuffle_threshold(void) {
    Game* g = fresh_rules(false, false, 1);
    g->draw = 38;                             // 14 left: the cut is at 39
    game_deal(g);
    CHECK(g->draw == 42);
    game_destroy(g);

    g = fresh_rules(false, false, 1);
    g->draw = 39;                             // 13 left: reshuffle first
    game_deal(g);
    CHECK(g->draw == 4);
    game_destroy(g);
}

// No card may appear on the table twice in one round, even when the shoe runs
// dry mid-round (the old game rebuilt all 52 cards, table included).
static bool table_is_legal(const Game* g) {
    Card all[MAX_HANDS * MAX_HAND_CARDS + MAX_HAND_CARDS];
    int n = 0;
    for (int h = 0; h < g->nhands; h++)
        for (int i = 0; i < g->hands[h].n; i++) all[n++] = g->hands[h].cards[i];
    for (int i = 0; i < g->dealer.n; i++) all[n++] = g->dealer.cards[i];
    for (int i = 0; i < n; i++)
        if (count_of(all, n, all[i]) > g->shoe_decks) return false;
    return true;
}

static void play_basic(Game* g) {
    if (g->phase == PHASE_INSURANCE) game_insurance(g, false);
    while (g->phase == PHASE_PLAYER) {
        const Hand* h = &g->hands[g->active];
        int t = hand_total(h->cards, h->n, NULL);
        if (game_can_split(g))                   game_split(g);
        else if (t == 11 && game_can_double(g))  game_double(g);
        else if (t < 17)                         game_hit(g);
        else                                     game_stand(g);
    }
}

static void test_no_duplicate_cards(void) {
    for (int decks = 1; decks <= 6; decks += 5) {
        Game* g = fresh_rules(false, false, decks);
        for (int round = 0; round < 3000; round++) {
            g->bankroll = START_BANKROLL;         // never run out mid-test
            game_deal(g);
            play_basic(g);
            CHECK(table_is_legal(g));
            game_next(g);
        }
        game_destroy(g);
    }

    // Force the shoe dry in the middle of a hand.
    Game* g = fresh_rules(false, false, 1);
    for (int round = 0; round < 500; round++) {
        g->bankroll = START_BANKROLL;
        game_deal(g);
        if (g->phase == PHASE_INSURANCE) game_insurance(g, false);
        if (g->phase == PHASE_PLAYER) {
            g->draw = g->shoe_n;                  // nothing left
            game_hit(g);                          // so this card rebuilds the shoe
            CHECK(g->short_shoe);
            play_basic(g);
        }
        CHECK(table_is_legal(g));
        game_next(g);
        game_deal(g);                             // a full shoe again
        CHECK(!g->short_shoe && g->shoe_n == 52);
        play_basic(g);
        game_next(g);
    }
    game_destroy(g);
}

static void test_same_seed_same_shoe(void) {
    Game* a = game_create(99, rules_default());
    Game* b = game_create(99, rules_default());
    CHECK(memcmp(a->shoe, b->shoe, sizeof a->shoe) == 0);
    Game* c = game_create(100, rules_default());
    CHECK(memcmp(a->shoe, c->shoe, sizeof a->shoe) != 0);
    game_destroy(a); game_destroy(b); game_destroy(c);
}

static void test_rng_below(void) {
    Game* g = fresh();
    int seen[13] = {0};
    for (int i = 0; i < 13000; i++) {
        int v = rng_below(g, 13);
        CHECK(v >= 0 && v < 13);
        if (v >= 0 && v < 13) seen[v]++;
    }
    for (int v = 0; v < 13; v++) CHECK(seen[v] > 800 && seen[v] < 1200);
    game_destroy(g);
}

// --- The fixed-timestep clock (tick.c) --------------------------------------
static void test_sim_clock(void) {
    SimClock c = {0};
    CHECK(sim_clock_advance(&c, SIM_DT) == 1);
    CHECK(c.accum == 0.0);

    c.accum = 0.0;
    CHECK(sim_clock_advance(&c, SIM_DT / 2) == 0);
    CHECK(sim_clock_advance(&c, SIM_DT / 2) == 1);

    c.accum = 0.0;
    int total = 0;
    for (int i = 0; i < 120; i++) total += sim_clock_advance(&c, SIM_DT / 2);
    CHECK(total == 60);              // a 120 Hz display still runs 60 steps/s

    c.accum = 0.0;
    CHECK(sim_clock_advance(&c, 100.0) == SIM_MAX_STEPS);   // spiral guard
    CHECK(c.accum == 0.0);

    c.accum = 0.0;
    CHECK(sim_clock_advance(&c, -1.0) == 0);                // stalled clock
    c.accum = 1.0;
    sim_clock_reset(&c);
    CHECK(c.accum == 0.0);
}

int main(void) {
    printf("test_game: rules, payouts, split, insurance, shoe, queue, clock\n");
    test_totals();
    test_blackjack_pays_3_to_2();
    test_blackjack_payout_is_whole_at_every_bet();
    test_dealer_ten_up_blackjack();
    test_even_money();
    test_insurance_wins_against_blackjack();
    test_insurance_lost();
    test_insurance_needs_the_chips();
    test_stand_and_win_after_dealer_draws();
    test_dealer_bust();
    test_push_after_standing();
    test_player_bust_dealer_does_not_draw();
    test_21_stands_by_itself();
    test_soft_17();
    test_double();
    test_double_refused();
    test_split_and_double_after();
    test_split_ten_values();
    test_split_aces();
    test_split_limit();
    test_surrender();
    test_bet_limits();
    test_bet_clamped_after_a_loss();
    test_refill_is_reported();
    test_actions_out_of_phase();
    test_rules_apply_at_next_deal();
    test_hole_card_hidden();
    test_deal_queue_order();
    test_outcome_waits_for_the_cards();
    test_shoe_contents();
    test_reshuffle_threshold();
    test_no_duplicate_cards();
    test_same_seed_same_shoe();
    test_rng_below();
    test_sim_clock();
    if (failures == 0) { printf("OK: all checks passed\n"); return 0; }
    printf("FAILED: %d check(s)\n", failures);
    return 1;
}
