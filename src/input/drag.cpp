#include "input/drag.hpp"

#include <algorithm>
#include <cmath>

namespace triples::input {

namespace {

inline float clamp01(float v) noexcept {
    return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v);
}

inline float ease_out_quad(float t) noexcept {
    float u = 1.0f - t;
    return 1.0f - u * u;
}

}  // namespace

float DragController::signed_along(float dx, float dy, game::Direction d) noexcept {
    switch (d) {
        case game::Direction::Right: return dx;
        case game::Direction::Left:  return -dx;
        case game::Direction::Down:  return dy;
        case game::Direction::Up:    return -dy;
    }
    return 0.0f;
}

void DragController::start_anim(float dur, float from, float to) noexcept {
    anim_t = 0.0f;
    anim_dur = dur;
    anim_from_f = from;
    anim_to_f = to;
}

void DragController::on_pointer(const PointerEvent& e, const game::Board& board) noexcept {
    switch (e.kind) {
        case PointerKind::Down: {
            // Only start a new drag from Idle. Mobile web can deliver the same
            // touch as both FINGER_DOWN and synthetic MOUSE_BUTTON_DOWN; we
            // must accept only one of them, not reset state on the second.
            if (state != DragState::Idle) return;
            state = DragState::AwaitingLock;
            start_x = cur_x = e.x;
            start_y = cur_y = e.y;
            f = 0.0f;
            break;
        }
        case PointerKind::Move: {
            cur_x = e.x;
            cur_y = e.y;
            if (state == DragState::AwaitingLock) {
                float dx  = cur_x - start_x;
                float dy  = cur_y - start_y;
                float adx = std::fabs(dx);
                float ady = std::fabs(dy);
                float major = std::max(adx, ady);
                float minor = std::min(adx, ady);
                // Grace radius: roughly a thumb-jitter (~3mm). During this
                // zone the gesture has no axis yet and the tiles don't move.
                const float deadzone = kLockDeadZoneCss * pixel_density;
                if (major < deadzone) return;
                // Once past the grace radius, lock to the dominant axis only
                // if it's clearly dominant (1.5× the minor) or the gesture
                // has grown large enough that we can't keep waiting (3× the
                // grace radius). This stops a one-pixel perpendicular jitter
                // at the start from hijacking the intended direction.
                if (major < minor * 1.5f && major < deadzone * 3.0f) return;
                if (adx > ady) {
                    dir = (dx > 0.0f) ? game::Direction::Right : game::Direction::Left;
                } else {
                    dir = (dy > 0.0f) ? game::Direction::Down  : game::Direction::Up;
                }
                dry = game::resolve(board, dir);
                state = DragState::Dragging;
            }
            if (state == DragState::Dragging) {
                float along = signed_along(cur_x - start_x, cur_y - start_y, dir);
                f = clamp01(along / cell_size_px);
            }
            break;
        }
        case PointerKind::Up: {
            // Only react if we're actually dragging. A duplicate Up (e.g., from
            // the synthetic mouse-from-touch on web) must NOT corrupt the
            // already-running commit/cancel animation.
            if (state == DragState::AwaitingLock) {
                state = DragState::Idle;
                f = 0.0f;
                return;
            }
            if (state != DragState::Dragging) return;
            if (dry.any_change && f >= kCommitThreshold) {
                start_anim(kCommitDuration, f, 1.0f);
                state = DragState::Committing;
            } else {
                start_anim(kCancelDuration, f, 0.0f);
                state = DragState::Canceling;
            }
            break;
        }
        case PointerKind::Cancel: {
            if (state == DragState::Dragging) {
                start_anim(kCancelDuration, f, 0.0f);
                state = DragState::Canceling;
            } else if (state == DragState::AwaitingLock) {
                state = DragState::Idle;
                f = 0.0f;
            }
            // else: leave Committing/Canceling alone.
            break;
        }
    }
}

void DragController::start_external_commit(game::Direction d, const game::Board& board) noexcept {
    if (state != DragState::Idle) return;
    dir = d;
    dry = game::resolve(board, dir);
    if (!dry.any_change) return;
    f = 0.0f;
    start_anim(kCommitDuration, 0.0f, 1.0f);
    state = DragState::Committing;
}

void DragController::step(float dt) noexcept {
    if (state != DragState::Committing && state != DragState::Canceling) return;
    anim_t += dt;
    float u = (anim_dur > 0.0f) ? clamp01(anim_t / anim_dur) : 1.0f;
    float eased = ease_out_quad(u);
    f = anim_from_f + (anim_to_f - anim_from_f) * eased;
    if (anim_t >= anim_dur) {
        if (state == DragState::Committing) {
            commit_ready = true;
            // Caller will apply the move and call reset_after_commit() (or
            // simply set state back to Idle by zeroing the controller).
        }
        state = DragState::Idle;
        f = 0.0f;
    }
}

}  // namespace triples::input
