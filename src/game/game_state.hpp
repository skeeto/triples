#pragma once
#include <array>
#include <cstdint>
#include <optional>

#include "game/board.hpp"
#include "game/deck.hpp"
#include "game/move.hpp"
#include "game/rng.hpp"

namespace triples::game {

// Specification of the next tile to spawn. If is_bonus is true, the value is
// chosen and `rank` is set; the preview just shows "white + plus" and does
// NOT reveal the value. Otherwise `rank` is one of 1, 2, 3 (deck draw).
struct NextTile {
    std::uint8_t rank = 0;
    bool         is_bonus = false;
};

struct GameState {
    Board       board{};
    Deck        deck{};
    NextTile    next{};
    std::uint8_t max_rank_seen = 0;  // tracks the historical max for bonus eligibility
    std::uint64_t score = 0;
    Rng         rng{};

    bool is_game_over() const noexcept;
};

// Build a fresh game by drawing 9 cards from a new bag and placing them on 9
// random cells.
GameState initial_state(std::uint64_t seed) noexcept;

// Apply the result of `move::resolve(state.board, dir)` to `state`, spawning
// the next-tile at the chosen edge cell and recomputing the next-tile preview
// for the upcoming move. Updates score and max_rank_seen. Does nothing if
// `mr.any_change` is false.
//
// Returns the cell index where the new tile was spawned (or -1 if none).
int apply_move(GameState& state, Direction dir, const MoveResult& mr) noexcept;

// Pick a bonus rank from the sliding-window distribution. Only valid when
// `max_rank_seen >= 7`.
std::uint8_t pick_bonus_rank(Rng& rng, std::uint8_t max_rank_seen) noexcept;

}  // namespace triples::game
