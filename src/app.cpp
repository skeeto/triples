#include "app.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <chrono>
#include <ctime>

#include "audio/sfx.hpp"
#include "game/score.hpp"

// The macOS trackpad shim is pure Cocoa/NSEvent — present on macOS only, NOT
// on iOS (where APPLE is also true, but UIKit replaces AppKit and there's no
// trackpad device anyway). Pin the include + call sites to TARGET_OS_OSX.
#if defined(__APPLE__) && !defined(__EMSCRIPTEN__)
#  include <TargetConditionals.h>
#  if TARGET_OS_OSX
#    define TRIPLES_USE_MACOS_TRACKPAD 1
#  endif
#endif

#ifdef TRIPLES_USE_MACOS_TRACKPAD
#  include "input/trackpad_macos.hpp"
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
App::~App() {
    if (pointer_cursor_) SDL_DestroyCursor(pointer_cursor_);
}

bool App::initialize() {
    if (!renderer_.initialize("Triples", 480, 800)) return false;
    // On web, the AudioContext underlying SDL3's audio device must be created
    // during a real user-gesture activation, or it stays suspended forever on
    // Firefox / mobile Safari. We defer mixer_.initialize() until the first
    // SDL input event arrives (see try_init_audio_on_first_gesture_).
#ifndef __EMSCRIPTEN__
    mixer_.initialize();  // desktop has no autoplay restriction
    audio_initialized_ = true;
#endif

#ifdef TRIPLES_USE_MACOS_TRACKPAD
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
    if (game_over_) {
        // If we resumed a game that was already over, replay the tally so the
        // user can see their final-score breakdown without having to play
        // through again. Don't engage the lockout — there was no recent
        // restart gesture to guard against.
        build_tally_();
    }

    // System hand cursor for the restart button. NULL on platforms that
    // don't support custom cursors; we just never SetCursor in that case.
    pointer_cursor_ = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_POINTER);

    last_tick_ms_ = SDL_GetTicks();
    return true;
}

void App::init_audio_now() {
    if (audio_initialized_) return;
    audio_initialized_ = true;
    mixer_.initialize();
}

void App::try_init_audio_on_first_gesture_(const SDL_Event& e) {
    if (audio_initialized_) return;
    // Fallback path: if the JS shell never calls into init_audio_now()
    // (e.g. desktop builds, or web builds where the export isn't accessible
    // yet), open the device when an activation-granting input event reaches
    // the main loop. The JS path is preferred because the resulting
    // AudioContext is created inside the gesture stack frame.
    bool is_gesture = (e.type == SDL_EVENT_MOUSE_BUTTON_UP ||
                       e.type == SDL_EVENT_FINGER_UP ||
                       e.type == SDL_EVENT_KEY_DOWN);
    if (!is_gesture) return;
    init_audio_now();
}

void App::handle_event_(const SDL_Event& e) {
    try_init_audio_on_first_gesture_(e);
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
            // Mid-flip the board is changing under the player's feet — no
            // sense letting them spam input until the cascade finishes.
            if (anims_.restart_flip.active) return;
            if (game_over_) {
                if (e.key.key == SDLK_RETURN || e.key.key == SDLK_SPACE
                    || e.key.key == SDLK_R) {
                    restart_with_flip_();
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
            // SDL3 on Emscripten synthesizes mouse events from every touch.
            // We let the dedicated FINGER events drive touch — otherwise the
            // same gesture would be double-processed AND we can't tell from
            // here whether other fingers are on the screen.
            if (e.button.which == SDL_TOUCH_MOUSEID) break;
            if (e.button.button == SDL_BUTTON_LEFT) {
                float x = e.button.x, y = e.button.y;
                apply_event_scale_(x, y);
                on_pointer_down_(x, y, input::PointerEvent::Source::Mouse);
            }
            break;
        case SDL_EVENT_MOUSE_BUTTON_UP:
            if (e.button.which == SDL_TOUCH_MOUSEID) break;
            if (e.button.button == SDL_BUTTON_LEFT) {
                float x = e.button.x, y = e.button.y;
                apply_event_scale_(x, y);
                on_pointer_up_(x, y, input::PointerEvent::Source::Mouse);
            }
            break;
        case SDL_EVENT_MOUSE_MOTION:
            if (e.motion.which == SDL_TOUCH_MOUSEID) break;
            if (e.motion.state & SDL_BUTTON_LMASK) {
                float x = e.motion.x, y = e.motion.y;
                apply_event_scale_(x, y);
                on_pointer_move_(x, y, input::PointerEvent::Source::Mouse);
            }
            break;
        case SDL_EVENT_FINGER_DOWN: {
            ++active_touch_fingers_;
            if (active_touch_fingers_ >= 2) {
                // Second (or later) finger — this is a pinch / multi-touch
                // gesture. Cancel any in-progress single-finger drag so it
                // doesn't keep tracking the first finger during the pinch.
                input::PointerEvent ev{input::PointerKind::Cancel, 0.0f, 0.0f,
                                       input::PointerEvent::Source::Touch};
                drag_.on_pointer(ev, state_.board);
                break;
            }
            float w = renderer_.layout().win_w;
            float h = renderer_.layout().win_h;
            on_pointer_down_(e.tfinger.x * w, e.tfinger.y * h, input::PointerEvent::Source::Touch);
            break;
        }
        case SDL_EVENT_FINGER_UP:
        case SDL_EVENT_FINGER_CANCELED: {
            if (active_touch_fingers_ > 0) --active_touch_fingers_;
            // Only synthesize a pointer-up when the FINAL finger lifts and
            // we were running a single-finger drag (active_touch_fingers_
            // would have been 1 before this decrement, never >=2 mid-pinch).
            if (active_touch_fingers_ == 0) {
                float w = renderer_.layout().win_w;
                float h = renderer_.layout().win_h;
                on_pointer_up_(e.tfinger.x * w, e.tfinger.y * h, input::PointerEvent::Source::Touch);
            }
            break;
        }
        case SDL_EVENT_FINGER_MOTION: {
            // Drop multi-touch motion — only single-finger motion drives the
            // drag controller.
            if (active_touch_fingers_ > 1) break;
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
    // Block input while the restart cascade is animating — same reason as
    // the KEY_DOWN guard above.
    if (anims_.restart_flip.active) return;
    // Restart button takes precedence over board input at any time — pressing
    // it always restarts. The drag controller stays Idle (no Down dispatched),
    // so the gesture's subsequent move/up events are silently dropped.
    if (renderer_.restart_button_contains(x, y)) {
        // Mobile web delivers one touch as FINGER_DOWN → FINGER_UP →
        // synthesized MOUSE_BUTTON_DOWN → MOUSE_BUTTON_UP (not nested),
        // so a flag would clear too early. Time-based debounce: ignore
        // restart presses landing within kRestartDebounceMs of the last.
        const std::uint64_t now = SDL_GetTicks();
        if (now - last_restart_at_ms_ >= kRestartDebounceMs) {
            last_restart_at_ms_ = now;
            restart_with_flip_();
        }
        return;
    }
    if (game_over_) return;  // board is frozen; only the button is live now
    input::PointerEvent ev{input::PointerKind::Down, x, y, src};
    drag_.cell_size_px  = renderer_.cell_size_px();
    drag_.pixel_density = renderer_.pixel_density();
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
    game_over_at_ms_ = SDL_GetTicks();
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

    build_tally_();
}

void App::build_tally_() {
    // One "+N" per scoring tile (rank >= 3), staggered so they appear one
    // after another.
    std::vector<render::ScoreTallyLabel> labels;
    labels.reserve(16);
    int order = 0;
    constexpr float kStaggerSec = 0.10f;
    for (int i = 0; i < 16; ++i) {
        std::uint8_t r = state_.board.cells[i];
        if (r < 3) continue;  // empty / 1 / 2 score zero
        render::ScoreTallyLabel L;
        L.cell  = i;
        L.value = game::kTileScores[r];
        L.delay = static_cast<float>(order) * kStaggerSec;
        labels.push_back(L);
        ++order;
    }
    anims_.start_tally(std::move(labels));
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

void App::restart_with_flip_() {
    // Snapshot the OLD board BEFORE we wipe state — the renderer reads it
    // through Animations::restart_flip during the cascading flip.
    auto old_cells = state_.board.cells;
    start_new_game_();          // clears anims, resets drag, picks fresh seed
    anims_.start_restart_flip(old_cells);
    // Ascending arpeggio synced to the cascade (one blip per diagonal).
    mixer_.play(audio::Sfx::RestartFlip, 0.7f);
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
    // SDL3 on macOS retina (and any platform with SDL_WINDOW_HIGH_PIXEL_DENSITY
    // on a HiDPI display) delivers mouse events in *logical* pixels while the
    // renderer's layout is in *device* pixels. Multiply by the window's
    // pixel-density factor so the two end up in the same units — without
    // this, point-based hit-tests (like the restart button) miss by the dpr
    // factor, and the drag controller's `f = along / cell_size_px` ratio is
    // off by the same factor (less obviously, since drags use deltas).
    float s = renderer_.pixel_density();
    if (s > 0.0f && s != 1.0f) {
        x *= s;
        y *= s;
    }
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

#ifdef TRIPLES_USE_MACOS_TRACKPAD
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
    std::uint64_t game_over_age_ms =
        (game_over_ && game_over_at_ms_ > 0 && now_ms >= game_over_at_ms_)
            ? (now_ms - game_over_at_ms_) : 0;
    renderer_.draw(state_, drag_, anims_, best_score_, game_over_, game_over_age_ms);

    // Hand cursor when the mouse is over the restart button. The default
    // cursor is restored when the mouse leaves. Touch-only platforms still
    // have a tracked mouse position but no visible cursor, so this is a
    // no-op there in practice.
    if (pointer_cursor_) {
        float mx = 0.0f, my = 0.0f;
        SDL_GetMouseState(&mx, &my);
        apply_event_scale_(mx, my);
        const bool over = renderer_.restart_button_contains(mx, my);
        if (over != cursor_over_button_) {
            cursor_over_button_ = over;
            SDL_SetCursor(over ? pointer_cursor_ : SDL_GetDefaultCursor());
        }
    }

    return running_;
}

}  // namespace triples
