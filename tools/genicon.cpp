// genicon.cpp — generates the Triples application icon, derived from the
// in-game artwork:
//
//   src/triples.ico       multi-resolution Windows icon, linked via .rc
//   src/triples.icns      multi-resolution macOS icon, bundled into .app
//   src/resources/icon.hpp 64×64 RGBA window icon, set via SDL_SetWindowIcon
//   shell/triples-favicon.png  256×256 PNG, base64'd into the web shell head
//
// The picture is a square tile in the game's white-with-yellow-edge palette,
// with three diagonal red "slashes" (the highest-rank-tile red) standing in
// for the digit. Re-rasterised at every size at 2× supersampling so the
// strokes stay crisp from 16 px (smallest .ico entry) up to 1024 px (largest
// .icns entry).
//
// Build & run from the repo root:
//   c++ -O2 -std=c++17 -Ithird_party -o /tmp/genicon tools/genicon.cpp
//   /tmp/genicon

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <vector>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb/stb_image_write.h"

namespace {

struct RGBA { uint8_t r, g, b, a; };

// --- signed-distance helpers ------------------------------------------------

// SDF for an axis-aligned rounded rect centered at (cx, cy) with half-extents
// (hw, hh) and corner radius r.
float sdRoundedRect(float px, float py, float cx, float cy,
                    float hw, float hh, float r) {
    float qx = std::fabs(px - cx) - (hw - r);
    float qy = std::fabs(py - cy) - (hh - r);
    float ax = std::max(qx, 0.0f);
    float ay = std::max(qy, 0.0f);
    return std::min(std::max(qx, qy), 0.0f) + std::sqrt(ax * ax + ay * ay) - r;
}

// SDF for a capsule (line segment with circular caps).
float sdSegment(float px, float py, float ax, float ay, float bx, float by) {
    float vx = bx - ax, vy = by - ay;
    float wx = px - ax, wy = py - ay;
    float c = vx * vx + vy * vy;
    float t = c > 0.0f ? std::clamp((wx * vx + wy * vy) / c, 0.0f, 1.0f) : 0.0f;
    float dx = px - (ax + t * vx);
    float dy = py - (ay + t * vy);
    return std::sqrt(dx * dx + dy * dy);
}

// Straight-alpha "src over dst" composite.
RGBA over(RGBA dst, RGBA src) {
    float sa = src.a / 255.0f;
    float da = dst.a / 255.0f;
    float oa = sa + da * (1.0f - sa);
    if (oa <= 0.0f) return RGBA{0, 0, 0, 0};
    auto ch = [&](float s, float d) {
        return uint8_t(std::clamp((s * sa + d * da * (1.0f - sa)) / oa,
                                  0.0f, 255.0f) + 0.5f);
    };
    return RGBA{
        ch(float(src.r), float(dst.r)),
        ch(float(src.g), float(dst.g)),
        ch(float(src.b), float(dst.b)),
        uint8_t(oa * 255.0f + 0.5f),
    };
}

// --- the icon itself --------------------------------------------------------

// Render the Triples tile icon at size SxS, straight-alpha RGBA, transparent
// outside the rounded square. Re-rasterised at the target size; no resampling
// from a fixed-size source.
std::vector<RGBA> renderTile(int S) {
    std::vector<RGBA> img(size_t(S) * S, RGBA{0, 0, 0, 0});

    // Geometry. Margin keeps the tile a hair off the icon edge so the
    // rounded corners aren't clipped on round-cornered launchers.
    const float cx     = S * 0.5f;
    const float cy     = S * 0.5f;
    const float margin = S * 0.04f;
    const float hw     = S * 0.5f - margin;
    const float hh     = S * 0.5f - margin;
    const float radius = std::min(hw, hh) * 0.22f;

    // Edge band — same idea as the in-game tile's `edge_band_frac`. The band
    // is the bottom slice of the tile; the body is the upper rounded-rect
    // mask that sits above it.
    const float edge_frac     = 0.18f;
    const float body_hh       = hh * (1.0f - edge_frac);
    const float body_cy       = cy - hh * edge_frac;

    // Colors. White face + yellow edge match the in-game ranks-3+ tiles;
    // red slashes match the "current max rank" text color, the most
    // distinctive accent in the game's palette.
    const RGBA face_color  {0xFC, 0xFC, 0xFC, 0xFF};
    const RGBA edge_color  {0xFF, 0xCC, 0x66, 0xFF};
    const RGBA slash_color {0xD9, 0x2E, 0x2E, 0xFF};

    // Anti-aliasing falloff in pixels — wide enough to give a soft edge on
    // small bakes, narrow enough that 1024 px stays crisp.
    const float aa = std::max(0.9f, S * 0.012f);

    // Three diagonal slashes (lower-left to upper-right), evenly spaced,
    // centered on the tile body. Each is a rounded-cap capsule.
    struct Slash { float ax, ay, bx, by; };
    Slash slashes[3];
    {
        const float angle    = -62.0f * 3.14159265f / 180.0f;  // steep, ~italic-III feel
        const float cosA     = std::cos(angle);
        const float sinA     = std::sin(angle);
        const float length   = S * 0.50f;
        const float spacing  = S * 0.18f;
        // A small downward bias parks the slashes visually centered on the
        // BODY of the tile (which extends from disc_top to band_top_y),
        // rather than on the full tile (which includes the band).
        const float slash_cy = body_cy + body_hh * 0.05f;
        const float halfLen  = length * 0.5f;
        for (int i = 0; i < 3; ++i) {
            float off = (i - 1) * spacing;
            float scx = cx + off;
            float scy = slash_cy;
            slashes[i].ax = scx - halfLen * cosA;
            slashes[i].ay = scy - halfLen * sinA;
            slashes[i].bx = scx + halfLen * cosA;
            slashes[i].by = scy + halfLen * sinA;
        }
    }
    const float slash_half_thick = S * 0.05f;  // → 10% total stroke width

    for (int y = 0; y < S; ++y) {
        for (int x = 0; x < S; ++x) {
            const float px = x + 0.5f;
            const float py = y + 0.5f;

            // Outer rounded-rect mask.
            float d_outer = sdRoundedRect(px, py, cx, cy, hw, hh, radius);
            if (d_outer > aa * 0.5f) continue;
            float outer_cov = std::clamp(0.5f - d_outer / aa, 0.0f, 1.0f);

            // Body region — same rect but inset from the bottom by edge_frac.
            float d_body = sdRoundedRect(px, py, cx, body_cy, hw, body_hh, radius);
            float body_cov = std::clamp(0.5f - d_body / aa, 0.0f, 1.0f);

            // Base color: body (white) or edge (yellow), per the AA blend
            // along the band's top.
            RGBA pixel;
            pixel.r = uint8_t(face_color.r * body_cov + edge_color.r * (1.0f - body_cov));
            pixel.g = uint8_t(face_color.g * body_cov + edge_color.g * (1.0f - body_cov));
            pixel.b = uint8_t(face_color.b * body_cov + edge_color.b * (1.0f - body_cov));
            pixel.a = uint8_t(outer_cov * 255.0f + 0.5f);

            // Slashes drawn on top, clipped to the outer rounded-rect.
            for (const auto& s : slashes) {
                float d = sdSegment(px, py, s.ax, s.ay, s.bx, s.by)
                        - slash_half_thick;
                if (d > aa * 0.5f) continue;
                float slash_cov = std::clamp(0.5f - d / aa, 0.0f, 1.0f);
                if (slash_cov <= 0.0f) continue;
                RGBA s_pixel = slash_color;
                s_pixel.a = uint8_t(slash_cov * outer_cov * 255.0f + 0.5f);
                pixel = over(pixel, s_pixel);
            }

            img[size_t(y) * S + x] = pixel;
        }
    }
    return img;
}

// Box-average downscale in premultiplied alpha. S must be a multiple of N.
std::vector<RGBA> downscale(const std::vector<RGBA>& src, int S, int N) {
    std::vector<RGBA> dst(size_t(N) * N);
    int b = S / N;
    for (int y = 0; y < N; ++y) {
        for (int x = 0; x < N; ++x) {
            float r = 0, g = 0, bl = 0, a = 0;
            for (int sy = 0; sy < b; ++sy)
                for (int sx = 0; sx < b; ++sx) {
                    RGBA p = src[size_t(y * b + sy) * S + (x * b + sx)];
                    float pa = p.a / 255.0f;
                    r += p.r * pa; g += p.g * pa; bl += p.b * pa; a += pa;
                }
            RGBA o{0, 0, 0, 0};
            if (a > 0.0f) {
                o.r = uint8_t(std::clamp(r / a, 0.0f, 255.0f) + 0.5f);
                o.g = uint8_t(std::clamp(g / a, 0.0f, 255.0f) + 0.5f);
                o.b = uint8_t(std::clamp(bl / a, 0.0f, 255.0f) + 0.5f);
                o.a = uint8_t(std::clamp(a / (b * b) * 255.0f, 0.0f, 255.0f) + 0.5f);
            }
            dst[size_t(y) * N + x] = o;
        }
    }
    return dst;
}

std::vector<RGBA> rasterizeAt(int n) {
    // 2× supersample then box-downscale.
    return downscale(renderTile(2 * n), 2 * n, n);
}

// --- little-endian byte writers --------------------------------------------
void put16(std::vector<uint8_t>& v, uint16_t x) {
    v.push_back(uint8_t(x));
    v.push_back(uint8_t(x >> 8));
}
void put32(std::vector<uint8_t>& v, uint32_t x) {
    for (int i = 0; i < 4; ++i) v.push_back(uint8_t(x >> (8 * i)));
}

// One icon image as a 32-bpp BMP (DIB): BITMAPINFOHEADER, a bottom-up BGRA
// XOR bitmap, and a 1-bpp AND mask (so the shape is still right on the rare
// path that ignores alpha).
std::vector<uint8_t> bmpImage(const std::vector<RGBA>& px, int N) {
    std::vector<uint8_t> v;
    put32(v, 40);                          // biSize
    put32(v, uint32_t(N));                 // biWidth
    put32(v, uint32_t(2 * N));             // biHeight = 2*N (XOR + AND)
    put16(v, 1);                           // biPlanes
    put16(v, 32);                          // biBitCount
    put32(v, 0);                           // biCompression = BI_RGB
    put32(v, 0);                           // biSizeImage
    put32(v, 0); put32(v, 0);              // X/Y pixels-per-metre
    put32(v, 0); put32(v, 0);              // biClrUsed / biClrImportant
    for (int y = N - 1; y >= 0; --y) {     // XOR bitmap, bottom-up, BGRA
        for (int x = 0; x < N; ++x) {
            RGBA p = px[size_t(y) * N + x];
            v.push_back(p.b); v.push_back(p.g); v.push_back(p.r); v.push_back(p.a);
        }
    }
    int stride = ((N + 31) / 32) * 4;      // AND mask, 1 bpp, 4-byte rows
    for (int y = N - 1; y >= 0; --y) {
        std::vector<uint8_t> row(size_t(stride), 0);
        for (int x = 0; x < N; ++x) {
            if (px[size_t(y) * N + x].a < 128) {
                row[x / 8] |= uint8_t(0x80 >> (x % 8));
            }
        }
        v.insert(v.end(), row.begin(), row.end());
    }
    return v;
}

void writeIco(const char* path, const std::vector<int>& sizes,
              const std::vector<std::vector<RGBA>>& icons) {
    std::vector<std::vector<uint8_t>> imgs;
    for (size_t i = 0; i < sizes.size(); ++i)
        imgs.push_back(bmpImage(icons[i], sizes[i]));

    std::vector<uint8_t> f;
    put16(f, 0); put16(f, 1);                          // reserved, type = icon
    put16(f, uint16_t(sizes.size()));                  // image count
    uint32_t offset = 6 + 16 * uint32_t(sizes.size());
    for (size_t i = 0; i < sizes.size(); ++i) {
        int n = sizes[i];
        f.push_back(n >= 256 ? 0 : uint8_t(n));        // bWidth (0 == 256)
        f.push_back(n >= 256 ? 0 : uint8_t(n));        // bHeight
        f.push_back(0);                                // bColorCount
        f.push_back(0);                                // bReserved
        put16(f, 1);                                   // wPlanes
        put16(f, 32);                                  // wBitCount
        put32(f, uint32_t(imgs[i].size()));            // dwBytesInRes
        put32(f, offset);                              // dwImageOffset
        offset += uint32_t(imgs[i].size());
    }
    for (const auto& im : imgs) f.insert(f.end(), im.begin(), im.end());

    FILE* fp = std::fopen(path, "wb");
    if (!fp) { std::fprintf(stderr, "cannot write %s\n", path); std::exit(1); }
    std::fwrite(f.data(), 1, f.size(), fp);
    std::fclose(fp);
    std::printf("%s  (%zu bytes, sizes", path, f.size());
    for (int n : sizes) std::printf(" %d", n);
    std::printf(")\n");
}

// --- macOS .icns -----------------------------------------------------------
static void pngAppend(void* user, void* data, int size) {
    auto* v = static_cast<std::vector<uint8_t>*>(user);
    uint8_t* p = static_cast<uint8_t*>(data);
    v->insert(v->end(), p, p + size);
}

std::vector<uint8_t> rgbaToPng(const std::vector<RGBA>& px, int N) {
    std::vector<uint8_t> out;
    stbi_write_png_to_func(&pngAppend, &out, N, N, 4,
                           reinterpret_cast<const void*>(px.data()), N * 4);
    return out;
}

void be32(std::vector<uint8_t>& v, uint32_t x) {
    v.push_back(uint8_t(x >> 24));
    v.push_back(uint8_t(x >> 16));
    v.push_back(uint8_t(x >> 8));
    v.push_back(uint8_t(x));
}

struct IcnsEntry { const char* type; int size; };

void writeIcns(const char* path, const std::vector<IcnsEntry>& entries) {
    std::vector<uint8_t> f;
    f.push_back('i'); f.push_back('c'); f.push_back('n'); f.push_back('s');
    be32(f, 0);                                        // total length (patched)

    for (const auto& e : entries) {
        auto rgba = rasterizeAt(e.size);
        auto png  = rgbaToPng(rgba, e.size);
        f.push_back(e.type[0]); f.push_back(e.type[1]);
        f.push_back(e.type[2]); f.push_back(e.type[3]);
        be32(f, uint32_t(png.size() + 8));
        f.insert(f.end(), png.begin(), png.end());
    }

    uint32_t total = uint32_t(f.size());
    f[4] = uint8_t(total >> 24);
    f[5] = uint8_t(total >> 16);
    f[6] = uint8_t(total >> 8);
    f[7] = uint8_t(total);

    FILE* fp = std::fopen(path, "wb");
    if (!fp) { std::fprintf(stderr, "cannot write %s\n", path); std::exit(1); }
    std::fwrite(f.data(), 1, f.size(), fp);
    std::fclose(fp);
    std::printf("%s  (%zu bytes, %zu entries)\n",
                path, f.size(), entries.size());
}

// --- icon.hpp (embedded RGBA for SDL_SetWindowIcon) -------------------------
void writeIconHeader(const char* path, const std::vector<RGBA>& px, int N) {
    FILE* fp = std::fopen(path, "w");
    if (!fp) { std::fprintf(stderr, "cannot write %s\n", path); std::exit(1); }
    std::fprintf(fp,
        "// icon.hpp - GENERATED by tools/genicon.cpp. Do not edit.\n"
        "// %dx%d RGBA window icon, uploaded with SDL_SetWindowIcon at\n"
        "// startup so the taskbar / task switcher show the right glyph.\n"
        "#pragma once\n#include <cstdint>\n\nnamespace triples::resources {\n\n"
        "inline constexpr int ICON_W = %d;\n"
        "inline constexpr int ICON_H = %d;\n\n"
        "inline constexpr std::uint8_t ICON_RGBA[%d * %d * 4] = {\n",
        N, N, N, N, N, N);
    size_t n = size_t(N) * N;
    for (size_t i = 0; i < n; ++i) {
        std::fprintf(fp, "%u,%u,%u,%u,",
                     px[i].r, px[i].g, px[i].b, px[i].a);
        if ((i & 7) == 7) std::fprintf(fp, "\n");
    }
    std::fprintf(fp, "\n};\n\n}  // namespace triples::resources\n");
    std::fclose(fp);
    std::printf("%s  (%dx%d RGBA)\n", path, N, N);
}

}  // namespace

int main() {
    // --- Windows .ico + embedded SDL_SetWindowIcon source ---
    const std::vector<int> winSizes = {16, 32, 48, 64, 128, 256};
    std::vector<std::vector<RGBA>> icons;
    for (int n : winSizes) icons.push_back(rasterizeAt(n));
    writeIco("src/triples.ico", winSizes, icons);
    // Use the 64 px entry for the in-window taskbar icon.
    writeIconHeader("src/resources/icon.hpp", icons[3], 64);

    // --- macOS .icns ---
    // ic07-ic10 are 1×; ic11-ic14 are the 2× retina variants of 16/32/128/256
    // respectively. icp4-icp6 cover older smaller 1× sizes as fallback.
    writeIcns("src/triples.icns", {
        {"icp4", 16}, {"icp5", 32}, {"icp6", 64},
        {"ic07", 128}, {"ic08", 256}, {"ic09", 512}, {"ic10", 1024},
        {"ic11",  32}, {"ic12",  64}, {"ic13", 256}, {"ic14", 512},
    });

    // --- Web favicon (PNG, base64'd into shell/index.html by hand) ---
    {
        auto fav = rasterizeAt(192);
        stbi_write_png("shell/triples-favicon.png", 192, 192, 4,
                       fav.data(), 192 * 4);
        std::printf("shell/triples-favicon.png  (192x192 RGBA)\n");
    }

    return 0;
}
