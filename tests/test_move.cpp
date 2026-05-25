#include "test_main.hpp"

#include "game/move.hpp"

using namespace triples::game;

namespace {

Board make_board(std::array<std::uint8_t, 16> cells) {
    Board b;
    b.cells = cells;
    return b;
}

bool board_eq(const Board& b, std::array<std::uint8_t, 16> expected) {
    return b.cells == expected;
}

}  // namespace

TEST(move_one_two_slides_right_no_merge_yet) {
    // [1,2,0,0] right → tiles slide by 1 → [0,1,2,0]. Merge only happens at the
    // leading edge once they reach it on a later swipe.
    Board b = make_board({1,2,0,0,  0,0,0,0,  0,0,0,0,  0,0,0,0});
    MoveResult r = resolve(b, Direction::Right);
    CHECK(r.any_change);
    CHECK(board_eq(r.new_board, {0,1,2,0, 0,0,0,0, 0,0,0,0, 0,0,0,0}));
}

TEST(move_one_two_at_leading_merges_to_three) {
    // [0,0,1,2] right → leading pair (cell[3]=2, cell[2]=1) is 2+1, merges to 3.
    Board b = make_board({0,0,1,2,  0,0,0,0,  0,0,0,0,  0,0,0,0});
    MoveResult r = resolve(b, Direction::Right);
    CHECK(r.any_change);
    CHECK(board_eq(r.new_board, {0,0,0,3, 0,0,0,0, 0,0,0,0, 0,0,0,0}));
}

TEST(move_three_threes_right_no_chained_merges) {
    // Only the leading pair merges. Board stores RANKS — rank 3 has face value 3,
    // rank 4 has face value 6. So all-rank-3 row [3,3,3,3] right → [0,3,3,4]
    // (values "0 3 3 6").
    Board b = make_board({3,3,3,3,  0,0,0,0,  0,0,0,0,  0,0,0,0});
    MoveResult r = resolve(b, Direction::Right);
    CHECK(r.any_change);
    CHECK(board_eq(r.new_board, {0,3,3,4, 0,0,0,0, 0,0,0,0, 0,0,0,0}));
}

TEST(move_one_one_blocks_stuck) {
    // 1+1 is illegal in Threes — leading pair is stuck.
    Board b = make_board({1,1,0,0,  0,0,0,0,  0,0,0,0,  0,0,0,0});
    // Right: leading pair (cell[3]=0, cell[2]=0), both empty, but 1s are there. Row moves.
    // Left: leading pair (cell[0]=1, cell[1]=1) — 1+1 invalid → STUCK on left swipe.
    MoveResult lr = resolve(b, Direction::Left);
    CHECK(!lr.line_moved[0]);
}

TEST(move_empty_board_rejected_all_directions) {
    Board b{};
    for (auto d : { Direction::Up, Direction::Down, Direction::Left, Direction::Right }) {
        MoveResult r = resolve(b, d);
        CHECK(!r.any_change);
    }
}

TEST(move_jammed_row_is_stuck) {
    // [3,6,12,24] right: ranks [3,4,5,6]. No two adjacent ranks are equal and
    // no 1+2 pair exists, so no merge anywhere in the row. STUCK.
    Board b = make_board({3,4,5,6,  0,0,0,0,  0,0,0,0,  0,0,0,0});
    MoveResult r = resolve(b, Direction::Right);
    CHECK(!r.line_moved[0]);
}

TEST(move_mid_row_merge_when_leading_jammed) {
    // [_, 3, 3, 6] right: leading pair (rank 4 = 6, rank 3) can't merge, but
    // the inner 3+3 can. Result: [_, _, 6, 6] (ranks [_, _, 4, 4]).
    Board b = make_board({0,3,3,4,  0,0,0,0,  0,0,0,0,  0,0,0,0});
    MoveResult r = resolve(b, Direction::Right);
    CHECK(r.any_change);
    CHECK(board_eq(r.new_board, {0,0,4,4, 0,0,0,0, 0,0,0,0, 0,0,0,0}));
}

TEST(move_back_row_one_two_merge_through_jam) {
    // [1, 2, 3, 6] right: leading pair (4, 3) can't merge, (3, 2) can't, but
    // (2, 1) can. Result: [_, 3, 3, 6] (ranks [_, 3, 3, 4]).
    Board b = make_board({1,2,3,4,  0,0,0,0,  0,0,0,0,  0,0,0,0});
    MoveResult r = resolve(b, Direction::Right);
    CHECK(r.any_change);
    CHECK(board_eq(r.new_board, {0,3,3,4, 0,0,0,0, 0,0,0,0, 0,0,0,0}));
}

TEST(move_only_one_merge_per_line) {
    // [3, 3, 6, 6] right: leading 6+6 merges to 12. Inner 3+3 does NOT
    // merge because we already used the per-line merge slot. The 3s just slide.
    // Result: [_, 3, 3, 12] (ranks [_, 3, 3, 5]).
    Board b = make_board({3,3,4,4,  0,0,0,0,  0,0,0,0,  0,0,0,0});
    MoveResult r = resolve(b, Direction::Right);
    CHECK(r.any_change);
    CHECK(board_eq(r.new_board, {0,3,3,5, 0,0,0,0, 0,0,0,0, 0,0,0,0}));
}

TEST(move_vertical_works) {
    // Column-major: a value of 1 at (0,0), 2 at (1,0). Swipe down:
    // leading edge is row 3. Column [1,2,0,0] processed top→bottom acts like a
    // forward slide: leading pair (cell[3]=0, cell[2]=0) → both empty; we just
    // shift everyone down by 1 → [0,1,2,0]_col. So cells (1,0)=1 and (2,0)=2.
    Board b{};
    b.set(0, 0, 1);
    b.set(1, 0, 2);
    MoveResult r = resolve(b, Direction::Down);
    CHECK(r.any_change);
    CHECK_EQ(r.new_board.at(0, 0), 0);
    CHECK_EQ(r.new_board.at(1, 0), 1);
    CHECK_EQ(r.new_board.at(2, 0), 2);
    CHECK_EQ(r.new_board.at(3, 0), 0);
}

TEST(move_origin_tracks_merging_partners) {
    // [0,0,3,3] right: leading pair merges; origin[3] is 3 (the leader),
    // merge_partner_origin[3] is 2 (the trailer).
    Board b = make_board({0,0,3,3,  0,0,0,0,  0,0,0,0,  0,0,0,0});
    MoveResult r = resolve(b, Direction::Right);
    CHECK(r.any_change);
    CHECK_EQ(static_cast<int>(r.origin[3]), 3);
    CHECK_EQ(static_cast<int>(r.merge_partner_origin[3]), 2);
}

TEST(move_full_board_with_no_merges_is_game_over) {
    // Alternating non-mergeable tiles. 1,2,1,2 / 2,1,2,1 / 1,2,1,2 / 2,1,2,1
    // Adjacent are always (1,2) which IS mergeable. So game NOT over.
    // Use ranks 3 and 4 instead which don't merge: 3+4 → no merge.
    Board b = make_board({3,4,3,4,  4,3,4,3,  3,4,3,4,  4,3,4,3});
    CHECK(!any_move_possible(b));
}

TEST(move_two_into_one_right) {
    // [_,_,2,1] right: leading pair (cell[3]=1, cell[2]=2). 1+2 valid merge → 3.
    Board b = make_board({0,0,2,1,  0,0,0,0,  0,0,0,0,  0,0,0,0});
    MoveResult r = resolve(b, Direction::Right);
    CHECK(r.any_change);
    CHECK(board_eq(r.new_board, {0,0,0,3, 0,0,0,0, 0,0,0,0, 0,0,0,0}));
}

TEST(move_two_into_one_left) {
    // [2,1,_,_] left: leading pair (cell[0]=2, cell[1]=1) merges to 3.
    Board b = make_board({2,1,0,0,  0,0,0,0,  0,0,0,0,  0,0,0,0});
    MoveResult r = resolve(b, Direction::Left);
    CHECK(r.any_change);
    CHECK(board_eq(r.new_board, {3,0,0,0, 0,0,0,0, 0,0,0,0, 0,0,0,0}));
}

TEST(move_two_into_one_down) {
    // Column [_,_,2,1] down → [_,_,_,3] (col 0).
    Board b{};
    b.set(2, 0, 2);
    b.set(3, 0, 1);
    MoveResult r = resolve(b, Direction::Down);
    CHECK(r.any_change);
    CHECK_EQ(r.new_board.at(3, 0), 3);
    CHECK_EQ(r.new_board.at(2, 0), 0);
}

TEST(move_two_into_one_up) {
    // Column [2,1,_,_] up → [3,_,_,_] (col 0).
    Board b{};
    b.set(0, 0, 2);
    b.set(1, 0, 1);
    MoveResult r = resolve(b, Direction::Up);
    CHECK(r.any_change);
    CHECK_EQ(r.new_board.at(0, 0), 3);
    CHECK_EQ(r.new_board.at(1, 0), 0);
}

TEST(move_partial_row_slides_without_merge) {
    // [3,0,3,0] right → after shift by 1 → [0,3,0,3]. Hmm let's think:
    // leading pair (cell[3]=0, cell[2]=3): slide. cell[3]=3, cell[2]=0.
    // Rest shifts right by 1: cell[2]=cell[1]=0, cell[1]=cell[0]=3, cell[0]=0.
    // Result: [0,3,0,3].
    Board b = make_board({3,0,3,0,  0,0,0,0,  0,0,0,0,  0,0,0,0});
    MoveResult r = resolve(b, Direction::Right);
    CHECK(r.any_change);
    CHECK(board_eq(r.new_board, {0,3,0,3, 0,0,0,0, 0,0,0,0, 0,0,0,0}));
}
