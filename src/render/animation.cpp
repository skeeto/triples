#include "render/animation.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace triples::render {

namespace {

inline float clamp01(float v) noexcept {
    return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v);
}

template <typename T>
void erase_finished(std::vector<T>& v) {
    v.erase(std::remove_if(v.begin(), v.end(),
                           [](const T& e) { return e.t >= e.dur; }),
            v.end());
}

// Tiny LCG for jitter inside animation.cpp.
std::uint32_t rng_state = 0x12345678u;
inline float urand() {
    rng_state = rng_state * 1664525u + 1013904223u;
    return static_cast<float>((rng_state >> 8) & 0xFFFFFFu) / 16777216.0f;
}

}  // namespace

float ease(float t, Easing e) noexcept {
    t = clamp01(t);
    switch (e) {
        case Easing::Linear:  return t;
        case Easing::OutQuad: {
            float u = 1.0f - t;
            return 1.0f - u * u;
        }
        case Easing::OutBack: {
            const float c1 = 1.70158f;
            const float c3 = c1 + 1.0f;
            float u = t - 1.0f;
            return 1.0f + c3 * u * u * u + c1 * u * u;
        }
    }
    return t;
}

void Animations::step(float dt) {
    for (auto& p : score_popups) p.t += dt;
    for (auto& m : merge_bumps)  m.t += dt;
    for (auto& s : spawn_fades)  s.t += dt;
    for (auto& p : sparkles) {
        p.t += dt;
        p.x += p.vx * dt;
        p.y += p.vy * dt;
        p.vy += 80.0f * dt;  // gentle gravity for sparkles
    }
    for (auto& p : confetti) {
        p.t += dt;
        p.x += p.vx * dt;
        p.y += p.vy * dt;
        p.vy += 400.0f * dt;  // stronger gravity for confetti
    }
    if (shake_amp > 0.0f) {
        shake_t += dt;
        if (shake_t >= shake_dur) {
            shake_amp = 0.0f;
            shake_t = 0.0f;
            shake_dur = 0.0f;
        }
    }
    if (!tally_labels.empty()) {
        tally_t += dt;
    }
    erase_finished(score_popups);
    erase_finished(merge_bumps);
    erase_finished(spawn_fades);
    erase_finished(sparkles);
    erase_finished(confetti);
}

void Animations::add_score_popup(float x, float y, std::uint64_t value) {
    ScorePopup p;
    p.x = x;
    p.y = y;
    p.value = value;
    score_popups.push_back(p);
}

void Animations::add_merge_bump(int cell) {
    CellEffect e;
    e.cell = cell;
    e.dur = 0.18f;
    merge_bumps.push_back(e);
}

void Animations::add_spawn_fade(int cell) {
    CellEffect e;
    e.cell = cell;
    e.dur = 0.14f;
    spawn_fades.push_back(e);
}

void Animations::add_screen_shake(float amplitude, float duration) {
    if (amplitude > shake_amp) {
        shake_amp = amplitude;
        shake_dur = duration;
        shake_t = 0.0f;
    }
}

void Animations::emit_sparkles(float x, float y, std::uint8_t r, std::uint8_t g, std::uint8_t b) {
    for (int i = 0; i < 14; ++i) {
        Particle p;
        p.x = x;
        p.y = y;
        float angle = urand() * 6.2831853f;
        float speed = 60.0f + urand() * 80.0f;
        p.vx = std::cos(angle) * speed;
        p.vy = std::sin(angle) * speed - 40.0f;  // mild upward bias
        p.dur = 0.45f + urand() * 0.20f;
        p.r = r; p.g = g; p.b = b;
        p.size = 4.0f + urand() * 3.0f;
        sparkles.push_back(p);
    }
}

void Animations::emit_confetti(float x_min, float x_max) {
    const std::uint8_t palette[5][3] = {
        {0x00, 0x99, 0xCC},
        {0xE6, 0x45, 0x45},
        {0xFF, 0xC8, 0x4D},
        {0x52, 0xC4, 0x6B},
        {0x9B, 0x6B, 0xD9},
    };
    for (int i = 0; i < 180; ++i) {
        Particle p;
        p.x = x_min + urand() * (x_max - x_min);
        p.y = -20.0f - urand() * 30.0f;
        p.vx = (urand() - 0.5f) * 120.0f;
        p.vy = 50.0f + urand() * 80.0f;
        p.dur = 1.8f + urand() * 0.6f;
        const auto& c = palette[static_cast<int>(urand() * 5) % 5];
        p.r = c[0]; p.g = c[1]; p.b = c[2];
        p.size = 4.0f + urand() * 4.0f;
        confetti.push_back(p);
    }
}

void Animations::shake_offset(float& out_x, float& out_y) const {
    if (shake_amp <= 0.0f || shake_dur <= 0.0f) {
        out_x = 0.0f;
        out_y = 0.0f;
        return;
    }
    float u = clamp01(shake_t / shake_dur);
    float decay = 1.0f - u;
    // Pseudo-random sample based on phase, no per-frame RNG state.
    float phase = shake_t * 32.0f;
    out_x = std::sin(phase * 1.7f) * shake_amp * decay;
    out_y = std::cos(phase * 2.3f) * shake_amp * decay;
}

void Animations::clear() {
    score_popups.clear();
    merge_bumps.clear();
    spawn_fades.clear();
    sparkles.clear();
    confetti.clear();
    shake_amp = 0.0f;
    shake_t = 0.0f;
    shake_dur = 0.0f;
    tally_labels.clear();
    tally_t = 0.0f;
}

void Animations::start_tally(std::vector<ScoreTallyLabel> labels) {
    tally_labels = std::move(labels);
    tally_t = 0.0f;
}

}  // namespace triples::render
