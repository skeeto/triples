#include "audio/mixer.hpp"

#include <SDL3/SDL.h>

#include <array>
#include <vector>

namespace triples::audio {

struct Mixer::Impl {
    SDL_AudioDeviceID                                                 device = 0;
    SDL_AudioSpec                                                     spec{};
    std::array<SfxBuffer, static_cast<std::size_t>(Sfx::Count_)>      buffers{};
    std::vector<SDL_AudioStream*>                                     active;

    bool open() {
        spec.format = SDL_AUDIO_F32;
        spec.channels = 1;
        spec.freq = kSampleRate;

        device = SDL_OpenAudioDevice(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec);
        if (device == 0) return false;
        SDL_ResumeAudioDevice(device);

        for (int i = 0; i < static_cast<int>(Sfx::Count_); ++i) {
            buffers[i] = synthesize(static_cast<Sfx>(i));
        }
        return true;
    }

    void close() {
        for (auto* s : active) {
            if (s) SDL_DestroyAudioStream(s);
        }
        active.clear();
        if (device) SDL_CloseAudioDevice(device);
        device = 0;
    }

    void play_sfx(Sfx s, float gain) {
        if (device == 0) return;
        const auto& b = buffers[static_cast<std::size_t>(s)];
        if (b.samples.empty()) return;

        SDL_AudioStream* stream =
            SDL_CreateAudioStream(&spec, &spec);
        if (!stream) return;

        // Apply gain by scaling — simple, transient-allocation tradeoff is fine
        // for our small buffer sizes (~9 KB each).
        std::vector<float> scaled;
        const float* src = b.samples.data();
        if (gain != 1.0f) {
            scaled.resize(b.samples.size());
            for (std::size_t i = 0; i < scaled.size(); ++i) scaled[i] = b.samples[i] * gain;
            src = scaled.data();
        }
        SDL_PutAudioStreamData(stream, src, static_cast<int>(b.samples.size() * sizeof(float)));
        SDL_FlushAudioStream(stream);

        if (!SDL_BindAudioStream(device, stream)) {
            SDL_DestroyAudioStream(stream);
            return;
        }
        active.push_back(stream);
    }

    void poll_drained() {
        for (auto it = active.begin(); it != active.end();) {
            SDL_AudioStream* s = *it;
            if (s && SDL_GetAudioStreamAvailable(s) == 0
                  && SDL_GetAudioStreamQueued(s) == 0) {
                SDL_DestroyAudioStream(s);
                it = active.erase(it);
            } else {
                ++it;
            }
        }
    }
};

Mixer::Mixer()  = default;
Mixer::~Mixer() {
    if (impl_) impl_->close();
}

bool Mixer::initialize() {
    impl_ = std::make_unique<Impl>();
    if (!impl_->open()) {
        impl_.reset();
        return false;
    }
    return true;
}

void Mixer::play(Sfx s, float gain) {
    if (impl_) impl_->play_sfx(s, gain);
}

void Mixer::poll() {
    if (impl_) impl_->poll_drained();
}

}  // namespace triples::audio
