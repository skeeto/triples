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

    // Draw a frame.
    void draw(const game::GameState& state,
              const input::DragController& drag,
              const Animations& anims,
              std::uint64_t high_score,
              bool game_over);

    // Convert a pointer (window-pixel) to a board cell index (0..15), or -1
    // if outside the board.
    int cell_for_point(float x, float y) const noexcept;

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
    SDL_Texture*      empty_slot_tex_ = nullptr;
    int               baked_cell_w_ = 0;   // px width the cache was baked at

    void recompute_layout_();
    void ensure_cache_();
    void draw_background_();
    void draw_empty_slots_();
    void draw_tiles_(const game::GameState& state, const input::DragController& drag,
                     const Animations& anims);
    void draw_one_tile_(std::uint8_t rank, float center_x, float center_y,
                        float scale_x, float scale_y, std::uint8_t alpha,
                        std::uint8_t current_max);
    void draw_hud_(const game::GameState& state, std::uint64_t high_score, bool game_over);
    void draw_tally_(const Animations& anims);
};

}  // namespace triples::render
