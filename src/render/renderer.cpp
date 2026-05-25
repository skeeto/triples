#include "render/renderer.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>

#include "render/effects.hpp"
#include "resources/embedded.hpp"
#include "resources/icon.hpp"

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
    if (restart_button_tex_) SDL_DestroyTexture(restart_button_tex_);
    if (empty_slot_tex_)     SDL_DestroyTexture(empty_slot_tex_);
    if (sdl_renderer_)       SDL_DestroyRenderer(sdl_renderer_);
    if (window_)             SDL_DestroyWindow(window_);
}

bool Renderer::initialize(const char* title, int initial_w, int initial_h) {
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO)) {
        std::fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
        return false;
    }
    // Windows sizes SDL3 windows in screen pixels and is always per-monitor
    // DPI-aware, so the unscaled 480×800 initial size is physically tiny on a
    // 4K display at 150–200% UI scaling. Pre-multiply by the display's
    // content scale on Windows. macOS sizes in points (OS handles scaling);
    // Emscripten is auto-sized to the canvas via sync_canvas_size_().
    int w_req = initial_w, h_req = initial_h;
#ifdef _WIN32
    float content_scale = SDL_GetDisplayContentScale(SDL_GetPrimaryDisplay());
    if (content_scale > 1.0f) {
        w_req = static_cast<int>(w_req * content_scale + 0.5f);
        h_req = static_cast<int>(h_req * content_scale + 0.5f);
    }
#endif
    window_ = SDL_CreateWindow(title, w_req, h_req,
                               SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY);
    if (!window_) {
        std::fprintf(stderr, "SDL_CreateWindow: %s\n", SDL_GetError());
        return false;
    }
    // Window / taskbar icon. SDL copies the pixels into the window, so the
    // source surface is only needed transiently. Skipped on macOS because
    // SDL_SetWindowIcon there hands the image to `[NSApp setApplicationIcon
    // Image:]`, which would overwrite the bundle's multi-resolution
    // triples.icns with this single 64×64 RGBA — visibly blurry in the Dock
    // and Cmd-Tab switcher. Letting the bundle icon (CFBundleIconFile)
    // win there gives the OS the full size range to pick from. A harmless
    // no-op on the web build, which has no OS window chrome.
#ifndef __APPLE__
    if (SDL_Surface* icon = SDL_CreateSurfaceFrom(
            resources::ICON_W, resources::ICON_H, SDL_PIXELFORMAT_RGBA32,
            const_cast<std::uint8_t*>(resources::ICON_RGBA),
            resources::ICON_W * 4)) {
        SDL_SetWindowIcon(window_, icon);
        SDL_DestroySurface(icon);
    }
#endif
    sdl_renderer_ = SDL_CreateRenderer(window_, nullptr);
    if (!sdl_renderer_) {
        std::fprintf(stderr, "SDL_CreateRenderer: %s\n", SDL_GetError());
        return false;
    }
    SDL_SetRenderDrawBlendMode(sdl_renderer_, SDL_BLENDMODE_BLEND);

    int w, h;
    SDL_GetWindowSizeInPixels(window_, &w, &h);
    set_logical_size(w, h);

    // Atlas baked at sizes large enough for the typical high-DPI tile —
    // the renderer scales each draw down (or up) to fit the current cell.
    if (!text_.initialize(sdl_renderer_, font_inter_bold_data, font_inter_bold_size,
                          32.0f, 48.0f, 96.0f)) {
        std::fprintf(stderr, "TextAtlas::initialize (Inter) failed\n");
        return false;
    }
    // Jua for digits + the "+" sign on the tally. Limited to ASCII 43..57
    // (`+`, `,`, `-`, `.`, `/`, `0`-`9`) so we can afford a much larger Large
    // bake (192 px) without inflating atlas memory — keeps 4K screens at
    // close to a 1:1 scale instead of upscaling a 96-px font 2-3×.
    if (!text_digits_.initialize(sdl_renderer_, font_jua_regular_data, font_jua_regular_size,
                                 64.0f, 96.0f, 192.0f, 43, 15)) {
        std::fprintf(stderr, "TextAtlas::initialize (Jua) failed\n");
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

    // Ask SDL for the OS-reported safe rect (iOS status bar / notch /
    // home-indicator inset, Android nav bar, …). SDL3 returns it in window
    // (logical) coords; we work in device pixels, so multiply by
    // pixel_density. Desktop platforms typically return the full window
    // and the insets are 0.
    layout_.safe_top = 0.0f;
    layout_.safe_bot = 0.0f;
    if (window_) {
        SDL_Rect safe{};
        if (SDL_GetWindowSafeArea(window_, &safe)) {
            int win_w_logical = 0, win_h_logical = 0;
            SDL_GetWindowSize(window_, &win_w_logical, &win_h_logical);
            const float dpr = pixel_density();
            layout_.safe_top = std::max(0.0f, safe.y * dpr);
            layout_.safe_bot = std::max(0.0f,
                (win_h_logical - safe.y - safe.h) * dpr);
        }
    }
    const float usable_h = std::max(0.0f,
        layout_.win_h - layout_.safe_top - layout_.safe_bot);

    // Reserve top and bottom HUD bands inside the safe area.
    const float hud_top = std::min(120.0f, usable_h * 0.16f);
    const float hud_bot = std::min(80.0f,  usable_h * 0.10f);
    const float margin  = std::min(layout_.win_w, usable_h) * 0.04f;

    float avail_w = layout_.win_w - margin * 2.0f;
    float avail_h = usable_h - hud_top - hud_bot - margin * 2.0f;
    float cell_w = std::min(avail_w / 4.0f, (avail_h / 4.0f) / 1.5f);
    cell_w = std::floor(cell_w);
    float cell_h = cell_w * 1.5f;

    layout_.cell_w = cell_w;
    layout_.cell_h = cell_h;
    // ~1.6% inset on each side — a couple of pixels at typical mobile sizes,
    // enough to make adjacent tiles visibly separate without losing room.
    layout_.tile_inset = std::max(1.0f, std::floor(cell_w * 0.016f));
    layout_.tile_w = cell_w - layout_.tile_inset * 2.0f;
    layout_.tile_h = cell_h - layout_.tile_inset * 2.0f;
    layout_.board_w = cell_w * 4.0f;
    layout_.board_h = cell_h * 4.0f;
    layout_.board_x = std::floor((layout_.win_w - layout_.board_w) * 0.5f);
    // board_y is offset by safe_top so the top HUD strip lives *below* any
    // status bar / notch.
    layout_.board_y = std::floor(layout_.safe_top + hud_top
                                 + (avail_h - layout_.board_h) * 0.5f
                                 + margin);
    layout_.hud_top_y    = layout_.safe_top + hud_top * 0.5f;
    layout_.hud_bottom_y = layout_.win_h - layout_.safe_bot - hud_bot * 0.5f;

    // Restart button: parked right under the board so it reads as part of
    // the board UI, not a footer. Sized by two competing constraints —
    // proportional to a board cell (so it feels in the same rhythm as the
    // tiles), AND small enough to fit between the board's bottom edge and
    // the window's bottom edge without overlap. The space-below-board cap
    // is critical: at aspect ratios where the board nearly fills avail_h
    // (e.g. landscape tablets), a large button would otherwise sit on top
    // of the bottom row of tiles.
    layout_.restart_cx = layout_.win_w * 0.5f;
    // Space below the board is bounded by the BOTTOM safe area edge, not
    // the window edge, so the button doesn't end up under the home indicator.
    const float space_below_board =
        (layout_.win_h - layout_.safe_bot)
        - (layout_.board_y + layout_.board_h);
    constexpr float kBottomMargin = 4.0f;
    const float usable = std::max(0.0f, space_below_board - kBottomMargin);
    // Given gap(r) = max(8, 0.16*r) and the constraint gap(r) + 2*r <= usable,
    // solve for the largest r in each gap regime and pick the feasible one.
    float r_max_min_gap = (usable - 8.0f) * 0.5f;
    float r_max = (r_max_min_gap > 50.0f) ? (usable / 2.16f) : r_max_min_gap;
    if (r_max < 0.0f) r_max = 0.0f;
    const float r_from_cell = layout_.cell_w * 0.42f;
    layout_.restart_r = std::clamp(std::min(r_from_cell, r_max), 16.0f, 100.0f);
    const float gap_to_board = std::max(8.0f, layout_.restart_r * 0.16f);
    layout_.restart_cy = (layout_.board_y + layout_.board_h)
                        + gap_to_board + layout_.restart_r;
    // Hit radius: 20% larger than the visible disc for finger-friendliness,
    // but never extends back into the board's bottom row.
    layout_.restart_hit_r = std::min(layout_.restart_r * 1.20f,
                                      layout_.restart_r + gap_to_board - 2.0f);
}

void Renderer::ensure_cache_() {
    // Bake at the rendered tile size (cell minus inset on each side), so the
    // texture maps 1:1 to its destination rect.
    int desired = static_cast<int>(layout_.tile_w);
    if (desired <= 8) desired = 8;
    if (desired != baked_cell_w_) {
        tiles_.bake(sdl_renderer_, desired);
        if (empty_slot_tex_) SDL_DestroyTexture(empty_slot_tex_);
        empty_slot_tex_ = bake_empty_slot_texture(sdl_renderer_, desired, static_cast<int>(desired * 1.5f));
        baked_cell_w_ = desired;
    }

    // Restart button texture, baked oversize so the on-screen pulse animation
    // can scale it slightly larger without going past 1:1.
    int desired_restart = std::max(48, static_cast<int>(layout_.restart_r * 2.0f * 2.0f));
    if (desired_restart != baked_restart_size_) {
        if (restart_button_tex_) SDL_DestroyTexture(restart_button_tex_);
        restart_button_tex_ = bake_restart_button_texture(sdl_renderer_, desired_restart);
        baked_restart_size_ = desired_restart;
    }
}

bool Renderer::restart_button_contains(float x, float y) const noexcept {
    float dx = x - layout_.restart_cx;
    float dy = y - layout_.restart_cy;
    return dx * dx + dy * dy <= layout_.restart_hit_r * layout_.restart_hit_r;
}

float Renderer::cell_size_px() const noexcept {
    return layout_.cell_w;
}

float Renderer::pixel_density() const noexcept {
    if (!window_) return 1.0f;
    float d = SDL_GetWindowPixelDensity(window_);
    return (d > 0.0f) ? d : 1.0f;
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
            layout_.board_x + c * layout_.cell_w + layout_.tile_inset,
            layout_.board_y + r * layout_.cell_h + layout_.tile_inset,
            layout_.tile_w, layout_.tile_h,
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
        center_x - layout_.tile_w * 0.5f * scale_x,
        center_y - layout_.tile_h * 0.5f * scale_y,
        layout_.tile_w * scale_x,
        layout_.tile_h * scale_y,
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
    } else if (rank == current_max && rank >= 4) {
        // Highest tile gets a red callout — but only once we're past the
        // starting white tiles (value 6 and up).
        tr = 0xFF; tg = 0x66; tb = 0x80;
    } else if (rank == 15) {
        tr = 0xFF; tg = 0xFF; tb = 0xFF;
    }
    // Scale the tile number proportionally to the tile. Target line height
    // is ~50% of tile height; if the number is too wide for the tile (long
    // values like 6144 on a narrow tile), shrink to fit.
    const TextAtlas::Size sz = TextAtlas::Size::Large;
    float scale = (layout_.tile_h * 0.50f) / text_digits_.line_height(sz);
    float w = text_digits_.measure_width(buf, sz, scale);
    const float max_w = layout_.tile_w * 0.82f;
    if (w > max_w) scale *= max_w / w;
    float h = text_digits_.line_height(sz, scale);
    // Center text vertically in the upper ~78% of the tile (above the bottom band).
    float text_center_y = center_y - layout_.tile_h * 0.5f + (layout_.tile_h * 0.78f) * 0.5f;
    float baseline_y = text_center_y + h * 0.32f;
    // Pass scale_x / scale_y through to the text so the digit follows
    // whatever transform the tile texture has — most importantly the
    // restart flip's horizontal squish, but also (more subtly) the
    // merge bump and stuck-wall squish.
    text_digits_.draw_centered(sdl_renderer_, buf, sz, center_x, baseline_y,
                               tr, tg, tb, alpha, scale, scale_x, scale_y);
}

void Renderer::draw_tiles_(const game::GameState& state, const input::DragController& drag,
                           const Animations& anims) {
    // Restart flip overrides everything else — the board has just been
    // reseeded and we're showing each cell flip from its OLD rank (first half
    // of the per-cell phase, x squishing 1 → 0 around its vertical axis) to
    // its NEW rank (second half, x growing 0 → 1). Top-left → bottom-right
    // diagonal cascade. cos(p·π) gives the natural axis-rotation curve.
    if (anims.restart_flip.active) {
        const std::uint8_t current_max = state.board.max_rank();
        for (int i = 0; i < 16; ++i) {
            float p = anims.restart_flip.cell_phase(i);
            std::uint8_t rank = (p < 0.5f) ? anims.restart_flip.old_cells[i]
                                           : state.board.cells[i];
            if (rank == 0) continue;
            float scale_x = std::fabs(std::cos(p * 3.14159265f));
            int r = i / 4, c = i % 4;
            float cx = layout_.board_x + c * layout_.cell_w + layout_.cell_w * 0.5f;
            float cy = layout_.board_y + r * layout_.cell_h + layout_.cell_h * 0.5f;
            draw_one_tile_(rank, cx, cy, scale_x, 1.0f, 255, current_max);
        }
        return;
    }

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
        // Stuck-tile press: expand slightly along the swipe axis and pinch a
        // touch across it. Reads as the tile leaning into the wall.
        float along  = 1.0f + 0.08f * k;
        float cross  = 1.0f - 0.04f * k;
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

    // Score popups. Yellow fill (matches the white-tile edge band) with a
    // thin black halo so the "+N" reads against both white tiles (whose
    // dark digits would otherwise camouflage it) and blue/red tiles. We
    // peak the fill at full opacity AND keep the halo at half-alpha with
    // just four cardinal stamps; with the previous 8-stamp full-alpha halo
    // each glyph-center pixel was painted ~100% black before the yellow
    // landed, and at the popup's ≤220 fill alpha that bled through as ~14%
    // black, dimming pure yellow (255, 204, 102) to a mustard ~(219, 175, 88).
    for (const auto& p : anims.score_popups) {
        float u = p.t / p.dur;
        u = std::clamp(u, 0.0f, 1.0f);
        float dy = -ease(u, Easing::OutQuad) * 40.0f;
        std::uint8_t a      = static_cast<std::uint8_t>((1.0f - u) * 255.0f);
        std::uint8_t halo_a = static_cast<std::uint8_t>(a * 0.5f);
        char buf[20];
        std::size_t n;
        format_int(p.value, buf, sizeof(buf), n);
        std::string s = "+";
        s.append(buf, n);
        const float ox = p.x;
        const float oy = p.y + dy;
        constexpr float o = 2.0f;
        const float offs[4][2] = {{-o, 0}, {o, 0}, {0, -o}, {0, o}};
        for (const auto& off : offs) {
            text_.draw_centered(sdl_renderer_, s, TextAtlas::Size::Body,
                                ox + off[0], oy + off[1],
                                0x00, 0x00, 0x00, halo_a);
        }
        text_.draw_centered(sdl_renderer_, s, TextAtlas::Size::Body,
                            ox, oy, 0xFF, 0xCC, 0x66, a);
    }
}

void Renderer::draw_tally_(const Animations& anims) {
    if (anims.tally_labels.empty()) return;
    const float fade_in_sec = 0.20f;
    for (const auto& L : anims.tally_labels) {
        if (anims.tally_t < L.delay) continue;
        float age = anims.tally_t - L.delay;
        float fade_in = std::min(age / fade_in_sec, 1.0f);

        int row, col;
        to_rc(L.cell, row, col);
        float cx = layout_.board_x + col * layout_.cell_w + layout_.cell_w * 0.5f;
        float top_y = layout_.board_y + row * layout_.cell_h + layout_.tile_inset;

        char buf[24];
        std::size_t n;
        format_int(L.value, buf, sizeof(buf), n);
        // Compose "+N" — caller already filtered out 0-score tiles.
        char text[28];
        text[0] = '+';
        std::memcpy(text + 1, buf, n);
        std::string_view label(text, n + 1);

        // Distinctly smaller than the tile number — the +N is supporting
        // info, not the headline. ~22% of tile height (vs 50% for the tile),
        // allowed to spill up to 15% past the tile width.
        const TextAtlas::Size sz = TextAtlas::Size::Large;
        float scale = (layout_.tile_h * 0.22f) / text_digits_.line_height(sz);
        float label_w = text_digits_.measure_width(label, sz, scale);
        const float max_w = layout_.tile_w * 1.15f;
        if (label_w > max_w) scale *= max_w / label_w;

        // Position: the label sits ON the tile in its upper-most strip, with
        // just the tops of the glyphs peeking above the tile's top edge.
        float ascent   = text_digits_.baseline(sz, scale);
        float baseline = top_y + ascent * 0.78f;  // glyphs mostly on the tile
        baseline -= (1.0f - fade_in) * 6.0f;

        std::uint8_t a = static_cast<std::uint8_t>(fade_in * 255.0f);
        // Outline width scales with the font so the stroke stays proportional
        // to the glyph at every resolution. The 1.5 px floor keeps the halo
        // visible at very small scales (LINEAR-sampled edges have a fraction
        // of a dest-pixel of falloff when the source is heavily downsampled,
        // so even 1 px of shift produces a solid ring there); a higher floor
        // would balloon to ~20% of the glyph height on low-DPI windows.
        const float o = std::max(scale * 6.0f, 1.5f);
        const float d = o * 0.7071f;  // diagonals at same distance as cardinals
        const float offs[8][2] = {{-o, 0}, {o, 0}, {0, -o}, {0, o},
                                  {-d, -d}, {d, -d}, {-d, d}, {d, d}};
        for (const auto& off : offs) {
            text_digits_.draw_centered(sdl_renderer_, label, sz,
                                       cx + off[0], baseline + off[1],
                                       0x00, 0x00, 0x00, a, scale);
        }
        // Yellow fill (white-tile edge color).
        text_digits_.draw_centered(sdl_renderer_, label, sz, cx, baseline,
                                   0xFF, 0xCC, 0x66, a, scale);
    }
}

void Renderer::draw_hud_(const game::GameState& state, std::uint64_t high_score,
                          bool game_over, std::uint64_t game_over_age_ms) {
    // Top HUD spans y=[safe_top .. board_y] — i.e. the band between any OS
    // safe-area inset at the top of the window (status bar / notch / camera
    // cutout) and the top edge of the board. Lay out: a small label row at
    // the top, then preview card / score number / best number row.
    const float hud_top_h      = layout_.board_y - layout_.safe_top;
    const float label_baseline = layout_.safe_top
                               + std::max(20.0f, hud_top_h * 0.18f);
    const float small_h = text_.line_height(TextAtlas::Size::Small);

    // Preview card sized to the available HUD height (from label baseline
    // down to the top of the board).
    const float row_top    = label_baseline + small_h * 0.4f;
    const float row_height = (layout_.board_y - row_top) - 4.0f;
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

    // Preview card: same tile texture (rounded corners, edge shelf) as on the
    // board. Per Threes, the preview shows only the color (blue=1, red=2,
    // white=3 or bonus); bonus value is never revealed — just a "+".
    {
        std::uint8_t display_rank = state.next.is_bonus
            ? static_cast<std::uint8_t>(3) : state.next.rank;
        SDL_Texture* t = tiles_.texture_for(display_rank);
        SDL_FRect dst{left_anchor - preview_w * 0.5f, row_top, preview_w, preview_h};
        if (t) {
            SDL_SetTextureAlphaMod(t, 255);
            SDL_RenderTexture(sdl_renderer_, t, nullptr, &dst);
        }
        if (state.next.is_bonus) {
            // "+" sized to fit inside the preview tile body (upper ~88%).
            const TextAtlas::Size bsz = TextAtlas::Size::Large;
            float bscale = (preview_h * 0.55f) / text_.line_height(bsz);
            float bw = text_.measure_width("+", bsz, bscale);
            const float bmax_w = preview_w * 0.60f;
            if (bw > bmax_w) bscale *= bmax_w / bw;
            float bh = text_.line_height(bsz, bscale);
            float baseline = row_top + preview_h * 0.44f + bh * 0.32f;
            text_.draw_centered(sdl_renderer_, "+", bsz,
                                left_anchor, baseline, 0x2A, 0x2A, 0x2A, 255, bscale);
        }
    }

    // Score (centered). Sized to fit between the NEXT preview's right edge
    // and the BEST column on the right, with a small horizontal margin. The
    // baked Large size is ~96 px tall — without this scaling, a wide score
    // like "27639" paints right through the BEST column on low-resolution
    // windows. The 0.70 height target leaves enough horizontal slack on
    // narrow windows for 6-7 digit scores to fit at the same scale (a
    // maxed-out game reaches ~600k); on wide windows the width clamp below
    // dominates anyway, so this knob only shrinks the narrow case.
    char buf[24];
    std::size_t n;
    format_int(state.score, buf, sizeof(buf), n);
    std::string_view score_str(buf, n);
    {
        const TextAtlas::Size sz = TextAtlas::Size::Large;
        float scale = (row_height * 0.70f) / text_.line_height(sz);
        const float score_half = std::max(20.0f,
            center_anchor - left_anchor - preview_w * 0.5f - 8.0f);
        const float score_max_w = 2.0f * score_half;
        float meas = text_.measure_width(score_str, sz, scale);
        if (meas > score_max_w) scale *= score_max_w / meas;
        const float h_now = text_.line_height(sz, scale);
        text_.draw_centered(sdl_renderer_, score_str, sz,
                            center_anchor, row_top + (preview_h + h_now) * 0.5f,
                            0x2A, 0x2A, 0x2A, 255, scale);
    }

    // Best (right). Same scaling treatment, sized against its own column —
    // about as wide as the NEXT preview, plus a hair, so the three HUD
    // columns stay balanced. The width cap is ALSO bounded by the room
    // between right_anchor and the window's right edge: that anchor sits
    // close to the edge on narrow windows (`right_anchor` is one half a
    // preview shy of `win_w - board_x`), and without this clamp a wide
    // best score's right half extent runs into the margin.
    format_int(high_score, buf, sizeof(buf), n);
    std::string_view best_str(buf, n);
    {
        // Source from Large (96 px) rather than Body (48 px) — on iOS the HUD
        // row is tall enough that a Body-sourced glyph ends up scaled ~2×, well
        // into bilinear-upscale blur territory. The 0.55 row-height ratio
        // keeps BEST visibly smaller than the 0.70-ratio SCORE; same final
        // pixel size as before on iOS, just sourced from a sharper bake.
        const TextAtlas::Size sz = TextAtlas::Size::Large;
        float scale = (row_height * 0.55f) / text_.line_height(sz);
        constexpr float kRightPad = 12.0f;
        const float right_room =
            std::max(0.0f, layout_.win_w - kRightPad - right_anchor);
        const float best_max_w = std::max(20.0f,
            std::min(preview_w * 2.0f, 2.0f * right_room));
        float meas = text_.measure_width(best_str, sz, scale);
        if (meas > best_max_w) scale *= best_max_w / meas;
        const float h_now = text_.line_height(sz, scale);
        text_.draw_centered(sdl_renderer_, best_str, sz,
                            right_anchor, row_top + (preview_h + h_now) * 0.5f,
                            0x2A, 0x2A, 0x2A, 255, scale);
    }

    // Bottom: restart button. Always visible (single tap reseeds the game at
    // any time), but it shakes + pulses on game-over so the player's eye is
    // drawn to it now that the board has nothing else to react to.
    if (restart_button_tex_) {
        float anim_dx = 0.0f;
        float anim_scale = 1.0f;
        if (game_over) {
            float age_s = static_cast<float>(game_over_age_ms) * 0.001f;
            // Initial wiggle: ±4 px at ~14 Hz, decaying over the first ~1.5 s.
            float shake_amp = std::max(0.0f, 4.0f - age_s * 2.7f);
            anim_dx = shake_amp *
                      std::sin(age_s * 14.0f * 2.0f * 3.14159265f);
            // Gentle continuous pulse so the button keeps "breathing" after
            // the initial shake settles.
            anim_scale = 1.0f + 0.08f * std::sin(age_s * 4.5f);
        }
        const float r = layout_.restart_r * anim_scale;
        SDL_FRect dst{
            layout_.restart_cx + anim_dx - r,
            layout_.restart_cy - r,
            r * 2.0f,
            r * 2.0f,
        };
        SDL_SetTextureAlphaMod(restart_button_tex_, 255);
        SDL_RenderTexture(sdl_renderer_, restart_button_tex_, nullptr, &dst);
    }
}

void Renderer::draw(const game::GameState& state,
                    const input::DragController& drag,
                    const Animations& anims,
                    std::uint64_t high_score,
                    bool game_over,
                    std::uint64_t game_over_age_ms) {
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
    draw_tally_(anims);
    draw_particles(sdl_renderer_, anims);

    layout_.board_x -= shake_x;
    layout_.board_y -= shake_y;

    draw_hud_(state, high_score, game_over, game_over_age_ms);

    SDL_RenderPresent(sdl_renderer_);
}

}  // namespace triples::render
