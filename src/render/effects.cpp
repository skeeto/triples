#include "render/effects.hpp"

#include <SDL3/SDL.h>

#include "render/animation.hpp"

namespace triples::render {

namespace {

void draw_particle(SDL_Renderer* r, const Particle& p) {
    float life = (p.dur > 0.0f) ? (p.t / p.dur) : 1.0f;
    if (life > 1.0f) return;
    float a = (1.0f - life);
    a = a * a;  // ease out
    SDL_FRect rect{p.x - p.size * 0.5f, p.y - p.size * 0.5f, p.size, p.size};
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(r, p.r, p.g, p.b, static_cast<std::uint8_t>(a * 255.0f));
    SDL_RenderFillRect(r, &rect);
}

}  // namespace

void draw_particles(SDL_Renderer* r, const Animations& a) {
    for (const auto& p : a.sparkles)  draw_particle(r, p);
    for (const auto& p : a.confetti)  draw_particle(r, p);
}

}  // namespace triples::render
