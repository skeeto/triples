#include "game/board.hpp"

namespace triples::game {

std::uint8_t merge_result(std::uint8_t a, std::uint8_t b) noexcept {
    if (a == 0 || b == 0) return 0;
    if ((a == 1 && b == 2) || (a == 2 && b == 1)) return 3;
    if (a >= 3 && a == b && a < kMaxRank) return static_cast<std::uint8_t>(a + 1);
    return 0;
}

bool Board::is_full() const noexcept {
    for (auto v : cells) if (v == 0) return false;
    return true;
}

int Board::count_empty() const noexcept {
    int n = 0;
    for (auto v : cells) if (v == 0) ++n;
    return n;
}

std::uint8_t Board::max_rank() const noexcept {
    std::uint8_t m = 0;
    for (auto v : cells) if (v > m) m = v;
    return m;
}

}  // namespace triples::game
