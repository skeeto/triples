#include "render/text.hpp"

#define STB_TRUETYPE_IMPLEMENTATION
#include "stb/stb_truetype.h"

#include <SDL3/SDL.h>

#include <array>
#include <vector>

namespace triples::render {

namespace {

constexpr int kAtlasW = 1024;
constexpr int kAtlasH = 1024;
// Pixels of zero-alpha around each glyph in the atlas. Lets callers shift the
// rendered glyph by up to this many pixels (e.g. for a black-outline halo)
// without sampling neighbour glyphs.
constexpr int kGlyphPadding = 4;

struct BakedSize {
    std::vector<stbtt_packedchar> chars;  // sized by Impl::char_count
    SDL_Texture*                  tex = nullptr;
    float                         px = 0.0f;
    float                         line = 0.0f;
    float                         ascent = 0.0f;
};

}  // namespace

struct TextAtlas::Impl {
    int                      first_char = 0x20;
    int                      char_count = 0x7F - 0x20;
    std::array<BakedSize, 3> sizes;

    ~Impl() {
        for (auto& s : sizes) {
            if (s.tex) SDL_DestroyTexture(s.tex);
        }
    }

    bool build_size(SDL_Renderer* r, const unsigned char* data,
                    BakedSize& out, float px) {
        out.chars.assign(char_count, stbtt_packedchar{});
        std::vector<unsigned char> bitmap(static_cast<std::size_t>(kAtlasW) * kAtlasH, 0u);
        stbtt_pack_context spc;
        if (!stbtt_PackBegin(&spc, bitmap.data(), kAtlasW, kAtlasH, kAtlasW,
                             kGlyphPadding, nullptr)) {
            return false;
        }
        stbtt_pack_range range{};
        range.font_size = px;
        range.first_unicode_codepoint_in_range = first_char;
        range.array_of_unicode_codepoints = nullptr;
        range.num_chars = char_count;
        range.chardata_for_range = out.chars.data();
        range.h_oversample = 1;
        range.v_oversample = 1;
        int ok = stbtt_PackFontRanges(&spc, data, 0, &range, 1);
        stbtt_PackEnd(&spc);
        if (!ok) return false;

        // Convert alpha-only to RGBA32 with white RGB and alpha = sample,
        // so SDL_SetTextureColorMod gives us tinted glyphs.
        std::vector<unsigned char> rgba(static_cast<std::size_t>(kAtlasW) * kAtlasH * 4, 0u);
        for (int i = 0; i < kAtlasW * kAtlasH; ++i) {
            rgba[i * 4 + 0] = 255;
            rgba[i * 4 + 1] = 255;
            rgba[i * 4 + 2] = 255;
            rgba[i * 4 + 3] = bitmap[i];
        }
        out.tex = SDL_CreateTexture(r, SDL_PIXELFORMAT_RGBA32,
                                    SDL_TEXTUREACCESS_STATIC, kAtlasW, kAtlasH);
        if (!out.tex) return false;
        SDL_SetTextureBlendMode(out.tex, SDL_BLENDMODE_BLEND);
        SDL_SetTextureScaleMode(out.tex, SDL_SCALEMODE_LINEAR);
        SDL_UpdateTexture(out.tex, nullptr, rgba.data(), kAtlasW * 4);

        out.px = px;
        // Use a quick metric pass.
        stbtt_fontinfo fi;
        stbtt_InitFont(&fi, data, 0);
        int a, d, l;
        stbtt_GetFontVMetrics(&fi, &a, &d, &l);
        float scale = stbtt_ScaleForPixelHeight(&fi, px);
        out.ascent = a * scale;
        out.line = (a - d + l) * scale;
        return true;
    }

    const BakedSize* by_size(TextAtlas::Size sz) const {
        return &sizes[static_cast<int>(sz)];
    }
};

TextAtlas::TextAtlas()  = default;
TextAtlas::~TextAtlas() = default;

bool TextAtlas::initialize(SDL_Renderer* r,
                           const unsigned char* ttf_data, std::size_t ttf_size,
                           float small_px, float body_px, float large_px,
                           int first_char, int char_count) {
    (void)ttf_size;
    impl_ = std::make_unique<Impl>();
    impl_->first_char = first_char;
    impl_->char_count = char_count;
    if (!impl_->build_size(r, ttf_data, impl_->sizes[0], small_px)) { impl_.reset(); return false; }
    if (!impl_->build_size(r, ttf_data, impl_->sizes[1], body_px))  { impl_.reset(); return false; }
    if (!impl_->build_size(r, ttf_data, impl_->sizes[2], large_px)) { impl_.reset(); return false; }
    return true;
}

float TextAtlas::measure_width(std::string_view text, Size sz, float scale) const {
    if (!impl_) return 0.0f;
    const BakedSize* b = impl_->by_size(sz);
    float x = 0.0f, y = 0.0f;
    stbtt_aligned_quad q;
    const int first = impl_->first_char;
    const int count = impl_->char_count;
    for (char c : text) {
        unsigned ch = static_cast<unsigned char>(c);
        if (static_cast<int>(ch) < first || static_cast<int>(ch) >= first + count) continue;
        stbtt_GetPackedQuad(const_cast<stbtt_packedchar*>(b->chars.data()),
                            kAtlasW, kAtlasH,
                            static_cast<int>(ch) - first, &x, &y, &q, 1);
    }
    return x * scale;
}

float TextAtlas::line_height(Size sz, float scale) const {
    if (!impl_) return 0.0f;
    return impl_->by_size(sz)->line * scale;
}

float TextAtlas::baseline(Size sz, float scale) const {
    if (!impl_) return 0.0f;
    return impl_->by_size(sz)->ascent * scale;
}

void TextAtlas::draw(SDL_Renderer* r, std::string_view text, Size sz,
                     float x, float y, std::uint8_t cr, std::uint8_t cg, std::uint8_t cb,
                     std::uint8_t ca, float scale) const {
    if (!impl_) return;
    const BakedSize* b = impl_->by_size(sz);
    if (!b->tex) return;
    SDL_SetTextureColorMod(b->tex, cr, cg, cb);
    SDL_SetTextureAlphaMod(b->tex, ca);
    // Run the pen in baked space (scale=1) and scale quad output around (x, y).
    const int first = impl_->first_char;
    const int count = impl_->char_count;
    float pen_x = 0.0f, pen_y = 0.0f;
    for (char c : text) {
        unsigned ch = static_cast<unsigned char>(c);
        if (static_cast<int>(ch) < first || static_cast<int>(ch) >= first + count) continue;
        stbtt_aligned_quad q;
        stbtt_GetPackedQuad(const_cast<stbtt_packedchar*>(b->chars.data()),
                            kAtlasW, kAtlasH,
                            static_cast<int>(ch) - first, &pen_x, &pen_y, &q, 1);
        SDL_FRect src{q.s0 * kAtlasW, q.t0 * kAtlasH,
                      (q.s1 - q.s0) * kAtlasW, (q.t1 - q.t0) * kAtlasH};
        SDL_FRect dst{x + q.x0 * scale, y + q.y0 * scale,
                      (q.x1 - q.x0) * scale, (q.y1 - q.y0) * scale};
        SDL_RenderTexture(r, b->tex, &src, &dst);
    }
}

void TextAtlas::draw_centered(SDL_Renderer* r, std::string_view text, Size sz,
                              float cx, float baseline_y, std::uint8_t cr,
                              std::uint8_t cg, std::uint8_t cb, std::uint8_t ca,
                              float scale) const {
    float w = measure_width(text, sz, scale);
    draw(r, text, sz, cx - w * 0.5f, baseline_y, cr, cg, cb, ca, scale);
}

}  // namespace triples::render
