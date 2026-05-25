#include "test_main.hpp"

#include "game/deck.hpp"

using namespace triples::game;

TEST(deck_initial_composition) {
    Rng rng(123);
    Deck d;
    d.refill_and_shuffle(rng);
    int count1 = 0, count2 = 0, count3 = 0;
    for (int i = 0; i < 12; ++i) {
        if (d.cards[i] == 1) ++count1;
        else if (d.cards[i] == 2) ++count2;
        else if (d.cards[i] == 3) ++count3;
    }
    CHECK_EQ(count1, 4);
    CHECK_EQ(count2, 4);
    CHECK_EQ(count3, 4);
}

TEST(deck_draws_exactly_twelve_then_reshuffles) {
    Rng rng(42);
    Deck d;
    d.refill_and_shuffle(rng);
    int draws[3] = {0, 0, 0};
    for (int i = 0; i < 12; ++i) {
        std::uint8_t v = d.draw(rng);
        CHECK(v >= 1 && v <= 3);
        ++draws[v - 1];
    }
    CHECK_EQ(draws[0], 4);
    CHECK_EQ(draws[1], 4);
    CHECK_EQ(draws[2], 4);
    CHECK_EQ(d.remaining(), 0);
    // 13th draw triggers reshuffle and yields a valid card.
    std::uint8_t v = d.draw(rng);
    CHECK(v >= 1 && v <= 3);
    CHECK_EQ(d.remaining(), 11);
}

TEST(deck_different_seeds_produce_different_orders) {
    Rng a(1), b(2);
    Deck da, db;
    da.refill_and_shuffle(a);
    db.refill_and_shuffle(b);
    bool any_diff = false;
    for (int i = 0; i < 12; ++i) if (da.cards[i] != db.cards[i]) { any_diff = true; break; }
    CHECK(any_diff);
}
