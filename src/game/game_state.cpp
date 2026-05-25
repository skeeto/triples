#include "game/game_state.hpp"

#include "game/score.hpp"

namespace triples::game {

namespace {

// Per nneonneo's threes-ai: once the max rank reaches 7 (value 48), each
// spawn has a 1/21 chance to be a bonus tile.
constexpr std::uint32_t kBonusFreq = 21;

NextTile compute_next_tile(Rng& rng, Deck& deck, std::uint8_t max_rank_seen) noexcept {
    NextTile n;
    if (max_rank_seen >= kBonusThresholdMaxRank && rng.uniform(kBonusFreq) == 0) {
        n.is_bonus = true;
        n.rank = pick_bonus_rank(rng, max_rank_seen);
    } else {
        n.is_bonus = false;
        n.rank = deck.draw(rng);
    }
    return n;
}

}  // namespace

std::uint8_t pick_bonus_rank(Rng& rng, std::uint8_t max_rank_seen) noexcept {
    if (max_rank_seen <= 7) return 4;                          // value 6 only
    if (max_rank_seen == 8) return static_cast<std::uint8_t>(4 + rng.uniform(2));  // {6, 12}
    // max_rank_seen >= 9 → three consecutive ranks within {4 .. max-3}.
    std::uint32_t windows = static_cast<std::uint32_t>(max_rank_seen) - 8u;  // # of windows
    std::uint32_t offset = rng.uniform(windows);
    std::uint32_t within = rng.uniform(3);
    return static_cast<std::uint8_t>(4 + offset + within);
}

bool GameState::is_game_over() const noexcept {
    // The hidden 13th tile (rank 15, value 12,288) ends the game immediately,
    // even if other moves remain. Per the rules doc.
    for (auto v : board.cells) if (v == 15) return true;
    return !any_move_possible(board);
}

GameState initial_state(std::uint64_t seed) noexcept {
    GameState s;
    s.rng = Rng(seed);
    s.deck.refill_and_shuffle(s.rng);

    // Choose 9 distinct cells uniformly at random by Fisher-Yates over [0,16).
    std::array<std::uint8_t, 16> idx;
    for (int i = 0; i < 16; ++i) idx[i] = static_cast<std::uint8_t>(i);
    for (int j = 15; j > 0; --j) {
        std::uint32_t k = s.rng.uniform(static_cast<std::uint32_t>(j + 1));
        std::swap(idx[j], idx[k]);
    }

    std::uint8_t max_rank = 0;
    for (int i = 0; i < 9; ++i) {
        std::uint8_t v = s.deck.draw(s.rng);
        s.board.cells[idx[i]] = v;
        if (v > max_rank) max_rank = v;
    }
    s.max_rank_seen = max_rank;
    s.score = score_board(s.board);
    s.next = compute_next_tile(s.rng, s.deck, s.max_rank_seen);
    return s;
}

namespace {

// Returns the destination cell index for the spawn given the swipe direction
// and the edge slot index (0..3 along the edge). For example, swipe Right
// means the spawn lands on the *left* edge (column 0).
int spawn_cell_for(Direction dir, int edge_slot) {
    switch (dir) {
        case Direction::Right: return edge_slot * 4 + 0;   // left edge
        case Direction::Left:  return edge_slot * 4 + 3;   // right edge
        case Direction::Down:  return 0 * 4 + edge_slot;   // top edge
        case Direction::Up:    return 3 * 4 + edge_slot;   // bottom edge
    }
    return -1;
}

}  // namespace

int apply_move(GameState& state, Direction dir, const MoveResult& mr) noexcept {
    if (!mr.any_change) return -1;

    state.board = mr.new_board;

    // Pick a moved line uniformly at random; the spawn lands in the edge
    // cell of that line.
    int moved_count = 0;
    int moved_lines[4];
    for (int i = 0; i < 4; ++i) {
        if (mr.line_moved[i]) moved_lines[moved_count++] = i;
    }
    int spawn_cell = -1;
    if (moved_count > 0) {
        int chosen = (moved_count == 1)
            ? moved_lines[0]
            : moved_lines[state.rng.uniform(static_cast<std::uint32_t>(moved_count))];
        spawn_cell = spawn_cell_for(dir, chosen);
        state.board.cells[spawn_cell] = state.next.rank;
    }

    if (state.next.rank > state.max_rank_seen) state.max_rank_seen = state.next.rank;
    if (state.board.max_rank() > state.max_rank_seen) state.max_rank_seen = state.board.max_rank();

    state.score = score_board(state.board);
    state.next = compute_next_tile(state.rng, state.deck, state.max_rank_seen);
    return spawn_cell;
}

}  // namespace triples::game
