#pragma once
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "game/game_state.hpp"

namespace triples::game {

// Versioned single-line text format. Compact, no JSON.
//   v1 b0 b1 ... b15  deck_count  d0 .. d(count-1)  next_rank next_bonus
//      max_rank_seen  score  rng0 rng1 rng2 rng3
//
// All fields are decimal integers separated by single spaces. Returns false
// (or nullopt) on malformed / version-mismatch input — caller should fall
// back to initial_state in that case.
std::string serialize_state(const GameState& s);
std::optional<GameState> deserialize_state(std::string_view text);

// One line per entry, "<score> <YYYY-MM-DD>". `iso_date` must be exactly 10
// characters in YYYY-MM-DD form; not validated here.
struct HighScore {
    std::uint64_t score = 0;
    std::string   date;
};

std::string serialize_highscores(const HighScore* entries, std::size_t n);
// Returns up to 8 entries, ignoring malformed lines.
std::vector<HighScore> deserialize_highscores(std::string_view text);

}  // namespace triples::game
