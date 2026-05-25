#pragma once
#include <cstdint>

#include "game/board.hpp"
#include "game/move.hpp"
#include "input/input.hpp"

namespace triples::input {

// Drag-based swipe state machine. The hard part of feeling like the original
// Threes!. The renderer reads `dir`, `f`, and `dry` each frame to draw the
// in-progress slide overlap. The state machine itself does no per-frame work
// outside of the snap/commit animations.
//
// States:
//   Idle          → AwaitingLock (on pointer down on board)
//   AwaitingLock  → Dragging (once finger has moved > 6 px in either axis)
//   Dragging      → Committing (on release with f >= 0.25)
//                 → Canceling  (on release with f < 0.25, or pointer cancel)
//   Committing    → Idle (after commit animation; app applies the move)
//   Canceling     → Idle (after snap-back animation)
enum class DragState : std::uint8_t {
    Idle,
    AwaitingLock,
    Dragging,
    Committing,
    Canceling,
};

struct DragController {
    DragState         state = DragState::Idle;
    game::Direction   dir   = game::Direction::Right;  // valid only in Dragging / Committing
    float             start_x = 0.0f, start_y = 0.0f;
    float             cur_x = 0.0f,   cur_y = 0.0f;

    // Overlap fraction in [0, 1]. f=0.25 is the commit threshold. Renderer
    // reads this every frame.
    float             f = 0.0f;

    // Animation-time interpolation (used during Committing and Canceling).
    float             anim_t = 0.0f;       // elapsed within the current animation
    float             anim_dur = 0.0f;     // total duration of the current animation
    float             anim_from_f = 0.0f;
    float             anim_to_f = 0.0f;

    // Pre-computed dry-run of the move for the locked direction. The renderer
    // uses this to know which tiles slide.
    game::MoveResult  dry{};

    // Cell size in *device pixels*. The app sets this every frame from the
    // current layout. Used to convert finger displacement into overlap.
    float             cell_size_px = 100.0f;

    // Device-pixels-per-CSS-pixel (≈ window.devicePixelRatio). The app sets
    // this so the lock dead zone and grace radius track a real physical
    // distance instead of shrinking on high-DPI screens.
    float             pixel_density = 1.0f;

    // Threshold for committing the move (fraction of one cell).
    static constexpr float kCommitThreshold = 0.25f;
    // Dead zone before direction-lock, expressed in CSS pixels (≈ 1/96 inch
    // at nominal viewing distance, so ~3mm). Scaled by pixel_density at use.
    static constexpr float kLockDeadZoneCss = 12.0f;
    // Animation durations (seconds).
    static constexpr float kCommitDuration  = 0.08f;
    static constexpr float kCancelDuration  = 0.12f;

    // Returns true if the drag is active (Dragging / Committing / Canceling)
    // — the renderer uses this to apply slide offsets.
    bool active() const noexcept {
        return state == DragState::Dragging
            || state == DragState::Committing
            || state == DragState::Canceling;
    }

    // Called whenever a pointer event lands on the board. Returns nothing —
    // observe `state` and the on_commit flag (set inside step()) for outcomes.
    void on_pointer(const PointerEvent& e, const game::Board& board) noexcept;

    // Per-frame tick. Drives commit / cancel animations. When a commit
    // animation completes, sets `commit_ready` to true; caller must apply
    // dry to the game state and clear the flag.
    void step(float dt) noexcept;

    // Externally trigger a commit (e.g. from a keyboard arrow). Resolves the
    // dry-run, animates the slide from f=0 to f=1, and ends in COMMITTING.
    // No-op if not Idle or if the move is rejected.
    void start_external_commit(game::Direction d, const game::Board& board) noexcept;

    // Set by step() when a commit animation finishes. Caller resets to false
    // after applying.
    bool commit_ready = false;

private:
    void start_anim(float dur, float from, float to) noexcept;
    static float signed_along(float dx, float dy, game::Direction d) noexcept;
};

}  // namespace triples::input
