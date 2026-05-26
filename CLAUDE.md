# Triples — notes for future hacking

A working journal of design choices that aren't obvious from the code alone.

## Audio: how the SFX are made

All sound effects are **synthesized at runtime** — no WAV files, no
embedded PCM blobs. The mixer (`src/audio/mixer.cpp`) opens one SDL3
output device at startup, then calls `synthesize(Sfx)` once per enum
value and stashes the resulting float32 PCM in a buffer. Every `play()`
allocates a transient `SDL_AudioStream`, dumps the pre-computed
buffer at the mixer's gain, and lets SDL3 handle device-side mixing.

### Where things live

| File | What it does |
|---|---|
| `src/audio/synth.hpp` | `enum class Sfx`, `SfxBuffer`, `synthesize(Sfx)` |
| `src/audio/synth.cpp` | The renderer functions (per-Sfx) + shared primitives |
| `src/audio/mixer.{hpp,cpp}` | Open audio device, cache buffers, play streams |
| `src/audio/sfx.hpp` | `merge_sfx_for_rank()` helper |

Constants worth knowing:

- `kSampleRate = 22050` Hz — mono, F32 — set in `synth.hpp`.
- Buffers live as `std::vector<float>` normalized to `[-1, 1]`.
- Mixer gain at `play()` is multiplied onto the buffer before pushing to the stream, so design each Sfx near the loud end and turn it down at the call site if needed.

### Shared primitives in `synth.cpp`

Three building blocks the renderers compose:

```cpp
// Linear ADSR. All three args are in seconds; sum should be <= duration.
float envelope(float t, float attack, float hold, float release);

// Tiny LCG. Audio-rate quality is good enough for whoosh / brushed-noise.
float white_noise(std::uint32_t& seed);

// Short tonal blip = sine at `freq` + 18% 3rd-harmonic for warmth.
// Resizes `out` if needed; ADDITIVE — call repeatedly with different
// start_t / freq to chord or sequence.
void add_blip(std::vector<float>& out,
              float freq, float gain, float dur, float start_t);
```

### Per-Sfx design notes

Each Sfx has a `render_*(out)` function called from the `synthesize(Sfx)`
switch. The current zoo:

| Sfx | Design | Why |
|---|---|---|
| `Whoosh` | white noise → one-pole LPF (`a=0.12`, ~600 Hz cutoff) → ADSR shaping over 180 ms | Soft "brushed" sound for swipe commits. Lowpass is what turns hiss into breath. |
| `MergeLow` (ranks 3–6) | `add_blip` at 392 Hz (G4) + 1.5×freq partial, 180 ms | Two-blip chord — the 1.5× partial sounds quasi-triangle and gives the tone body without going full square. |
| `MergeMid` (7–8) | Same shape, 523 Hz (C5), 220 ms | Up a fourth from MergeLow. |
| `MergeHigh` (9+) | Same shape, 659 Hz (E5), 260 ms | Up a major third again. Together the three merges form a triadic "scale up by rank" sequence. |
| `NewMax` | 3 blips: 880 / 1320 / 1760 Hz at staggered starts (0 / 10 / 20 ms), decreasing gain | Bell-like — fundamental + stretched partials at 1.5× and 2×. Slight stagger gives the strike a little attack texture. |
| `GameOver` | A4 → F4 → D4 (440 / 349 / 294 Hz) at 0 / 150 / 300 ms, last one longer & louder | Descending minor third. The longer-held final note sells the "end" — the fade-out is the saddest part. |
| `RestartFlip` | 7 blips ascending C major pentatonic (C5 → C6), spaced 60 ms apart | One blip per diagonal of the flip cascade — the 60 ms spacing matches `render::RestartFlip::kStagger` exactly so audio and visual rhythm align. Pentatonic for a "fresh start" feel. |

### Adding a new SFX

1. Add the enum value to `Sfx` in `synth.hpp`. **Insert before `Count_`** — the mixer indexes `Sfx::Count_` to size its buffer array.
2. Write a `render_<name>(std::vector<float>& out)` in `synth.cpp`, composing `envelope` / `white_noise` / `add_blip` (or writing raw samples). Don't forget `out.clear()` if you start with `add_blip` (which resizes additively).
3. Add a `case Sfx::<Name>:` to the `synthesize()` switch.
4. Call `mixer_.play(audio::Sfx::<Name>, gain)` from wherever in `app.cpp` triggers it.

That's it — no resource files, no asset pipeline, no rebake step. Tweak a constant, rebuild, rerun.

### Picking pitches

The merges + RestartFlip use standard equal-temperament frequencies. Quick reference (A4 = 440):

| Note | Hz |
|---|---|
| C5 | 523.25 |
| D5 | 587.33 |
| E5 | 659.25 |
| G5 | 783.99 |
| A5 | 880.00 |
| B5 | 987.77 |
| C6 | 1046.50 |

For a "happier" feel, stay in major / pentatonic. For tension / sadness, descend or use minor intervals (the GameOver chord is a stack of minor thirds for that reason).
