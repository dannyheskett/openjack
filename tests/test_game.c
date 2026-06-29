// Unit tests for openjack's game logic — no raylib. game.c is included directly.
// Built and run by `make test`; a non-zero exit means a failure.
#include "../src/game.c"
#include <stdio.h>
#include <assert.h>

#define PASS(name) printf("PASS: %s\n", name)
#define FAIL(name, msg) do { fprintf(stderr, "FAIL: %s — %s\n", name, msg); exit(1); } while (0)

static Card mk(int rank) { return (Card){ (uint8_t)rank, 0 }; }

// Overwrite the deck with a known order so deals are deterministic.
static void stack(Game* g, const int* ranks, int n) {
    for (int i = 0; i < n; i++) g->deck[i] = mk(ranks[i]);
    g->draw = 0;
}

static void test_hand_value(void) {
    bool soft;
    Card h1[] = { mk(1), mk(6) };                 // A,6 = 17 soft
    if (hand_value(h1, 2, &soft) != 17 || !soft) FAIL("value", "A6 should be soft 17");
    Card h2[] = { mk(1), mk(6), mk(10) };         // A,6,10 = 17 hard
    if (hand_value(h2, 3, &soft) != 17 || soft)  FAIL("value", "A6T should be hard 17");
    Card h3[] = { mk(1), mk(1) };                 // A,A = 12
    if (hand_value(h3, 2, NULL) != 12) FAIL("value", "AA should be 12");
    Card h4[] = { mk(1), mk(1), mk(9) };          // A,A,9 = 21
    if (hand_value(h4, 3, NULL) != 21) FAIL("value", "AA9 should be 21");
    Card h5[] = { mk(13), mk(12), mk(5) };        // K,Q,5 = 25 bust
    if (hand_value(h5, 3, NULL) != 25) FAIL("value", "KQ5 should be 25");
    if (!is_blackjack((Card[]){ mk(1), mk(13) }, 2)) FAIL("value", "AK should be blackjack");
    if (is_blackjack((Card[]){ mk(7), mk(7), mk(7) }, 3)) FAIL("value", "777 is not a natural");
    PASS("hand_value");
}

static void test_blackjack_payout(void) {
    Game* g = game_create();
    int ranks[] = { 1, 5, 13, 6 };               // player A,K (BJ); dealer 5,6
    stack(g, ranks, 4);
    g->bet = 100;
    game_deal(g);
    if (g->result != RES_BLACKJACK) FAIL("bj", "expected blackjack");
    if (g->last_delta != 150) FAIL("bj", "blackjack should pay 3:2 (150 on 100)");
    if (g->bankroll != START_BANKROLL + 150) FAIL("bj", "bankroll wrong after bj");
    game_destroy(g);
    PASS("blackjack_payout");
}

static void test_push_on_double_natural(void) {
    Game* g = game_create();
    int ranks[] = { 1, 1, 13, 13 };              // both A,K
    stack(g, ranks, 4);
    g->bet = 100;
    game_deal(g);
    if (g->result != RES_PUSH || g->last_delta != 0) FAIL("push", "two naturals should push");
    if (g->bankroll != START_BANKROLL) FAIL("push", "push should return the bet");
    game_destroy(g);
    PASS("push_natural");
}

static void test_normal_win(void) {
    Game* g = game_create();
    int ranks[] = { 13, 10, 12, 7 };             // player K,Q=20; dealer 10,7=17
    stack(g, ranks, 4);
    g->bet = 50;
    game_deal(g);
    if (g->phase != PHASE_PLAYER) FAIL("win", "should be player's turn");
    game_stand(g);
    if (g->result != RES_WIN || g->last_delta != 50) FAIL("win", "20 beats 17");
    if (g->dn != 2) FAIL("win", "dealer should stand on 17");
    if (g->bankroll != START_BANKROLL + 50) FAIL("win", "bankroll wrong after win");
    game_destroy(g);
    PASS("normal_win");
}

static void test_player_bust(void) {
    Game* g = game_create();
    int ranks[] = { 13, 10, 12, 7, 5 };          // player K,Q then hit 5 -> 25
    stack(g, ranks, 5);
    g->bet = 50;
    game_deal(g);
    game_hit(g);
    if (g->result != RES_BUST || g->last_delta != -50) FAIL("bust", "player should bust");
    if (g->bankroll != START_BANKROLL - 50) FAIL("bust", "bust should lose the bet");
    game_destroy(g);
    PASS("player_bust");
}

static void test_dealer_draws_and_soft17(void) {
    // Dealer draws when below 17.
    Game* g = game_create();
    int ranks[] = { 13, 10, 12, 2, 5 };          // player 20; dealer 10,2=12 -> draws 5 -> 17
    stack(g, ranks, 5);
    g->bet = 50;
    game_deal(g); game_stand(g);
    if (g->dn != 3 || game_dealer_value(g) != 17) FAIL("dealer", "dealer should draw to 17");
    if (g->result != RES_WIN) FAIL("dealer", "20 should beat drawn 17");
    game_destroy(g);

    // Dealer stands on soft 17 (A,6) and does not draw.
    Game* h = game_create();
    int r2[] = { 10, 1, 8, 6 };                  // player 18; dealer A,6 = soft 17
    stack(h, r2, 4);
    h->bet = 50;
    game_deal(h); game_stand(h);
    if (h->dn != 2) FAIL("dealer", "dealer should stand on soft 17");
    if (h->result != RES_WIN) FAIL("dealer", "18 should beat soft 17");
    game_destroy(h);
    PASS("dealer_rule");
}

static void test_double(void) {
    Game* g = game_create();
    int ranks[] = { 5, 10, 6, 8, 9 };            // player 11, double draws 9 -> 20; dealer 18
    stack(g, ranks, 5);
    g->bet = 50;
    game_deal(g);
    game_double(g);
    if (!g->doubled) FAIL("double", "should be doubled");
    if (g->result != RES_WIN) FAIL("double", "20 beats 18");
    if (g->last_delta != 100) FAIL("double", "doubled win should pay the doubled wager");
    if (g->bankroll != START_BANKROLL + 100) FAIL("double", "bankroll wrong after doubled win");
    game_destroy(g);
    PASS("double");
}

static void test_bet_and_refill(void) {
    Game* g = game_create();
    g->bet = MIN_BET;
    game_bet_change(g, -BET_STEP);
    if (g->bet != MIN_BET) FAIL("bet", "bet should clamp at MIN_BET");
    g->bankroll = 0; g->phase = PHASE_RESULT;
    game_next(g);
    if (g->bankroll != START_BANKROLL || !g->refilled) FAIL("bet", "broke player should be refilled");
    game_destroy(g);
    PASS("bet_refill");
}

int main(void) {
    test_hand_value();
    test_blackjack_payout();
    test_push_on_double_natural();
    test_normal_win();
    test_player_bust();
    test_dealer_draws_and_soft17();
    test_double();
    test_bet_and_refill();
    printf("\nAll tests passed.\n");
    return 0;
}
