#pragma once
#include <cstdint>

namespace triples::game {

// Tiny deterministic RNG (xoshiro256**). Sized so the entire seed fits in
// a serialized state line. Public state for serialize/deserialize.
struct Rng {
    std::uint64_t s[4];

    explicit Rng(std::uint64_t seed = 0xC0FFEEull) noexcept;

    std::uint64_t next_u64() noexcept;
    // Uniform integer in [0, n). n must be > 0.
    std::uint32_t uniform(std::uint32_t n) noexcept;
};

}  // namespace triples::game
