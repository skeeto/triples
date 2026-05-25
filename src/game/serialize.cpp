#include "game/serialize.hpp"

#include <charconv>
#include <sstream>
#include <vector>

namespace triples::game {

namespace {

// Append a decimal integer + trailing space.
template <typename T>
void appendf(std::string& out, T v) {
    char buf[32];
    auto res = std::to_chars(buf, buf + sizeof(buf), v);
    out.append(buf, res.ptr);
    out.push_back(' ');
}

// Read the next whitespace-separated decimal token from `text` starting at
// `pos`. On success, advances `pos` past the token and any trailing
// whitespace, writes the value to `out`, and returns true. Returns false
// on parse failure.
template <typename T>
bool read_int(std::string_view text, std::size_t& pos, T& out) {
    while (pos < text.size() && (text[pos] == ' ' || text[pos] == '\t' || text[pos] == '\n')) ++pos;
    if (pos >= text.size()) return false;
    std::size_t start = pos;
    while (pos < text.size() && text[pos] != ' ' && text[pos] != '\t' && text[pos] != '\n') ++pos;
    auto* first = text.data() + start;
    auto* last  = text.data() + pos;
    auto r = std::from_chars(first, last, out);
    return r.ec == std::errc() && r.ptr == last;
}

}  // namespace

std::string serialize_state(const GameState& s) {
    std::string out;
    out.reserve(192);
    out += "v1 ";
    for (auto v : s.board.cells) appendf(out, static_cast<unsigned>(v));
    appendf(out, static_cast<unsigned>(s.deck.count));
    for (int i = 0; i < s.deck.count; ++i) {
        appendf(out, static_cast<unsigned>(s.deck.cards[s.deck.head + i]));
    }
    appendf(out, static_cast<unsigned>(s.next.rank));
    appendf(out, s.next.is_bonus ? 1u : 0u);
    appendf(out, static_cast<unsigned>(s.max_rank_seen));
    appendf(out, s.score);
    appendf(out, s.rng.s[0]);
    appendf(out, s.rng.s[1]);
    appendf(out, s.rng.s[2]);
    appendf(out, s.rng.s[3]);
    if (!out.empty() && out.back() == ' ') out.pop_back();
    return out;
}

std::optional<GameState> deserialize_state(std::string_view text) {
    std::size_t pos = 0;
    // Skip leading whitespace, check version prefix "v1".
    while (pos < text.size() && (text[pos] == ' ' || text[pos] == '\t' || text[pos] == '\n')) ++pos;
    if (pos + 2 > text.size() || text[pos] != 'v' || text[pos + 1] != '1') return std::nullopt;
    pos += 2;

    GameState s;
    for (int i = 0; i < 16; ++i) {
        unsigned v;
        if (!read_int(text, pos, v) || v > kMaxRank) return std::nullopt;
        s.board.cells[i] = static_cast<std::uint8_t>(v);
    }
    unsigned deck_count;
    if (!read_int(text, pos, deck_count) || deck_count > 12) return std::nullopt;
    s.deck.head = 0;
    s.deck.count = static_cast<std::uint8_t>(deck_count);
    for (unsigned i = 0; i < deck_count; ++i) {
        unsigned d;
        if (!read_int(text, pos, d) || d < 1 || d > 3) return std::nullopt;
        s.deck.cards[i] = static_cast<std::uint8_t>(d);
    }
    unsigned next_rank, is_bonus, max_rank;
    if (!read_int(text, pos, next_rank) || next_rank > kMaxRank) return std::nullopt;
    if (!read_int(text, pos, is_bonus)) return std::nullopt;
    if (!read_int(text, pos, max_rank) || max_rank > kMaxRank) return std::nullopt;
    s.next.rank = static_cast<std::uint8_t>(next_rank);
    s.next.is_bonus = (is_bonus != 0);
    s.max_rank_seen = static_cast<std::uint8_t>(max_rank);
    if (!read_int(text, pos, s.score)) return std::nullopt;
    for (int i = 0; i < 4; ++i) {
        if (!read_int(text, pos, s.rng.s[i])) return std::nullopt;
    }
    return s;
}

std::string serialize_highscores(const HighScore* entries, std::size_t n) {
    std::string out;
    for (std::size_t i = 0; i < n; ++i) {
        char buf[32];
        auto res = std::to_chars(buf, buf + sizeof(buf), entries[i].score);
        out.append(buf, res.ptr);
        out.push_back(' ');
        out.append(entries[i].date);
        out.push_back('\n');
    }
    return out;
}

std::vector<HighScore> deserialize_highscores(std::string_view text) {
    std::vector<HighScore> out;
    std::size_t pos = 0;
    while (pos < text.size() && out.size() < 8) {
        std::size_t line_start = pos;
        while (pos < text.size() && text[pos] != '\n') ++pos;
        std::string_view line(text.data() + line_start, pos - line_start);
        if (pos < text.size()) ++pos;  // skip '\n'

        std::size_t lp = 0;
        std::uint64_t score = 0;
        if (!read_int(line, lp, score)) continue;
        while (lp < line.size() && line[lp] == ' ') ++lp;
        if (line.size() - lp != 10) continue;
        HighScore hs;
        hs.score = score;
        hs.date.assign(line.data() + lp, 10);
        out.push_back(std::move(hs));
    }
    return out;
}

}  // namespace triples::game
