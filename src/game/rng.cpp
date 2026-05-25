#include "game/rng.hpp"

namespace triples::game {

namespace {
inline std::uint64_t rotl(std::uint64_t x, int k) noexcept {
    return (x << k) | (x >> (64 - k));
}
inline std::uint64_t splitmix64(std::uint64_t& x) noexcept {
    x += 0x9E3779B97F4A7C15ull;
    std::uint64_t z = x;
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    return z ^ (z >> 31);
}
}  // namespace

Rng::Rng(std::uint64_t seed) noexcept {
    std::uint64_t x = seed ? seed : 0x12345678ABCDEF01ull;
    s[0] = splitmix64(x);
    s[1] = splitmix64(x);
    s[2] = splitmix64(x);
    s[3] = splitmix64(x);
}

std::uint64_t Rng::next_u64() noexcept {
    const std::uint64_t result = rotl(s[1] * 5ull, 7) * 9ull;
    const std::uint64_t t = s[1] << 17;
    s[2] ^= s[0];
    s[3] ^= s[1];
    s[1] ^= s[2];
    s[0] ^= s[3];
    s[2] ^= t;
    s[3] = rotl(s[3], 45);
    return result;
}

std::uint32_t Rng::uniform(std::uint32_t n) noexcept {
    // Lemire's nearly divisionless method, using 32-bit RNG output × n into 64.
    std::uint32_t x = static_cast<std::uint32_t>(next_u64());
    std::uint64_t m = static_cast<std::uint64_t>(x) * static_cast<std::uint64_t>(n);
    std::uint32_t l = static_cast<std::uint32_t>(m);
    if (l < n) {
        std::uint32_t t = static_cast<std::uint32_t>(-n) % n;
        while (l < t) {
            x = static_cast<std::uint32_t>(next_u64());
            m = static_cast<std::uint64_t>(x) * static_cast<std::uint64_t>(n);
            l = static_cast<std::uint32_t>(m);
        }
    }
    return static_cast<std::uint32_t>(m >> 32);
}

}  // namespace triples::game
