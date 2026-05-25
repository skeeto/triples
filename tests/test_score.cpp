#include "test_main.hpp"

#include "game/score.hpp"

using namespace triples::game;

TEST(score_per_tile_table) {
    CHECK_EQ(kTileScores[0], 0u);
    CHECK_EQ(kTileScores[1], 0u);
    CHECK_EQ(kTileScores[2], 0u);
    CHECK_EQ(kTileScores[3], 3u);
    CHECK_EQ(kTileScores[4], 9u);
    CHECK_EQ(kTileScores[5], 27u);
    CHECK_EQ(kTileScores[6], 81u);
    CHECK_EQ(kTileScores[7], 243u);
    CHECK_EQ(kTileScores[14], 531441u);
}

TEST(score_board_sums_white_only) {
    Board b{};
    b.set(0, 0, 1);   // blue: 0 points
    b.set(0, 1, 2);   // red: 0 points
    b.set(0, 2, 3);   // 3 points
    b.set(1, 0, 4);   // 9 points
    b.set(1, 1, 6);   // 81 points
    CHECK_EQ(score_board(b), 0u + 0u + 3u + 9u + 81u);
}
