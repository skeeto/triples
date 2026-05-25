#pragma once
#include <cstdint>

#include "audio/mixer.hpp"

namespace triples::audio {

// Map a tile rank to the appropriate merge SFX tier.
inline Sfx merge_sfx_for_rank(std::uint8_t resulting_rank) noexcept {
    if (resulting_rank <= 6) return Sfx::MergeLow;
    if (resulting_rank <= 8) return Sfx::MergeMid;
    return Sfx::MergeHigh;
}

}  // namespace triples::audio
