#include "app.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <chrono>
#include <ctime>

#include "audio/sfx.hpp"
#include "game/score.hpp"

#if defined(__APPLE__) && !defined(__EMSCRIPTEN__)
#include "input/trackpad_macos.hpp"
#endif

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
// SDL3's Emscripten backend delivers mouse/pointer event coordinates in CSS
// pixels (the canvas's logical layout space), while the renderer's layout is
// computed in device pixels (cell sizes scale with devicePixelRatio). We need
// the scale factor to keep them consistent.
EM_JS(double, ts_canvas_pixel_ratio, (), {
    var c = document.getElementById('canvas');
    if (!c) return 1.0;
    var rect = c.getBoundingClientRect();
    return (rect.width > 0) ? (c.width / rect.width) : 1.0;
});
EM_JS(int, ts_canvas_pixel_width, (), {
    var c = document.getElementById('canvas');
    return c ? c.width : 0;
});
EM_JS(int, ts_canvas_pixel_height, (), {
    var c = document.getElementById('canvas');
    return c ? c.height : 0;
});
#endif

namespace triples {

namespace {

constexpr const char* kKeyState      = "game.current";
constexpr const char* kKeyHighScores = "game.highscores";

input::PointerEvent::Source source_for_touch(bool is_touch) {
    return is_touch ? input::PointerEvent::Source::Touch
                    : input::PointerEvent::Source::Mouse;
}

}  // namespace

App::App() = default;
App::~App() = default;

bool App::initialize() {
    if (!renderer_.initialize("Triples", 480, 800)) return false;
    mixer_.initialize();  // soft failure ok — game still playable without audio

#if defined(__APPLE__) && !defined(__EMSCRIPTEN__)
    input::macos_trackpad_init(renderer_.sdl_window());
#endif

    store_ = platform::make_store();

    // Try to restore the saved game state. Fall back to a fresh start if any
    // step fails.
    bool loaded = false;
    if (auto txt = store_->read(kKeyState)) {
        if (auto s = game::deserialize_state(*txt)) {
            state_ = *s;
            loaded = true;
        }
    }
    if (!loaded) {
        std::uint64_t seed =
            static_cast<std::uint64_t>(std::chrono::system_clock::now().time_since_epoch().count());
        state_ = game::initial_state(seed);
        save_state_();
    }

    load_highscores_();
    recompute_best_();

    game_over_ = state_.is_game_over();

    last_tick_ms_ = SDL_GetTicks();
    return true;
}

void App::handle_event_(const SDL_Event& e) {
    switch (e.type) {
        case SDL_EVENT_QUIT:
            running_ = false;
            break;
        case SDL_EVENT_WINDOW_RESIZED:
        case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED: {
            int w, h;
            SDL_GetWindowSizeInPixels(renderer_.sdl_window(), &w, &h);
            on_window_resize(w, h);
            break;
        }
        case SDL_EVENT_KEY_DOWN: {
            if (game_over_) {
                if (e.key.key == SDLK_RETURN || e.key.key == SDLK_SPACE
                    || e.key.key == SDLK_R) {
                    start_new_game_();
                }
                return;
            }
            game::Direction d;
            switch (e.key.key) {
                case SDLK_UP:    case SDLK_W: d = game::Direction::Up;    break;
                case SDLK_DOWN:  case SDLK_S: d = game::Direction::Down;  break;
                case SDLK_LEFT:  case SDLK_A: d = game::Direction::Left;  break;
                case SDLK_RIGHT: case SDLK_D: d = game::Direction::Right; break;
                default: return;
            }
            drag_.cell_size_px = renderer_.cell_size_px();
            drag_.start_external_commit(d, state_.board);
            break;
        }
        case SDL_EVENT_MOUSE_BUTTON_DOWN:
            if (e.button.button == SDL_BUTTON_LEFT) {
                float x = e.button.x, y = e.button.y;
                apply_event_scale_(x, y);
                on_pointer_down_(x, y, input::PointerEvent::Source::Mouse);
            }
            break;
        case SDL_EVENT_MOUSE_BUTTON_UP:
            if (e.button.button == SDL_BUTTON_LEFT) {
                float x = e.button.x, y = e.button.y;
                apply_event_scale_(x, y);
                on_pointer_up_(x, y, input::PointerEvent::Source::Mouse);
            }
            break;
        case SDL_EVENT_MOUSE_MOTION:
            if (e.motion.state & SDL_BUTTON_LMASK) {
                float x = e.motion.x, y = e.motion.y;
                apply_event_scale_(x, y);
                on_pointer_move_(x, y, input::PointerEvent::Source::Mouse);
            }
            break;
        case SDL_EVENT_FINGER_DOWN: {
            float w = renderer_.layout().win_w;
            float h = renderer_.layout().win_h;
            on_pointer_down_(e.tfinger.x * w, e.tfinger.y * h, input::PointerEvent::Source::Touch);
            break;
        }
        case SDL_EVENT_FINGER_UP: {
            float w = renderer_.layout().win_w;
            float h = renderer_.layout().win_h;
            on_pointer_up_(e.tfinger.x * w, e.tfinger.y * h, input::PointerEvent::Source::Touch);
            break;
        }
        case SDL_EVENT_FINGER_MOTION: {
            float w = renderer_.layout().win_w;
            float h = renderer_.layout().win_h;
            on_pointer_move_(e.tfinger.x * w, e.tfinger.y * h, input::PointerEvent::Source::Touch);
            break;
        }
        default:
            break;
    }
}

void App::on_pointer_down_(float x, float y, input::PointerEvent::Source src) {
    if (game_over_) {
        // Tap anywhere → restart.
        start_new_game_();
        return;
    }
    input::PointerEvent ev{input::PointerKind::Down, x, y, src};
    drag_.cell_size_px = renderer_.cell_size_px();
    drag_.on_pointer(ev, state_.board);
}

void App::on_pointer_move_(float x, float y, input::PointerEvent::Source src) {
    if (game_over_) return;
    input::PointerEvent ev{input::PointerKind::Move, x, y, src};
    drag_.on_pointer(ev, state_.board);
}

void App::on_pointer_up_(float x, float y, input::PointerEvent::Source src) {
    if (game_over_) return;
    input::PointerEvent ev{input::PointerKind::Up, x, y, src};
    drag_.on_pointer(ev, state_.board);
}

void App::apply_commit_() {
    const auto pre_max = state_.max_rank_seen;
    const auto dry = drag_.dry;

    // Collect merged cells (those with a merge_partner_origin != -1).
    struct MergeInfo { int cell; std::uint8_t rank; std::uint64_t score; };
    std::vector<MergeInfo> merges;
    merges.reserve(4);
    for (int i = 0; i < 16; ++i) {
        if (dry.merge_partner_origin[i] >= 0) {
            std::uint8_t r = dry.new_board.cells[i];
            merges.push_back({i, r, game::kTileScores[r]});
        }
    }

    int spawn = game::apply_move(state_, drag_.dir, dry);
    save_state_();

    if (spawn >= 0) anims_.add_spawn_fade(spawn);
    std::uint8_t max_merge_rank = 0;
    for (const auto& m : merges) {
        anims_.add_merge_bump(m.cell);
        float cx, cy;
        cell_center_(m.cell, cx, cy);
        if (m.score > 0) anims_.add_score_popup(cx, cy, m.score);
        if (m.rank > max_merge_rank) max_merge_rank = m.rank;
    }

    if (max_merge_rank >= 9) {
        float amp = 2.0f + (max_merge_rank - 9) * 1.5f;
        anims_.add_screen_shake(amp, 0.18f);
    }

    // SFX.
    mixer_.play(audio::Sfx::Whoosh, 0.6f);
    if (!merges.empty()) {
        mixer_.play(audio::merge_sfx_for_rank(max_merge_rank), 0.9f);
    }

    if (state_.max_rank_seen > pre_max && state_.max_rank_seen >= 4) {
        // Sparkle on the cell of the new max — there's a unique cell, since
        // the merge produced it.
        for (int i = 0; i < 16; ++i) {
            if (state_.board.cells[i] == state_.max_rank_seen) {
                float cx, cy;
                cell_center_(i, cx, cy);
                std::uint8_t r = 0xFF, g = 0xC8, b = 0x4D;
                if (state_.max_rank_seen == 1)      { r = 0x00; g = 0x99; b = 0xCC; }
                else if (state_.max_rank_seen == 2) { r = 0xE6; g = 0x45; b = 0x45; }
                anims_.emit_sparkles(cx, cy, r, g, b);
                break;
            }
        }
        mixer_.play(audio::Sfx::NewMax, 0.8f);
    }

    if (state_.max_rank_seen == 15) {
        anims_.emit_confetti(0.0f, renderer_.layout().win_w);
    }

    if (state_.is_game_over()) {
        on_game_over_();
    }
}

void App::on_game_over_() {
    game_over_ = true;
    mixer_.play(audio::Sfx::GameOver, 0.9f);

    // Insert into high score list.
    game::HighScore entry;
    entry.score = state_.score;
    entry.date = today_iso_date_();
    highscores_.push_back(entry);
    std::sort(highscores_.begin(), highscores_.end(),
              [](const game::HighScore& a, const game::HighScore& b) {
                  return a.score > b.score;
              });
    if (highscores_.size() > 8) highscores_.resize(8);
    save_highscores_();
    recompute_best_();
}

void App::start_new_game_() {
    std::uint64_t seed =
        static_cast<std::uint64_t>(std::chrono::system_clock::now().time_since_epoch().count());
    state_ = game::initial_state(seed);
    anims_.clear();
    drag_ = input::DragController{};
    game_over_ = false;
    save_state_();
}

void App::save_state_() {
    if (!store_) return;
    store_->write(kKeyState, game::serialize_state(state_));
}

void App::save_highscores_() {
    if (!store_) return;
    store_->write(kKeyHighScores,
                  game::serialize_highscores(highscores_.data(), highscores_.size()));
}

void App::load_highscores_() {
    highscores_.clear();
    if (!store_) return;
    if (auto txt = store_->read(kKeyHighScores)) {
        highscores_ = game::deserialize_highscores(*txt);
    }
}

void App::cell_center_(int cell, float& cx, float& cy) const {
    int r = cell / 4, c = cell % 4;
    const auto& L = renderer_.layout();
    cx = L.board_x + c * L.cell_w + L.cell_w * 0.5f;
    cy = L.board_y + r * L.cell_h + L.cell_h * 0.5f;
}

std::string App::today_iso_date_() {
    auto now = std::chrono::system_clock::now();
    std::time_t t = std::chrono::system_clock::to_time_t(now);
    std::tm tm{};
#if defined(_WIN32)
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif
    char buf[11];
    std::snprintf(buf, sizeof(buf), "%04d-%02d-%02d",
                  tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday);
    return std::string(buf, 10);
}

void App::recompute_best_() {
    best_score_ = state_.score;
    for (const auto& h : highscores_) {
        if (h.score > best_score_) best_score_ = h.score;
    }
}

void App::on_window_resize(int w, int h) {
    renderer_.set_logical_size(w, h);
}

void App::apply_event_scale_(float& x, float& y) const noexcept {
#ifdef __EMSCRIPTEN__
    double s = ts_canvas_pixel_ratio();
    x = static_cast<float>(x * s);
    y = static_cast<float>(y * s);
#else
    (void)x; (void)y;
#endif
}

void App::sync_canvas_size_() {
#ifdef __EMSCRIPTEN__
    int cw = ts_canvas_pixel_width();
    int ch = ts_canvas_pixel_height();
    if (cw <= 0 || ch <= 0) return;
    const auto& L = renderer_.layout();
    if (static_cast<int>(L.win_w) != cw || static_cast<int>(L.win_h) != ch) {
        renderer_.set_logical_size(cw, ch);
    }
#endif
}

bool App::tick() {
    sync_canvas_size_();
    SDL_Event e;
    while (SDL_PollEvent(&e)) handle_event_(e);

#if defined(__APPLE__) && !defined(__EMSCRIPTEN__)
    // Drain trackpad gesture events into synthetic pointer events. The shim
    // gives deltas; we anchor at the board center so the drag controller has
    // a sensible start position regardless of where the cursor sits.
    input::TrackpadEvent te;
    while (input::macos_trackpad_poll(&te)) {
        float cx = renderer_.layout().board_x + renderer_.layout().board_w * 0.5f;
        float cy = renderer_.layout().board_y + renderer_.layout().board_h * 0.5f;
        input::PointerEvent::Source src = input::PointerEvent::Source::Trackpad;
        switch (te.phase) {
            case input::TrackpadEvent::Phase::Began:
                on_pointer_down_(cx, cy, src);
                break;
            case input::TrackpadEvent::Phase::Changed:
                on_pointer_move_(cx + te.dx, cy + te.dy, src);
                break;
            case input::TrackpadEvent::Phase::Ended:
                on_pointer_up_(cx + te.dx, cy + te.dy, src);
                break;
        }
    }
#endif

    std::uint64_t now_ms = SDL_GetTicks();
    float dt = static_cast<float>(now_ms - last_tick_ms_) / 1000.0f;
    if (dt > 0.1f) dt = 0.1f;  // cap to keep animations sane on tab-resume
    last_tick_ms_ = now_ms;

    drag_.step(dt);
    if (drag_.commit_ready) {
        drag_.commit_ready = false;
        apply_commit_();
    }
    anims_.step(dt);
    mixer_.poll();

    if (best_score_ < state_.score) best_score_ = state_.score;
    renderer_.draw(state_, drag_, anims_, best_score_, game_over_);

    return running_;
}

}  // namespace triples
