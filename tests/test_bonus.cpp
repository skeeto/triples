#include "test_main.hpp"

#include "game/game_state.hpp"

using namespace triples::game;

TEST(bonus_at_max_7_always_rank_4) {
    Rng rng(99);
    for (int i = 0; i < 200; ++i) {
        std::uint8_t r = pick_bonus_rank(rng, 7);
        CHECK_EQ(static_cast<int>(r), 4);
    }
}

TEST(bonus_at_max_8_uniform_4_or_5) {
    Rng rng(7);
    int got[16] = {0};
    for (int i = 0; i < 4000; ++i) {
        std::uint8_t r = pick_bonus_rank(rng, 8);
        CHECK(r >= 4 && r <= 5);
        ++got[r];
    }
    CHECK(got[4] > 1500);
    CHECK(got[5] > 1500);
    CHECK_EQ(got[6], 0);
}

TEST(bonus_at_max_9_in_range_4_6) {
    Rng rng(33);
    int got[16] = {0};
    for (int i = 0; i < 3000; ++i) {
        std::uint8_t r = pick_bonus_rank(rng, 9);
        CHECK(r >= 4 && r <= 6);
        ++got[r];
    }
    CHECK(got[4] > 500);
    CHECK(got[5] > 500);
    CHECK(got[6] > 500);
}

TEST(bonus_at_max_14_capped_at_rank_11) {
    Rng rng(101);
    int got[16] = {0};
    for (int i = 0; i < 10000; ++i) {
        std::uint8_t r = pick_bonus_rank(rng, 14);
        CHECK(r >= 4 && r <= 11);  // rank 11 == value 768, which is 6144/8
        ++got[r];
    }
    for (int k = 4; k <= 11; ++k) CHECK(got[k] > 100);
    CHECK_EQ(got[12], 0);
}
