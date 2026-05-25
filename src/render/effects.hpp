#pragma once
// Particle drawing helpers live here. The state lives in Animations; this
// header is just for the draw functions, which need an SDL_Renderer.

struct SDL_Renderer;

namespace triples::render {

class Animations;

// Draw sparkles + confetti in their current state.
void draw_particles(SDL_Renderer* r, const Animations& a);

}  // namespace triples::render
