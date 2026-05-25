#include "test_main.hpp"

#include "game/serialize.hpp"

using namespace triples::game;

TEST(serialize_roundtrip_initial_state) {
    GameState s = initial_state(0xDEADBEEFull);
    std::string text = serialize_state(s);
    auto back = deserialize_state(text);
    CHECK(back.has_value());
    CHECK(back->board == s.board);
    CHECK_EQ(back->score, s.score);
    CHECK_EQ(back->max_rank_seen, s.max_rank_seen);
    CHECK_EQ(back->deck.count, s.deck.count);
    CHECK_EQ(back->next.rank, s.next.rank);
    CHECK_EQ(back->next.is_bonus, s.next.is_bonus);
    for (int i = 0; i < 4; ++i) CHECK_EQ(back->rng.s[i], s.rng.s[i]);
}

TEST(serialize_rejects_garbage) {
    CHECK(!deserialize_state("not a game").has_value());
    CHECK(!deserialize_state("").has_value());
    CHECK(!deserialize_state("v2 0 0 0 0").has_value());
}

TEST(highscore_roundtrip) {
    HighScore in[3] = {
        {1234, "2026-05-24"},
        {567,  "2026-05-23"},
        {0,    "2026-05-22"},
    };
    std::string text = serialize_highscores(in, 3);
    auto back = deserialize_highscores(text);
    CHECK_EQ(back.size(), std::size_t{3});
    if (back.size() == 3) {
        CHECK_EQ(back[0].score, 1234u);
        CHECK(back[0].date == "2026-05-24");
        CHECK_EQ(back[2].score, 0u);
    }
}
