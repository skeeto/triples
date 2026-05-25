#pragma once
#include <array>
#include <cstdint>

struct SDL_Renderer;
struct SDL_Texture;

namespace triples::render {

// One pre-baked rounded-rect texture per tile rank. Bottom 22% is shaded
// darker as the "edge". Generated once, reused per frame. Re-bake on
// dpi/scale change (which is rare).
class TileTextureCache {
public:
    TileTextureCache();
    ~TileTextureCache();
    TileTextureCache(const TileTextureCache&)            = delete;
    TileTextureCache& operator=(const TileTextureCache&) = delete;

    // Bake all 15 tile textures (rank 1..15). `tex_w` is the desired tile
    // width in pixels; height = 1.5 * tex_w (the 2:3 aspect ratio). The
    // textures are stored at this resolution and stretched/squished by the
    // renderer as needed.
    bool bake(SDL_Renderer* r, int tex_w);

    // Returns the texture for the given rank (1..15), or nullptr.
    SDL_Texture* texture_for(std::uint8_t rank) const noexcept;

    int width()  const noexcept { return w_; }
    int height() const noexcept { return h_; }

private:
    std::array<SDL_Texture*, 16> textures_{};   // indexed by rank; [0] unused
    int                          w_ = 0;
    int                          h_ = 0;
    SDL_Renderer*                renderer_ = nullptr;

    void destroy_all_();
};

// "Empty cell" slot texture — subtle inset rounded rect drawn at the same
// dimensions as a tile. Drawn once into a single texture, reused.
SDL_Texture* bake_empty_slot_texture(SDL_Renderer* r, int w, int h);

}  // namespace triples::render
