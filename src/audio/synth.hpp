#pragma once
#include <cstddef>
#include <cstdint>
#include <vector>

namespace triples::audio {

// All synthesized SFX share this internal format.
inline constexpr int kSampleRate = 22050;

enum class Sfx {
    Whoosh,      // soft brushed noise during commit
    MergeLow,    // ranks 3..6 (values 3, 6, 12, 24)
    MergeMid,    // ranks 7..8 (values 48, 96)
    MergeHigh,   // ranks 9+   (values 192+)
    NewMax,      // bell-like chime when a new max appears
    GameOver,    // descending minor chord
    RestartFlip, // ascending arpeggio synced to the per-diagonal cascade
    Count_
};

// PCM samples for an SFX, mono, float32 normalized to [-1, 1].
struct SfxBuffer {
    std::vector<float> samples;
};

// Generate all sfx buffers. Called once at startup.
SfxBuffer synthesize(Sfx s);

}  // namespace triples::audio
