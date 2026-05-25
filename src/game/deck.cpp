#include "game/deck.hpp"

namespace triples::game {

void Deck::refill_and_shuffle(Rng& rng) noexcept {
    int i = 0;
    for (int k = 0; k < 4; ++k) cards[i++] = 1;
    for (int k = 0; k < 4; ++k) cards[i++] = 2;
    for (int k = 0; k < 4; ++k) cards[i++] = 3;
    // Fisher-Yates.
    for (int j = 11; j > 0; --j) {
        std::uint32_t k = rng.uniform(static_cast<std::uint32_t>(j + 1));
        std::uint8_t tmp = cards[j];
        cards[j] = cards[k];
        cards[k] = tmp;
    }
    head = 0;
    count = 12;
}

std::uint8_t Deck::draw(Rng& rng) noexcept {
    if (count == 0) refill_and_shuffle(rng);
    std::uint8_t v = cards[head++];
    --count;
    return v;
}

std::uint8_t Deck::peek(Rng& rng) noexcept {
    if (count == 0) refill_and_shuffle(rng);
    return cards[head];
}

}  // namespace triples::game
