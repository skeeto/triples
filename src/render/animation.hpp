#pragma once
#include <array>
#include <cstdint>
#include <vector>

namespace triples::render {

// Generic tween system for post-commit animations.
//
// Effects we drive through this:
//   - Spawn fade-in for the new tile (alpha + scale).
//   - Merge bump on the resulting tile (scale pulse).
//   - Score popup ("+N" floating up).
//   - Screen shake (a 2D offset that decays over time).
//   - Sparkle particles when a new max appears.
//   - Confetti when hitting 12,288.

enum class Easing : std::uint8_t {
    Linear,
    OutQuad,
    OutBack,   // for the merge bump
};

struct ScorePopup {
    float x, y;          // anchor position (top of the floating text)
    float t   = 0.0f;
    float dur = 0.6f;
    std::uint64_t value = 0;
};

struct CellEffect {
    int   cell = 0;      // 0..15
    float t   = 0.0f;
    float dur = 0.0f;
};

struct Particle {
    float x, y;
    float vx, vy;
    float t = 0.0f;
    float dur = 0.5f;
    std::uint8_t r, g, b;
    float size = 6.0f;
};

// Game-over tally: one "+N" label per scoring tile, staggered in time.
struct ScoreTallyLabel {
    int           cell;       // 0..15
    std::uint64_t value;      // tile's score (kTileScores[rank])
    float         delay;      // seconds after tally start before this label appears
};

// Restart flip: each cell flips around its vertical axis in a top-left →
// bottom-right diagonal cascade, showing the OLD tile through the first half
// of its flip (scaling x → 0) and the NEW tile through the second half
// (x growing 0 → 1). Total ≈ 0.7 s.
struct RestartFlip {
    bool                         active = false;
    float                        t      = 0.0f;
    std::array<std::uint8_t, 16> old_cells{};

    static constexpr float kFlipDur = 0.32f;   // per-cell flip
    static constexpr float kStagger = 0.06f;   // delay between diagonals
    static constexpr int   kMaxDiagonal = 6;   // (3+3) on a 4x4 grid

    float total_dur() const noexcept {
        return kMaxDiagonal * kStagger + kFlipDur;
    }
    // Per-cell phase in [0, 1]: 0 = not started (or just started),
    // 0.5 = edge-on (the OLD/NEW swap), 1.0 = done.
    float cell_phase(int idx) const noexcept {
        int r = idx / 4, c = idx % 4;
        float delay = float(r + c) * kStagger;
        float u = t - delay;
        if (u <= 0.0f)        return 0.0f;
        if (u >= kFlipDur)    return 1.0f;
        return u / kFlipDur;
    }
};

class Animations {
public:
    Animations() {
        score_popups.reserve(16);
        merge_bumps.reserve(16);
        spawn_fades.reserve(4);
        sparkles.reserve(64);
        confetti.reserve(256);
    }

    // Advance every active timer by dt. Removes finished ones.
    void step(float dt);

    // Triggers.
    void add_score_popup(float x, float y, std::uint64_t value);
    void add_merge_bump(int cell);
    void add_spawn_fade(int cell);
    void add_screen_shake(float amplitude, float duration);
    void emit_sparkles(float x, float y, std::uint8_t r, std::uint8_t g, std::uint8_t b);
    void emit_confetti(float x_min, float x_max);
    void start_tally(std::vector<ScoreTallyLabel> labels);
    void start_restart_flip(const std::array<std::uint8_t, 16>& old_cells);

    // Current screen shake offset (px). Renderer applies before everything.
    void shake_offset(float& out_x, float& out_y) const;

    // Reset everything (e.g., on game restart).
    void clear();

    // Public state read by the renderer.
    std::vector<ScorePopup> score_popups;
    std::vector<CellEffect> merge_bumps;
    std::vector<CellEffect> spawn_fades;
    std::vector<Particle>   sparkles;
    std::vector<Particle>   confetti;

    float shake_amp = 0.0f;
    float shake_t   = 0.0f;
    float shake_dur = 0.0f;

    std::vector<ScoreTallyLabel> tally_labels;
    float                        tally_t = 0.0f;

    RestartFlip                  restart_flip;
};

// Eased value in [0, 1].
float ease(float t, Easing e) noexcept;

}  // namespace triples::render
