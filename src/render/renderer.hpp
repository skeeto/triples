#pragma once
#include <cstdint>
#include <memory>

#include "game/game_state.hpp"
#include "input/drag.hpp"
#include "render/animation.hpp"
#include "render/text.hpp"
#include "render/tile_draw.hpp"

struct SDL_Window;
struct SDL_Renderer;

namespace triples::render {

// Layout in device pixels for the current frame.
struct Layout {
    float win_w = 0.0f, win_h = 0.0f;
    float cell_w = 0.0f, cell_h = 0.0f;     // slot the tile lives in (2:3 ratio)
    // The tile itself is slightly smaller than its cell so there's a visible
    // gap between adjacent tiles. cell rect = (cell_x, cell_y, cell_w, cell_h);
    // tile rect = the same inset by tile_inset on every side.
    float tile_inset = 0.0f;
    float tile_w = 0.0f, tile_h = 0.0f;
    float board_x = 0.0f, board_y = 0.0f;   // top-left of board area in pixels
    float board_w = 0.0f, board_h = 0.0f;
    float hud_top_y = 0.0f;                 // baseline area for top HUD
    float hud_bottom_y = 0.0f;

    // Restart button — circular, sits in the bottom HUD strip. `restart_r` is
    // the visible radius; `restart_hit_r` is a slightly larger radius used for
    // pointer hit-testing so the tap target stays generous on touch screens.
    float restart_cx = 0.0f, restart_cy = 0.0f;
    float restart_r = 0.0f;
    float restart_hit_r = 0.0f;
};

class Renderer {
public:
    Renderer();
    ~Renderer();

    // Create window + renderer. Returns false on failure.
    bool initialize(const char* title, int initial_w, int initial_h);

    // Set the device-pixel logical size (call when window/canvas resizes).
    // For HiDPI, pass the device-pixel dimensions, not the CSS-pixel ones.
    void set_logical_size(int w, int h);

    // Draw a frame. `game_over_age_ms` is milliseconds since the game ended;
    // used to drive the restart button's attention-grabbing shake/pulse.
    // Ignored when `game_over` is false.
    void draw(const game::GameState& state,
              const input::DragController& drag,
              const Animations& anims,
              std::uint64_t high_score,
              bool game_over,
              std::uint64_t game_over_age_ms);

    // Convert a pointer (window-pixel) to a board cell index (0..15), or -1
    // if outside the board.
    int cell_for_point(float x, float y) const noexcept;

    // True when the given window-pixel point falls inside the restart button's
    // hit area. The hit area is slightly larger than the visible disc to keep
    // the tap target comfortable on touch screens.
    bool restart_button_contains(float x, float y) const noexcept;

    // Pixel size of one tile in the current layout — fed back into the drag
    // controller each frame.
    float cell_size_px() const noexcept;

    // Device pixels per CSS pixel. Used by the drag controller to keep its
    // lock dead-zone roughly constant in physical units across DPI scales.
    float pixel_density() const noexcept;

    SDL_Renderer* sdl_renderer() const noexcept { return sdl_renderer_; }
    SDL_Window*   sdl_window()   const noexcept { return window_; }

    // For external use (e.g., tests, debug overlays).
    const Layout& layout() const noexcept { return layout_; }

private:
    SDL_Window*   window_       = nullptr;
    SDL_Renderer* sdl_renderer_ = nullptr;
    int           logical_w_    = 0;
    int           logical_h_    = 0;
    Layout        layout_{};

    TextAtlas         text_;          // Inter-Bold for HUD / labels.
    TextAtlas         text_digits_;   // Jua for tile values + tally "+N".
    TileTextureCache  tiles_;
    SDL_Texture*      empty_slot_tex_     = nullptr;
    SDL_Texture*      restart_button_tex_ = nullptr;
    int               baked_cell_w_       = 0;   // px width the tile cache was baked at
    int               baked_restart_size_ = 0;   // px diameter the button tex was baked at

    void recompute_layout_();
    void ensure_cache_();
    void draw_background_();
    void draw_empty_slots_();
    void draw_tiles_(const game::GameState& state, const input::DragController& drag,
                     const Animations& anims);
    void draw_one_tile_(std::uint8_t rank, float center_x, float center_y,
                        float scale_x, float scale_y, std::uint8_t alpha,
                        std::uint8_t current_max);
    void draw_hud_(const game::GameState& state, std::uint64_t high_score,
                   bool game_over, std::uint64_t game_over_age_ms);
    void draw_tally_(const Animations& anims);
};

}  // namespace triples::render
