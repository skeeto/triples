#include "audio/synth.hpp"

#include <cmath>
#include <cstdlib>

namespace triples::audio {

namespace {

constexpr float kTwoPi = 6.2831853071795864769f;

// Linear envelope: attack to 1.0 over `attack`, hold at 1.0 for `hold`, linear
// decay to 0 over `release`. All in seconds. Sum should be <= duration.
float envelope(float t, float attack, float hold, float release) {
    if (t < attack) return t / attack;
    if (t < attack + hold) return 1.0f;
    float r = t - attack - hold;
    if (r < release) return 1.0f - r / release;
    return 0.0f;
}

// Tiny LCG noise. Audio-rate quality is fine for whoosh sounds.
float white_noise(std::uint32_t& s) {
    s = s * 1664525u + 1013904223u;
    return (static_cast<int>(s >> 8) & 0xFFFFFF) / 8388608.0f - 1.0f;  // [-1, 1)
}

void render_whoosh(std::vector<float>& out) {
    constexpr float dur = 0.18f;
    int n = static_cast<int>(dur * kSampleRate);
    out.resize(n);
    std::uint32_t s = 0xDEADBEEFu;
    // One-pole lowpass running at ~600 Hz so it sounds breathy, not hissy.
    float lp = 0.0f;
    constexpr float a = 0.12f;
    for (int i = 0; i < n; ++i) {
        float t = static_cast<float>(i) / kSampleRate;
        float env = envelope(t, 0.012f, 0.02f, dur - 0.032f);
        float src = white_noise(s);
        lp += a * (src - lp);
        out[i] = lp * env * 0.30f;
    }
}

// Short percussive tone at `freq` Hz. Combine multiple of these for chords.
void add_blip(std::vector<float>& out, float freq, float gain, float dur, float start_t) {
    int n = static_cast<int>(dur * kSampleRate);
    int start = static_cast<int>(start_t * kSampleRate);
    int total = start + n;
    if (out.size() < static_cast<std::size_t>(total)) out.resize(total, 0.0f);
    for (int i = 0; i < n; ++i) {
        float t = static_cast<float>(i) / kSampleRate;
        float env = envelope(t, 0.003f, 0.02f, dur - 0.023f);
        float sample = std::sin(kTwoPi * freq * t);
        // Slight triangle harmonic for warmth.
        sample += 0.18f * std::sin(kTwoPi * 3.0f * freq * t);
        out[start + i] += sample * env * gain;
    }
}

void render_merge(std::vector<float>& out, float base_freq, float dur) {
    out.clear();
    add_blip(out, base_freq,       0.45f, dur,       0.0f);
    add_blip(out, base_freq * 1.5f, 0.30f, dur * 0.85f, 0.005f);
}

void render_new_max(std::vector<float>& out) {
    out.clear();
    // Bell-like: fundamental + a couple of stretched partials.
    add_blip(out, 880.0f,  0.40f, 0.6f, 0.00f);
    add_blip(out, 1320.0f, 0.20f, 0.5f, 0.01f);
    add_blip(out, 1760.0f, 0.12f, 0.4f, 0.02f);
}

void render_game_over(std::vector<float>& out) {
    out.clear();
    // Minor third descending: A4 (440), F4 (~349), D4 (~293).
    add_blip(out, 440.0f, 0.45f, 0.40f, 0.00f);
    add_blip(out, 349.23f, 0.45f, 0.40f, 0.15f);
    add_blip(out, 293.66f, 0.50f, 0.70f, 0.30f);
}

}  // namespace

SfxBuffer synthesize(Sfx s) {
    SfxBuffer b;
    switch (s) {
        case Sfx::Whoosh:    render_whoosh(b.samples); break;
        case Sfx::MergeLow:  render_merge(b.samples, 392.0f, 0.18f); break;  // G4
        case Sfx::MergeMid:  render_merge(b.samples, 523.25f, 0.22f); break; // C5
        case Sfx::MergeHigh: render_merge(b.samples, 659.25f, 0.26f); break; // E5
        case Sfx::NewMax:    render_new_max(b.samples); break;
        case Sfx::GameOver:  render_game_over(b.samples); break;
        case Sfx::Count_: break;
    }
    return b;
}

}  // namespace triples::audio
