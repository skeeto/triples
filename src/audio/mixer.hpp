#pragma once
#include <array>
#include <memory>

#include "audio/synth.hpp"

struct SDL_AudioStream;

namespace triples::audio {

// Plays SFX through SDL3 audio streams bound to one output device. Each
// `play` call creates a transient stream (drained and freed after playback).
//
// The mixer is intentionally tiny: synth + SDL3 stream mixing on the device
// side handle the heavy lifting.
class Mixer {
public:
    Mixer();
    ~Mixer();
    Mixer(const Mixer&)            = delete;
    Mixer& operator=(const Mixer&) = delete;

    // Returns true if audio was successfully opened. If false, play() is a no-op.
    bool initialize();

    // Trigger an SFX. Cheap to call (one stream allocation, one fill).
    void play(Sfx s, float gain = 1.0f);

    // Frees any drained streams. Call once per frame from the main loop.
    void poll();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace triples::audio
