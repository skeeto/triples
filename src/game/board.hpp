#pragma once
#include <array>
#include <cstdint>

namespace triples::game {

// Tile rank → face value lookup.
// 0=empty, 1=1, 2=2, 3=3, 4=6, 5=12, 6=24, 7=48, 8=96, 9=192,
// 10=384, 11=768, 12=1536, 13=3072, 14=6144, 15=12288 (hidden).
inline constexpr std::array<std::uint32_t, 16> kTileValues = {
    0, 1, 2, 3, 6, 12, 24, 48, 96, 192, 384, 768, 1536, 3072, 6144, 12288,
};

// Score per tile by rank (3^(k-2) for k >= 3; 0 otherwise).
inline constexpr std::array<std::uint32_t, 16> kTileScores = {
    0, 0, 0, 3, 9, 27, 81, 243, 729, 2187, 6561, 19683, 59049, 177147, 531441, 1594323,
};

inline constexpr std::uint8_t kMaxRank = 15;          // rank of the hidden 12288
inline constexpr std::uint8_t kBonusThresholdMaxRank = 7;  // value 48 → bonus tiles unlock

enum class Direction : std::uint8_t { Up, Down, Left, Right };

// 4x4 grid stored row-major. cells[row * 4 + col].
struct Board {
    std::array<std::uint8_t, 16> cells{};

    std::uint8_t at(int r, int c) const noexcept { return cells[r * 4 + c]; }
    void         set(int r, int c, std::uint8_t v) noexcept { cells[r * 4 + c] = v; }

    bool   is_empty(int r, int c) const noexcept { return at(r, c) == 0; }
    bool   is_full()              const noexcept;
    int    count_empty()          const noexcept;
    std::uint8_t max_rank()       const noexcept;
    bool   operator==(const Board& other) const noexcept { return cells == other.cells; }
};

// Merge rules (per Threes!):
//   rank 1 + rank 2  → rank 3  (commutative)
//   rank n + rank n  → rank n+1  for n >= 3 (must be multiple-of-3 face values)
// Rank 14 + rank 14 produces rank 15 (the hidden 12,288) and ends the game.
// Returns 0 (invalid) if the pair cannot merge.
std::uint8_t merge_result(std::uint8_t a, std::uint8_t b) noexcept;

inline bool can_merge(std::uint8_t a, std::uint8_t b) noexcept {
    return merge_result(a, b) != 0;
}

}  // namespace triples::game
