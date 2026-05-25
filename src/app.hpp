#pragma once
#include <cstdint>
#include <memory>
#include <vector>

#include "audio/mixer.hpp"
#include "game/game_state.hpp"
#include "game/serialize.hpp"
#include "input/drag.hpp"
#include "platform/persistence.hpp"
#include "render/animation.hpp"
#include "render/renderer.hpp"

union SDL_Event;

namespace triples {

class App {
public:
    App();
    ~App();

    // Initialize SDL, create the window, load persisted state. Returns false
    // on fatal failure.
    bool initialize();

    // Run one frame: poll events, advance animations, render.
    // Returns false if the user has quit (desktop). The web loop ignores this
    // and runs forever (emscripten_set_main_loop drives it).
    bool tick();

    // Public for the Emscripten main loop trampoline in main.cpp.
    void on_window_resize(int w, int h);

private:
    render::Renderer            renderer_;
    audio::Mixer                mixer_;
    std::unique_ptr<platform::PersistenceStore> store_;
    game::GameState             state_;
    input::DragController       drag_;
    render::Animations          anims_;
    std::vector<game::HighScore> highscores_;
    std::uint64_t               best_score_ = 0;
    bool                        running_ = true;
    bool                        game_over_ = false;
    std::uint64_t               last_tick_ms_ = 0;

    void handle_event_(const SDL_Event& e);
    void on_pointer_down_(float x, float y, input::PointerEvent::Source src);
    void on_pointer_move_(float x, float y, input::PointerEvent::Source src);
    void on_pointer_up_(float x, float y, input::PointerEvent::Source src);

    // Web: convert SDL3 mouse coordinates (CSS pixels) to device pixels.
    // No-op on desktop platforms.
    void apply_event_scale_(float& x, float& y) const noexcept;
    // Web: poll the canvas's actual pixel dimensions each frame and resync the
    // renderer's layout if they changed. SDL3 doesn't always emit
    // WINDOW_PIXEL_SIZE_CHANGED on canvas resize.
    void sync_canvas_size_();

    void apply_commit_();
    void on_game_over_();
    void start_new_game_();
    void save_state_();
    void save_highscores_();
    void load_highscores_();

    void cell_center_(int cell, float& cx, float& cy) const;
    static std::string today_iso_date_();
    void recompute_best_();
};

}  // namespace triples
