#pragma once
#include <cstdint>
#include <memory>
#include <string_view>

struct SDL_Renderer;
struct SDL_Texture;

namespace triples::render {

// stb_truetype-based bitmap atlas. Three pre-baked sizes covering ASCII
// printable characters (0x20-0x7E). At-runtime tinting via SDL_SetTextureColorMod.
class TextAtlas {
public:
    enum class Size { Small, Body, Large };

    TextAtlas();
    ~TextAtlas();
    TextAtlas(const TextAtlas&)            = delete;
    TextAtlas& operator=(const TextAtlas&) = delete;

    // Build atlas textures. Returns false on failure (e.g., bad TTF data,
    // texture creation failed). The atlas owns nothing if init failed.
    //
    // `first_char` / `char_count` select a contiguous run of ASCII codepoints
    // to bake. Default is the full printable range (0x20-0x7E). For atlases
    // used only for digits (e.g., the tile-number font), passing a smaller
    // range lets you bake a much larger font into the same atlas.
    bool initialize(SDL_Renderer* r,
                    const unsigned char* ttf_data, std::size_t ttf_size,
                    float small_px, float body_px, float large_px,
                    int first_char = 0x20, int char_count = 0x7F - 0x20);

    // Measure pixel width / line metrics of `text` at the given size. All
    // three accept an optional `scale` so callers can render at any size
    // without re-baking the atlas — scale > 1 upscales (slightly blurry past
    // ~2×), scale < 1 downscales smoothly.
    float measure_width(std::string_view text, Size sz, float scale = 1.0f) const;
    float line_height(Size sz, float scale = 1.0f) const;
    float baseline(Size sz, float scale = 1.0f) const;

    // Draw `text` at (x, y) — y is the BASELINE. Color is RGBA.
    // `scale_x_extra` / `scale_y_extra` apply anisotropic squish/stretch ON
    // TOP of `scale`; defaulting to 1.0 leaves the old isotropic behavior
    // unchanged. Used by the tile flip animation so the digit follows the
    // tile's horizontal squish through the flip.
    void draw(SDL_Renderer* r, std::string_view text, Size sz,
              float x, float y, std::uint8_t cr, std::uint8_t cg, std::uint8_t cb,
              std::uint8_t ca = 255, float scale = 1.0f,
              float scale_x_extra = 1.0f, float scale_y_extra = 1.0f) const;

    // Convenience: draw centered at (cx, baseline_y).
    void draw_centered(SDL_Renderer* r, std::string_view text, Size sz,
                       float cx, float baseline_y, std::uint8_t cr, std::uint8_t cg,
                       std::uint8_t cb, std::uint8_t ca = 255, float scale = 1.0f,
                       float scale_x_extra = 1.0f, float scale_y_extra = 1.0f) const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace triples::render
