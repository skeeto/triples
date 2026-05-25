#pragma once
#include <cstdint>

#include "game/board.hpp"

namespace triples::game {

// Sum of kTileScores[cell] over the board. Blue (rank 1) and red (rank 2) tiles
// contribute zero.
std::uint64_t score_board(const Board& b) noexcept;

}  // namespace triples::game
