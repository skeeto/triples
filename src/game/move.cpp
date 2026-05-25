#include "game/move.hpp"

#include <algorithm>

namespace triples::game {

namespace {

struct LineResolution {
    std::array<std::uint8_t, 4> new_line{};
    bool                        moved = false;
    // For each new cell index 0..3, which old cell ended up there (-1 if empty).
    std::array<std::int8_t, 4>  origin{};
    // For each new cell index 0..3, which old cell was the merge *partner* (the
    // trailing tile that merged INTO this cell), or -1 if no merge happened here.
    std::array<std::int8_t, 4>  merge_partner{};

    LineResolution() {
        origin.fill(-1);
        merge_partner.fill(-1);
    }
};

// Resolve a single line as if swiped *toward higher indices* (i.e., "right" for
// a row, "down" for a column). Caller is responsible for mirroring the input
// and output for the opposite directions.
//
// Algorithm: process adjacent pairs from the leading edge (index 3) inward.
// At each pair (high=cell[i], low=cell[i-1]):
//   - If high is empty and low is non-empty: slide low into high.
//   - Else if both are non-empty and they can merge AND we haven't merged yet
//     this line: merge low into high.
//   - Else if both are non-empty and we can't merge them: blocked — `low`
//     can't move this swipe, but we continue inward (trailing tiles may still
//     have room behind them).
// At most one merge per line, but slides are allowed throughout the line.
// This matches real Threes! behavior: a row like `[_, 3, 3, 6]` right merges
// the inner `3+3` even though the `6` at the wall doesn't budge.
LineResolution resolve_line_forward(const std::array<std::uint8_t, 4>& line) {
    LineResolution r;
    r.new_line = line;
    for (int i = 0; i < 4; ++i) {
        r.origin[i] = (line[i] != 0) ? static_cast<std::int8_t>(i) : -1;
    }
    r.merge_partner.fill(-1);
    r.moved = false;

    bool merged_already = false;
    for (int i = 3; i >= 1; --i) {
        std::uint8_t high = r.new_line[i];
        std::uint8_t low  = r.new_line[i - 1];

        if (high == 0 && low != 0) {
            // Slide low into high.
            std::int8_t low_origin = r.origin[i - 1];
            r.new_line[i]     = low;
            r.new_line[i - 1] = 0;
            r.origin[i]       = low_origin;
            r.origin[i - 1]   = -1;
            r.moved = true;
        } else if (high != 0 && low != 0) {
            if (!merged_already) {
                std::uint8_t m = merge_result(high, low);
                if (m != 0) {
                    std::int8_t low_origin = r.origin[i - 1];
                    r.new_line[i]      = m;
                    r.new_line[i - 1]  = 0;
                    // origin[i] stays: the leading contributor's source cell.
                    r.merge_partner[i] = low_origin;
                    r.origin[i - 1]    = -1;
                    merged_already = true;
                    r.moved = true;
                }
                // else: blocked — low can't slide into high. Continue inward.
            }
            // else: a merge has already happened in this line; low is blocked.
        }
        // else: low is empty; nothing to slide. Continue inward.
    }
    return r;
}

// Helpers to extract/insert lines based on direction.
struct LineAccess {
    // For each line i, which board cell indices map to line positions 0..3,
    // and which line positions are the "leading edge" (mapped to 3 in the
    // forward-resolved line).
    std::array<int, 4> idx;
};

inline LineAccess row_left_to_right(int row) {
    return { { row * 4 + 0, row * 4 + 1, row * 4 + 2, row * 4 + 3 } };
}
inline LineAccess row_right_to_left(int row) {
    return { { row * 4 + 3, row * 4 + 2, row * 4 + 1, row * 4 + 0 } };
}
inline LineAccess col_top_to_bottom(int col) {
    return { { 0 * 4 + col, 1 * 4 + col, 2 * 4 + col, 3 * 4 + col } };
}
inline LineAccess col_bottom_to_top(int col) {
    return { { 3 * 4 + col, 2 * 4 + col, 1 * 4 + col, 0 * 4 + col } };
}

}  // namespace

MoveResult resolve(const Board& b, Direction dir) noexcept {
    MoveResult r;
    r.new_board = b;
    r.origin.fill(-1);
    r.merge_partner_origin.fill(-1);
    for (int i = 0; i < 16; ++i) {
        if (b.cells[i] != 0) r.origin[i] = static_cast<std::int8_t>(i);
    }

    // For each of 4 lines, build the access mapping such that index 3 is the
    // leading edge in the swipe direction.
    for (int line = 0; line < 4; ++line) {
        LineAccess la;
        switch (dir) {
            case Direction::Right: la = row_left_to_right(line); break;
            case Direction::Left:  la = row_right_to_left(line); break;
            case Direction::Down:  la = col_top_to_bottom(line); break;
            case Direction::Up:    la = col_bottom_to_top(line); break;
        }
        std::array<std::uint8_t, 4> in_line;
        for (int p = 0; p < 4; ++p) in_line[p] = b.cells[la.idx[p]];

        LineResolution lr = resolve_line_forward(in_line);
        if (!lr.moved) {
            r.line_moved[line] = false;
            continue;
        }
        r.line_moved[line] = true;
        r.any_change = true;

        // Write back.
        for (int p = 0; p < 4; ++p) {
            r.new_board.cells[la.idx[p]] = lr.new_line[p];
            r.origin[la.idx[p]] =
                (lr.origin[p] >= 0) ? static_cast<std::int8_t>(la.idx[lr.origin[p]]) : -1;
            r.merge_partner_origin[la.idx[p]] =
                (lr.merge_partner[p] >= 0) ? static_cast<std::int8_t>(la.idx[lr.merge_partner[p]]) : -1;
        }
    }
    return r;
}

bool any_move_possible(const Board& b) noexcept {
    for (Direction d : { Direction::Up, Direction::Down, Direction::Left, Direction::Right }) {
        if (resolve(b, d).any_change) return true;
    }
    return false;
}

}  // namespace triples::game
