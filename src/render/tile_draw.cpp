#include "render/tile_draw.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>
#include <vector>

#include "game/board.hpp"

namespace triples::render {

namespace {

// Tile colors keyed by rank. Returns {r, g, b} for the body and a separate
// color for the hard bottom edge.
struct TileBG {
    std::uint8_t r, g, b;
};

TileBG bg_for_rank(std::uint8_t rank) {
    if (rank == 1) return {0x66, 0xCC, 0xFF};   // blue
    if (rank == 2) return {0xFF, 0x66, 0x80};   // red
    if (rank == 15) return {0x2A, 0x2A, 0x2A};  // hidden 13th character — dark
    return {0xFC, 0xFC, 0xFC};                  // white
}

TileBG edge_for_rank(std::uint8_t rank) {
    if (rank == 1) return {0x5F, 0xA9, 0xF1};   // darker blue
    if (rank == 2) return {0xCC, 0x52, 0x7A};   // darker red
    if (rank == 15) return {0x16, 0x16, 0x16};  // darker grey for hidden tile
    return {0xFF, 0xCC, 0x66};                  // yellow band under whites
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
    TileBG body = bg_for_rank(rank);
    TileBG edge = edge_for_rank(rank);
    const float radius = std::min(w, h) * 0.10f;
    // The "edge" is the colored band peeking out below the front face. The
    // face itself is a rounded rectangle — its bottom corners are rounded too,
    // so a sliver of the edge color shows along the bottom of the tile (with
    // the corner curve matching the outer tile mask).
    const float edge_band_frac = 0.12f;
    const float body_h         = static_cast<float>(h) * (1.0f - edge_band_frac);
    rgba.assign(static_cast<std::size_t>(w) * h * 4, 0u);
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            float px = static_cast<float>(x) + 0.5f;
            float py = static_cast<float>(y) + 0.5f;
            float outer_cov = rounded_rect_coverage(
                px, py, static_cast<float>(w), static_cast<float>(h), radius);
            if (outer_cov <= 0.0f) continue;

            float body_cov = rounded_rect_coverage(
                px, py, static_cast<float>(w), body_h, radius);

            float r, g, b;
            if (body_cov >= 1.0f) {
                r = body.r; g = body.g; b = body.b;
            } else if (body_cov <= 0.0f) {
                r = edge.r; g = edge.g; b = edge.b;
            } else {
                // Anti-aliased blend along the body's bottom curve.
                float t = body_cov;
                r = body.r * t + edge.r * (1.0f - t);
                g = body.g * t + edge.g * (1.0f - t);
                b = body.b * t + edge.b * (1.0f - t);
            }

            int idx = (y * w + x) * 4;
            rgba[idx + 0] = static_cast<unsigned char>(r);
            rgba[idx + 1] = static_cast<unsigned char>(g);
            rgba[idx + 2] = static_cast<unsigned char>(b);
            rgba[idx + 3] = static_cast<unsigned char>(outer_cov * 255.0f);
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

SDL_Texture* bake_restart_button_texture(SDL_Renderer* r, int diameter_px) {
    if (diameter_px < 16) diameter_px = 16;
    const int w = diameter_px, h = diameter_px;
    std::vector<unsigned char> rgba(static_cast<std::size_t>(w) * h * 4, 0u);

    // The button is rendered as two SAME-SIZE discs stacked with a small
    // vertical offset — the face on top, the (darker) "edge" disc just
    // below and slightly behind. Only the slice of the edge that sticks
    // out below the face is visible, so the dark color reads as the
    // visible "side" of a coin viewed from a hair above rather than as
    // an outline ring around the disc.
    const float disc_r   = std::min(w, h) * 0.46f;
    const float offset_y = std::min(w, h) * 0.07f;
    const float face_cx  = w * 0.5f;
    const float face_cy  = h * 0.5f - offset_y * 0.5f;
    const float edge_cx  = w * 0.5f;
    const float edge_cy  = h * 0.5f + offset_y * 0.5f;

    // Icon (refresh arrow) geometry. Centered on the face so the glyph
    // sits entirely on the light disc, clear of the crescent.
    const float icx = face_cx;
    const float icy = face_cy;
    const float ring_r_out = disc_r * 0.62f;
    const float ring_r_in  = disc_r * 0.40f;
    const float ring_r_mid = 0.5f * (ring_r_in + ring_r_out);
    const float thickness  = ring_r_out - ring_r_in;
    const float gap_half   = 0.50f;        // ~28.6° half-width of the right-side gap

    // Arrowhead triangle. The arc's clockwise terminus sits at the TOP of
    // the gap (angle = -gap_half). The triangle base is perpendicular to
    // the tangent there (i.e., radial), spanning a bit past the ring's
    // inner/outer edges; the apex extends along the clockwise tangent so
    // the arrow visually "exits" the gap and continues the spin.
    const float gap_top_a = -gap_half;
    const float rcx = std::cos(gap_top_a);
    const float rcy = std::sin(gap_top_a);
    const float tcx = -std::sin(gap_top_a);  // clockwise tangent (screen y down)
    const float tcy =  std::cos(gap_top_a);
    const float ext = thickness * 0.55f;
    const float base_inner_r = std::max(0.0f, ring_r_in - ext);
    const float base_outer_r = ring_r_out + ext;
    const float apex_len     = thickness * 2.1f;
    const float p1x = icx + base_inner_r * rcx;
    const float p1y = icy + base_inner_r * rcy;
    const float p2x = icx + base_outer_r * rcx;
    const float p2y = icy + base_outer_r * rcy;
    const float p3x = icx + ring_r_mid   * rcx + apex_len * tcx;
    const float p3y = icy + ring_r_mid   * rcy + apex_len * tcy;

    auto edge_fn = [](float ax, float ay, float bx, float by,
                      float px, float py) {
        return (bx - ax) * (py - ay) - (by - ay) * (px - ax);
    };
    const float orient = edge_fn(p1x, p1y, p2x, p2y, p3x, p3y);
    const float sign = (orient >= 0.0f) ? 1.0f : -1.0f;
    auto in_arrow = [&](float px, float py) {
        float s1 = edge_fn(p1x, p1y, p2x, p2y, px, py) * sign;
        float s2 = edge_fn(p2x, p2y, p3x, p3y, px, py) * sign;
        float s3 = edge_fn(p3x, p3y, p1x, p1y, px, py) * sign;
        return s1 >= 0.0f && s2 >= 0.0f && s3 >= 0.0f;
    };

    // Disc colors: rank-1 tile palette. The hard step between body and edge
    // at band_top_y gives the same 3D shelf-look the tiles have.
    constexpr float body_rC = 0x66, body_gC = 0xCC, body_bC = 0xFF;
    constexpr float edge_rC = 0x5F, edge_gC = 0xA9, edge_bC = 0xF1;
    constexpr float icon_rC = 0xFF, icon_gC = 0xFF, icon_bC = 0xFF;

    // 4×4 supersampling — the icon has curved edges and a triangle tip;
    // generous sampling keeps both crisp at typical small sizes.
    constexpr int N = 4;
    const float step = 1.0f / N;
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            int body_hits = 0, edge_hits = 0, icon_hits = 0;
            for (int sy = 0; sy < N; ++sy) {
                for (int sx = 0; sx < N; ++sx) {
                    float px = static_cast<float>(x) + (sx + 0.5f) * step;
                    float py = static_cast<float>(y) + (sy + 0.5f) * step;
                    // Membership: in the face disc → "face"; in the edge
                    // disc but not the face → "rim crescent". Both discs
                    // are the same size; the offset puts the rim's visible
                    // slice entirely below the face (and a thin sliver of
                    // it curving up the lower-sides).
                    float dxf = px - face_cx, dyf = py - face_cy;
                    float rad_f = std::sqrt(dxf * dxf + dyf * dyf);
                    float dxe = px - edge_cx, dye = py - edge_cy;
                    float rad_e = std::sqrt(dxe * dxe + dye * dye);
                    if (rad_f < disc_r) {
                        ++body_hits;
                    } else if (rad_e < disc_r) {
                        ++edge_hits;
                    }
                    bool ring_hit = false;
                    float dix = px - icx, diy = py - icy;
                    float irad = std::sqrt(dix * dix + diy * diy);
                    if (irad >= ring_r_in && irad <= ring_r_out) {
                        float ang = std::atan2(diy, dix);
                        if (ang < -gap_half || ang > gap_half) ring_hit = true;
                    }
                    if (ring_hit || in_arrow(px, py)) ++icon_hits;
                }
            }
            const float a_body = body_hits / float(N * N);
            const float a_edge = edge_hits / float(N * N);
            const float a_disc = a_body + a_edge;
            const float a_icon = icon_hits / float(N * N);
            if (a_disc <= 0.0f && a_icon <= 0.0f) continue;

            // Blend body vs edge contribution within the disc, then composite
            // the icon on top.
            float dr_ = 0.0f, dg_ = 0.0f, db_ = 0.0f;
            if (a_disc > 0.0f) {
                dr_ = (body_rC * a_body + edge_rC * a_edge) / a_disc;
                dg_ = (body_gC * a_body + edge_gC * a_edge) / a_disc;
                db_ = (body_bC * a_body + edge_bC * a_edge) / a_disc;
            }
            const float ao = a_icon + a_disc * (1.0f - a_icon);
            if (ao < 0.0005f) continue;
            const float cr = (icon_rC * a_icon + dr_ * a_disc * (1.0f - a_icon)) / ao;
            const float cg = (icon_gC * a_icon + dg_ * a_disc * (1.0f - a_icon)) / ao;
            const float cb = (icon_bC * a_icon + db_ * a_disc * (1.0f - a_icon)) / ao;
            int idx = (y * w + x) * 4;
            rgba[idx + 0] = static_cast<unsigned char>(cr);
            rgba[idx + 1] = static_cast<unsigned char>(cg);
            rgba[idx + 2] = static_cast<unsigned char>(cb);
            rgba[idx + 3] = static_cast<unsigned char>(ao * 255.0f);
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
