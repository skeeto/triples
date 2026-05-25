#include "render/renderer.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>

#include "render/effects.hpp"
#include "resources/embedded.hpp"

namespace triples::render {

namespace {

constexpr std::uint8_t kBgR = 0xF8;
constexpr std::uint8_t kBgG = 0xF2;
constexpr std::uint8_t kBgB = 0xE5;

// Map cell index → (row, col).
inline void to_rc(int idx, int& r, int& c) {
    r = idx / 4;
    c = idx % 4;
}

// Per the rules, the visual offset for a swipe direction.
inline void dir_dx_dy(game::Direction d, int& dx, int& dy) {
    switch (d) {
        case game::Direction::Right: dx = 1;  dy = 0;  break;
        case game::Direction::Left:  dx = -1; dy = 0;  break;
        case game::Direction::Down:  dx = 0;  dy = 1;  break;
        case game::Direction::Up:    dx = 0;  dy = -1; break;
    }
}

inline char digit(int v) { return '0' + static_cast<char>(v); }

void format_int(std::uint64_t v, char* buf, std::size_t buflen, std::size_t& out_len) {
    if (buflen == 0) { out_len = 0; return; }
    if (v == 0) { buf[0] = '0'; out_len = 1; return; }
    char tmp[24];
    std::size_t n = 0;
    while (v > 0 && n < sizeof(tmp)) {
        tmp[n++] = digit(static_cast<int>(v % 10));
        v /= 10;
    }
    std::size_t k = std::min(n, buflen - 1);
    for (std::size_t i = 0; i < k; ++i) buf[i] = tmp[n - 1 - i];
    buf[k] = '\0';
    out_len = k;
}

// Tile face value as a base-10 string. Returns length.
std::size_t tile_label(std::uint8_t rank, char* buf, std::size_t buflen) {
    if (rank == 0 || rank > 15) { if (buflen) buf[0] = 0; return 0; }
    std::size_t n;
    format_int(game::kTileValues[rank], buf, buflen, n);
    return n;
}

}  // namespace

Renderer::Renderer() = default;
Renderer::~Renderer() {
    if (empty_slot_tex_) SDL_DestroyTexture(empty_slot_tex_);
    if (sdl_renderer_)   SDL_DestroyRenderer(sdl_renderer_);
    if (window_)         SDL_DestroyWindow(window_);
}

bool Renderer::initialize(const char* title, int initial_w, int initial_h) {
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO)) {
        std::fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
        return false;
    }
    window_ = SDL_CreateWindow(title, initial_w, initial_h,
                               SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY);
    if (!window_) {
        std::fprintf(stderr, "SDL_CreateWindow: %s\n", SDL_GetError());
        return false;
    }
    sdl_renderer_ = SDL_CreateRenderer(window_, nullptr);
    if (!sdl_renderer_) {
        std::fprintf(stderr, "SDL_CreateRenderer: %s\n", SDL_GetError());
        return false;
    }
    SDL_SetRenderDrawBlendMode(sdl_renderer_, SDL_BLENDMODE_BLEND);

    int w, h;
    SDL_GetWindowSizeInPixels(window_, &w, &h);
    set_logical_size(w, h);

    if (!text_.initialize(sdl_renderer_, font_inter_bold_data, font_inter_bold_size,
                          18.0f, 28.0f, 56.0f)) {
        std::fprintf(stderr, "TextAtlas::initialize failed\n");
        return false;
    }
    return true;
}

void Renderer::set_logical_size(int w, int h) {
    logical_w_ = w;
    logical_h_ = h;
    recompute_layout_();
    ensure_cache_();
}

void Renderer::recompute_layout_() {
    layout_.win_w = static_cast<float>(logical_w_);
    layout_.win_h = static_cast<float>(logical_h_);

    // Reserve top and bottom HUD bands.
    const float hud_top = std::min(120.0f, layout_.win_h * 0.16f);
    const float hud_bot = std::min(80.0f,  layout_.win_h * 0.10f);
    const float margin  = std::min(layout_.win_w, layout_.win_h) * 0.04f;

    float avail_w = layout_.win_w - margin * 2.0f;
    float avail_h = layout_.win_h - hud_top - hud_bot - margin * 2.0f;
    float cell_w = std::min(avail_w / 4.0f, (avail_h / 4.0f) / 1.5f);
    cell_w = std::floor(cell_w);
    float cell_h = cell_w * 1.5f;

    layout_.cell_w = cell_w;
    layout_.cell_h = cell_h;
    layout_.board_w = cell_w * 4.0f;
    layout_.board_h = cell_h * 4.0f;
    layout_.board_x = std::floor((layout_.win_w - layout_.board_w) * 0.5f);
    layout_.board_y = std::floor(hud_top + (avail_h - layout_.board_h) * 0.5f + margin);
    layout_.hud_top_y    = hud_top * 0.5f;
    layout_.hud_bottom_y = layout_.win_h - hud_bot * 0.5f;
}

void Renderer::ensure_cache_() {
    int desired = static_cast<int>(layout_.cell_w);
    if (desired <= 8) desired = 8;
    if (desired == baked_cell_w_) return;
    tiles_.bake(sdl_renderer_, desired);
    if (empty_slot_tex_) SDL_DestroyTexture(empty_slot_tex_);
    empty_slot_tex_ = bake_empty_slot_texture(sdl_renderer_, desired, static_cast<int>(desired * 1.5f));
    baked_cell_w_ = desired;
}

float Renderer::cell_size_px() const noexcept {
    return layout_.cell_w;
}

int Renderer::cell_for_point(float x, float y) const noexcept {
    if (x < layout_.board_x || y < layout_.board_y) return -1;
    float lx = x - layout_.board_x;
    float ly = y - layout_.board_y;
    if (lx >= layout_.board_w || ly >= layout_.board_h) return -1;
    int col = static_cast<int>(lx / layout_.cell_w);
    int row = static_cast<int>(ly / layout_.cell_h);
    if (col < 0 || col > 3 || row < 0 || row > 3) return -1;
    return row * 4 + col;
}

void Renderer::draw_background_() {
    SDL_SetRenderDrawColor(sdl_renderer_, kBgR, kBgG, kBgB, 0xFF);
    SDL_RenderClear(sdl_renderer_);
}

void Renderer::draw_empty_slots_() {
    if (!empty_slot_tex_) return;
    for (int i = 0; i < 16; ++i) {
        int r, c;
        to_rc(i, r, c);
        SDL_FRect dst{
            layout_.board_x + c * layout_.cell_w,
            layout_.board_y + r * layout_.cell_h,
            layout_.cell_w, layout_.cell_h,
        };
        SDL_RenderTexture(sdl_renderer_, empty_slot_tex_, nullptr, &dst);
    }
}

void Renderer::draw_one_tile_(std::uint8_t rank, float center_x, float center_y,
                              float scale_x, float scale_y, std::uint8_t alpha,
                              std::uint8_t current_max) {
    SDL_Texture* t = tiles_.texture_for(rank);
    if (!t) return;
    SDL_SetTextureAlphaMod(t, alpha);
    SDL_FRect dst{
        center_x - layout_.cell_w * 0.5f * scale_x,
        center_y - layout_.cell_h * 0.5f * scale_y,
        layout_.cell_w * scale_x,
        layout_.cell_h * scale_y,
    };
    SDL_RenderTexture(sdl_renderer_, t, nullptr, &dst);

    // Draw the digit text on top — skip for blue/red small ranks if they're
    // visibly tiny, but we always have room.
    char buf[8];
    std::size_t n = tile_label(rank, buf, sizeof(buf));
    if (n == 0) return;
    std::uint8_t tr = 0x2A, tg = 0x2A, tb = 0x2A;
    if (rank == 1 || rank == 2) {
        tr = 0xFF; tg = 0xFF; tb = 0xFF;
    } else if (rank == current_max) {
        tr = 0xD9; tg = 0x2E; tb = 0x2E;
    } else if (rank == 15) {
        tr = 0xFF; tg = 0xFF; tb = 0xFF;
    }
    // Pick a font size that fits the tile width.
    TextAtlas::Size sz = TextAtlas::Size::Large;
    if (layout_.cell_w < 56.0f) sz = TextAtlas::Size::Body;
    if (layout_.cell_w < 32.0f) sz = TextAtlas::Size::Small;

    float w = text_.measure_width(buf, sz);
    while (w > layout_.cell_w * 0.85f && sz != TextAtlas::Size::Small) {
        sz = (sz == TextAtlas::Size::Large) ? TextAtlas::Size::Body : TextAtlas::Size::Small;
        w = text_.measure_width(buf, sz);
    }
    float h = text_.line_height(sz);
    // Center text vertically in the upper ~78% of the tile (above the bottom band).
    float text_center_y = center_y - layout_.cell_h * 0.5f + (layout_.cell_h * 0.78f) * 0.5f;
    float baseline_y = text_center_y + h * 0.32f;
    text_.draw_centered(sdl_renderer_, buf, sz, center_x, baseline_y, tr, tg, tb, alpha);
}

void Renderer::draw_tiles_(const game::GameState& state, const input::DragController& drag,
                           const Animations& anims) {
    // Build a "source → destination" map for the current drag direction.
    std::array<int, 16> src_to_dst;
    for (auto& v : src_to_dst) v = -1;

    bool dragging = drag.active() && drag.dry.any_change;
    if (dragging) {
        for (int i = 0; i < 16; ++i) {
            if (drag.dry.origin[i] >= 0)                src_to_dst[drag.dry.origin[i]]                = i;
            if (drag.dry.merge_partner_origin[i] >= 0)  src_to_dst[drag.dry.merge_partner_origin[i]]  = i;
        }
    } else {
        for (int i = 0; i < 16; ++i) {
            if (state.board.cells[i] != 0) src_to_dst[i] = i;
        }
    }

    // Stuck-line squish: for lines that didn't move, the leading cell (in the
    // swipe direction) gets a small anisotropic squish for f > 0.85.
    auto stuck_squish_cell = [&](int cell, float& sx, float& sy) {
        if (!dragging) return;
        if (drag.f <= 0.85f) return;
        int row, col;
        to_rc(cell, row, col);
        bool is_leading = false;
        int line = -1;
        switch (drag.dir) {
            case game::Direction::Right: line = row; is_leading = (col == 3); break;
            case game::Direction::Left:  line = row; is_leading = (col == 0); break;
            case game::Direction::Down:  line = col; is_leading = (row == 3); break;
            case game::Direction::Up:    line = col; is_leading = (row == 0); break;
        }
        if (!is_leading) return;
        if (line < 0 || drag.dry.line_moved[line]) return;
        float k = (drag.f - 0.85f) / 0.15f;
        float along  = 1.0f - 0.08f * k;
        float cross  = 1.0f + 0.04f * k;
        switch (drag.dir) {
            case game::Direction::Right:
            case game::Direction::Left:
                sx *= along; sy *= cross; break;
            case game::Direction::Up:
            case game::Direction::Down:
                sx *= cross; sy *= along; break;
        }
    };

    auto tile_center = [&](int row, int col, float& cx, float& cy) {
        cx = layout_.board_x + col * layout_.cell_w + layout_.cell_w * 0.5f;
        cy = layout_.board_y + row * layout_.cell_h + layout_.cell_h * 0.5f;
    };

    const std::uint8_t current_max = state.board.max_rank();

    // Draw merge "trailing" tiles first so the leading sits on top — but only
    // during the drag, when both halves of a merge are visible. In the static
    // (non-drag) case, only leading is present (board state is post-move).
    auto draw_pass = [&](bool trailing_only) {
        for (int s = 0; s < 16; ++s) {
            std::uint8_t rank = state.board.cells[s];
            if (rank == 0) continue;
            int dst = src_to_dst[s];
            if (dst < 0) continue;
            // Was this source the trailing partner of a merge?
            bool is_trailing = false;
            if (dragging && drag.dry.merge_partner_origin[dst] == static_cast<std::int8_t>(s)) {
                is_trailing = true;
            }
            if (trailing_only != is_trailing) continue;

            int sr, sc, dr, dc;
            to_rc(s, sr, sc);
            to_rc(dst, dr, dc);
            float src_cx, src_cy, dst_cx, dst_cy;
            tile_center(sr, sc, src_cx, src_cy);
            tile_center(dr, dc, dst_cx, dst_cy);
            float f = dragging ? drag.f : 0.0f;
            float cx = src_cx + (dst_cx - src_cx) * f;
            float cy = src_cy + (dst_cy - src_cy) * f;

            // Merge-bump scaling.
            float bump_scale = 1.0f;
            for (const auto& mb : anims.merge_bumps) {
                if (mb.cell == s) {
                    float u = mb.t / mb.dur;
                    if (u < 1.0f) {
                        float k = ease(u, Easing::OutBack);
                        bump_scale = 1.0f + 0.10f * (1.0f - std::fabs(k - 0.5f) * 2.0f);
                    }
                }
            }

            float scale_x = bump_scale, scale_y = bump_scale;
            stuck_squish_cell(s, scale_x, scale_y);

            draw_one_tile_(rank, cx, cy, scale_x, scale_y, 255, current_max);
        }
    };

    draw_pass(true);   // trailing partners first
    draw_pass(false);  // leading / non-merge tiles second

    // Spawn fade: a newly-spawned tile at a specific cell fades in over its dur.
    // The board's actual cell holds the new tile; we draw it again on top
    // with its alpha tracking the animation. Skip if no active fade.
    for (const auto& sf : anims.spawn_fades) {
        std::uint8_t rank = state.board.cells[sf.cell];
        if (rank == 0) continue;
        float u = sf.t / sf.dur;
        u = std::clamp(u, 0.0f, 1.0f);
        float a = u;
        float s = 0.85f + 0.15f * ease(u, Easing::OutQuad);
        int r, c;
        to_rc(sf.cell, r, c);
        float cx, cy;
        tile_center(r, c, cx, cy);
        // Draw a fading-in copy. The base layer already drew the tile at full alpha,
        // so this overlay is mainly a scale effect.
        draw_one_tile_(rank, cx, cy, s, s, static_cast<std::uint8_t>(a * 255.0f), current_max);
    }

    // Score popups.
    for (const auto& p : anims.score_popups) {
        float u = p.t / p.dur;
        u = std::clamp(u, 0.0f, 1.0f);
        float dy = -ease(u, Easing::OutQuad) * 40.0f;
        std::uint8_t a = static_cast<std::uint8_t>((1.0f - u) * 220.0f);
        char buf[20];
        std::size_t n;
        format_int(p.value, buf, sizeof(buf), n);
        std::string s = "+";
        s.append(buf, n);
        text_.draw_centered(sdl_renderer_, s, TextAtlas::Size::Body,
                            p.x, p.y + dy, 0x2A, 0x2A, 0x2A, a);
    }
}

void Renderer::draw_hud_(const game::GameState& state, std::uint64_t high_score, bool game_over) {
    // Top HUD spans y=[0 .. board_y]. Lay out: a small label row at the top,
    // then preview card / score number / best number row.
    const float hud_top_h = layout_.board_y;
    const float label_baseline = std::max(20.0f, hud_top_h * 0.18f);
    const float small_h = text_.line_height(TextAtlas::Size::Small);
    const float body_h  = text_.line_height(TextAtlas::Size::Body);
    const float large_h = text_.line_height(TextAtlas::Size::Large);

    // Preview card sized to the available HUD height.
    const float row_top    = label_baseline + small_h * 0.4f;
    const float row_height = hud_top_h - row_top - 4.0f;
    const float preview_h  = std::max(24.0f, std::min(row_height, layout_.cell_h * 0.55f));
    const float preview_w  = preview_h / 1.5f;

    const float left_anchor   = layout_.board_x + preview_w * 0.5f;
    const float right_anchor  = layout_.win_w - layout_.board_x - preview_w * 0.5f;
    const float center_anchor = layout_.win_w * 0.5f;

    // Small labels (NEXT / SCORE / BEST).
    text_.draw_centered(sdl_renderer_, "NEXT",  TextAtlas::Size::Small,
                        left_anchor,   label_baseline, 0x66, 0x66, 0x66);
    text_.draw_centered(sdl_renderer_, "SCORE", TextAtlas::Size::Small,
                        center_anchor, label_baseline, 0x66, 0x66, 0x66);
    text_.draw_centered(sdl_renderer_, "BEST",  TextAtlas::Size::Small,
                        right_anchor,  label_baseline, 0x66, 0x66, 0x66);

    // Preview card (rank-colored fill, rounded-rect-ish via separate SDL_FRect).
    {
        std::uint8_t cr, cg, cb;
        std::uint8_t rank = state.next.rank;
        if (rank == 1) { cr = 0x00; cg = 0x99; cb = 0xCC; }
        else if (rank == 2) { cr = 0xE6; cg = 0x45; cb = 0x45; }
        else { cr = 0xFC; cg = 0xFC; cb = 0xFC; }
        SDL_FRect rrect{left_anchor - preview_w * 0.5f, row_top,
                        preview_w, preview_h};
        SDL_SetRenderDrawColor(sdl_renderer_, cr, cg, cb, 0xFF);
        SDL_RenderFillRect(sdl_renderer_, &rrect);
        if (state.next.is_bonus) {
            text_.draw_centered(sdl_renderer_, "+", TextAtlas::Size::Large,
                                left_anchor, row_top + preview_h * 0.65f,
                                0x2A, 0x2A, 0x2A);
        }
    }

    // Score (centered, large).
    char buf[24];
    std::size_t n;
    format_int(state.score, buf, sizeof(buf), n);
    text_.draw_centered(sdl_renderer_, std::string_view(buf, n), TextAtlas::Size::Large,
                        center_anchor, row_top + (preview_h + large_h) * 0.5f,
                        0x2A, 0x2A, 0x2A);

    // Best (right, body).
    format_int(high_score, buf, sizeof(buf), n);
    text_.draw_centered(sdl_renderer_, std::string_view(buf, n), TextAtlas::Size::Body,
                        right_anchor, row_top + (preview_h + body_h) * 0.5f,
                        0x2A, 0x2A, 0x2A);

    // Bottom: game-over message.
    if (game_over) {
        text_.draw_centered(sdl_renderer_, "GAME OVER  —  TAP TO RESTART",
                            TextAtlas::Size::Body,
                            layout_.win_w * 0.5f, layout_.hud_bottom_y,
                            0xD9, 0x2E, 0x2E);
    }
}

void Renderer::draw(const game::GameState& state,
                    const input::DragController& drag,
                    const Animations& anims,
                    std::uint64_t high_score,
                    bool game_over) {
    if (!sdl_renderer_) return;
    float shake_x, shake_y;
    anims.shake_offset(shake_x, shake_y);

    // Apply shake as a temporary viewport translation.
    SDL_Rect prev_viewport;
    SDL_GetRenderViewport(sdl_renderer_, &prev_viewport);

    draw_background_();

    // Shift the world by shake offset using viewport-style translation. SDL3
    // doesn't have a built-in translate; we just offset the layout temporarily.
    layout_.board_x += shake_x;
    layout_.board_y += shake_y;

    draw_empty_slots_();
    draw_tiles_(state, drag, anims);
    draw_particles(sdl_renderer_, anims);

    layout_.board_x -= shake_x;
    layout_.board_y -= shake_y;

    draw_hud_(state, high_score, game_over);

    SDL_RenderPresent(sdl_renderer_);
}

}  // namespace triples::render
