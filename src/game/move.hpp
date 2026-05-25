#pragma once
#include <array>
#include <cstdint>

#include "game/board.hpp"

namespace triples::game {

// Result of resolving a swipe (NOT yet spawning a new tile). `any_change`
// reports whether the move would be accepted; if false the swipe is rejected.
//
// `line_moved[i]` is true when the i-th line (row for L/R, column for U/D)
// actually moved — used to determine where the spawn tile can appear.
//
// `origin[i]` is the source cell index of whatever ended up at cell i after
// the move; -1 means "this cell is empty in the new board". For merged
// destinations, `origin` is the *leading* contributor (the one closer to
// the destination edge); `merge_partner_origin[i]` gives the trailing
// contributor (or -1 if no merge happened at i). This lets the renderer
// animate two tiles overlapping into one during the commit animation.
struct MoveResult {
    Board                       new_board{};
    bool                        any_change = false;
    std::array<bool, 4>         line_moved{};                // indexed by row (L/R) or col (U/D)
    std::array<std::int8_t, 16> origin{};                    // -1 = empty in new_board
    std::array<std::int8_t, 16> merge_partner_origin{};      // -1 = no merge at this dest

    MoveResult() {
        origin.fill(-1);
        merge_partner_origin.fill(-1);
    }
};

MoveResult resolve(const Board& b, Direction dir) noexcept;

// True if at least one direction would produce a change.
bool any_move_possible(const Board& b) noexcept;

}  // namespace triples::game
