#pragma once
#include <array>
#include <cstdint>

#include "game/rng.hpp"

namespace triples::game {

// 12-card "bag": four rank-1, four rank-2, four rank-3. Refilled and reshuffled
// on exhaustion. Order is permuted via the supplied RNG.
struct Deck {
    std::array<std::uint8_t, 12> cards{};
    std::uint8_t                 head = 0;   // index into cards; cards[head..12) are remaining
    std::uint8_t                 count = 0;  // == 12 - head, kept explicit for serialization

    void refill_and_shuffle(Rng& rng) noexcept;
    std::uint8_t draw(Rng& rng) noexcept;     // returns a rank (1..3), refills if empty
    std::uint8_t peek(Rng& rng) noexcept;     // peek next without consuming; refills if empty
    int          remaining()    const noexcept { return count; }
};

}  // namespace triples::game
