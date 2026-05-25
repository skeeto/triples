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
    bool initialize(SDL_Renderer* r,
                    const unsigned char* ttf_data, std::size_t ttf_size,
                    float small_px, float body_px, float large_px);

    // Measure pixel width of `text` at the given size. Height is the line
    // height at that size (set by initialize()).
    float measure_width(std::string_view text, Size sz) const;
    float line_height(Size sz) const;
    float baseline(Size sz) const;

    // Draw `text` at (x, y) — y is the BASELINE. Color is RGBA.
    void draw(SDL_Renderer* r, std::string_view text, Size sz,
              float x, float y, std::uint8_t cr, std::uint8_t cg, std::uint8_t cb,
              std::uint8_t ca = 255) const;

    // Convenience: draw centered at (cx, baseline_y).
    void draw_centered(SDL_Renderer* r, std::string_view text, Size sz,
                       float cx, float baseline_y, std::uint8_t cr, std::uint8_t cg,
                       std::uint8_t cb, std::uint8_t ca = 255) const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace triples::render
