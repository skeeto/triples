#include "render/tile_draw.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>
#include <vector>

#include "game/board.hpp"

namespace triples::render {

namespace {

// Tile colors keyed by rank. Returns {r, g, b} for the background and the
// text color is determined separately in the renderer.
struct TileBG {
    std::uint8_t r, g, b;
};

TileBG bg_for_rank(std::uint8_t rank) {
    if (rank == 1) return {0x00, 0x99, 0xCC};   // blue
    if (rank == 2) return {0xE6, 0x45, 0x45};   // red
    if (rank == 15) return {0x2A, 0x2A, 0x2A};  // hidden 13th character — dark
    return {0xFC, 0xFC, 0xFC};                  // white
}

// Anti-aliased rounded-rect coverage at (px, py) within a w×h rect with corner
// radius r, supersampled 2× internally. Returns coverage in [0, 1].
float rounded_rect_coverage(float px, float py, float w, float h, float r) {
    // Distance from point to rect's inner-rounded boundary (signed; <0 inside).
    auto sd = [&](float x, float y) {
        float qx = std::fabs(x - w * 0.5f) - (w * 0.5f - r);
        float qy = std::fabs(y - h * 0.5f) - (h * 0.5f - r);
        float ax = std::max(qx, 0.0f);
        float ay = std::max(qy, 0.0f);
        return std::min(std::max(qx, qy), 0.0f) + std::sqrt(ax * ax + ay * ay) - r;
    };
    // 4-tap supersample (2×2 grid).
    float c = 0.0f;
    for (int sy = 0; sy < 2; ++sy) {
        for (int sx = 0; sx < 2; ++sx) {
            float ssx = px + (sx + 0.5f) * 0.5f - 0.5f;
            float ssy = py + (sy + 0.5f) * 0.5f - 0.5f;
            float d = sd(ssx, ssy);
            // Edge at 0, half-pixel falloff for AA.
            c += std::clamp(0.5f - d, 0.0f, 1.0f);
        }
    }
    return c * 0.25f;
}

void bake_tile_pixels(std::vector<unsigned char>& rgba, int w, int h, std::uint8_t rank) {
    TileBG bg = bg_for_rank(rank);
    const float radius = std::min(w, h) * 0.10f;
    const float shade_band_start = h * (1.0f - 0.22f);
    rgba.assign(static_cast<std::size_t>(w) * h * 4, 0u);
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            float cov = rounded_rect_coverage(
                static_cast<float>(x) + 0.5f,
                static_cast<float>(y) + 0.5f,
                static_cast<float>(w), static_cast<float>(h), radius);
            if (cov <= 0.0f) continue;

            float br = bg.r / 255.0f;
            float bgc = bg.g / 255.0f;
            float bbc = bg.b / 255.0f;

            // Bottom edge: gradient over the bottom 22% band, multiply down to 0.78
            // (more pronounced shading so the "edge" reads clearly).
            if (y > shade_band_start) {
                float band_t = (y - shade_band_start) / (h - shade_band_start);
                band_t = std::clamp(band_t, 0.0f, 1.0f);
                float k = 1.0f - 0.22f * band_t;
                br *= k; bgc *= k; bbc *= k;
            }

            int idx = (y * w + x) * 4;
            rgba[idx + 0] = static_cast<unsigned char>(br * 255.0f);
            rgba[idx + 1] = static_cast<unsigned char>(bgc * 255.0f);
            rgba[idx + 2] = static_cast<unsigned char>(bbc * 255.0f);
            rgba[idx + 3] = static_cast<unsigned char>(cov * 255.0f);
        }
    }
}

}  // namespace

TileTextureCache::TileTextureCache()  = default;
TileTextureCache::~TileTextureCache() { destroy_all_(); }

void TileTextureCache::destroy_all_() {
    for (auto& t : textures_) {
        if (t) SDL_DestroyTexture(t);
        t = nullptr;
    }
}

bool TileTextureCache::bake(SDL_Renderer* r, int tex_w) {
    destroy_all_();
    renderer_ = r;
    w_ = tex_w;
    h_ = static_cast<int>(tex_w * 1.5f);

    std::vector<unsigned char> rgba;
    for (std::uint8_t rank = 1; rank <= 15; ++rank) {
        bake_tile_pixels(rgba, w_, h_, rank);
        SDL_Texture* t = SDL_CreateTexture(r, SDL_PIXELFORMAT_RGBA32,
                                           SDL_TEXTUREACCESS_STATIC, w_, h_);
        if (!t) {
            destroy_all_();
            return false;
        }
        SDL_SetTextureBlendMode(t, SDL_BLENDMODE_BLEND);
        SDL_SetTextureScaleMode(t, SDL_SCALEMODE_LINEAR);
        SDL_UpdateTexture(t, nullptr, rgba.data(), w_ * 4);
        textures_[rank] = t;
    }
    return true;
}

SDL_Texture* TileTextureCache::texture_for(std::uint8_t rank) const noexcept {
    if (rank == 0 || rank > 15) return nullptr;
    return textures_[rank];
}

SDL_Texture* bake_empty_slot_texture(SDL_Renderer* r, int w, int h) {
    std::vector<unsigned char> rgba(static_cast<std::size_t>(w) * h * 4, 0u);
    const float radius = std::min(w, h) * 0.10f;
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            float cov = rounded_rect_coverage(
                static_cast<float>(x) + 0.5f,
                static_cast<float>(y) + 0.5f,
                static_cast<float>(w), static_cast<float>(h), radius);
            if (cov <= 0.0f) continue;
            int idx = (y * w + x) * 4;
            rgba[idx + 0] = 0xEC;
            rgba[idx + 1] = 0xE6;
            rgba[idx + 2] = 0xD8;
            rgba[idx + 3] = static_cast<unsigned char>(cov * 255.0f);
        }
    }
    SDL_Texture* t = SDL_CreateTexture(r, SDL_PIXELFORMAT_RGBA32,
                                       SDL_TEXTUREACCESS_STATIC, w, h);
    if (!t) return nullptr;
    SDL_SetTextureBlendMode(t, SDL_BLENDMODE_BLEND);
    SDL_SetTextureScaleMode(t, SDL_SCALEMODE_LINEAR);
    SDL_UpdateTexture(t, nullptr, rgba.data(), w * 4);
    return t;
}

}  // namespace triples::render
