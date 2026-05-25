#include "game/score.hpp"

namespace triples::game {

std::uint64_t score_board(const Board& b) noexcept {
    std::uint64_t s = 0;
    for (auto v : b.cells) s += kTileScores[v];
    return s;
}

}  // namespace triples::game
