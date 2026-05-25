#include <SDL3/SDL.h>

#include "app.hpp"

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif

namespace {

triples::App* g_app = nullptr;

#ifdef __EMSCRIPTEN__
void web_tick() {
    if (g_app) g_app->tick();
}
#endif

}  // namespace

#ifdef __EMSCRIPTEN__
// Exported for the JS shell so it can initialize SDL3 audio from inside the
// first user-gesture stack frame, which iOS Safari requires for the
// AudioContext to start running instead of suspended.
extern "C" EMSCRIPTEN_KEEPALIVE void triples_init_audio() {
    if (g_app) g_app->init_audio_now();
}
#endif

int main(int /*argc*/, char* /*argv*/[]) {
    triples::App app;
    if (!app.initialize()) return 1;
    g_app = &app;

#ifdef __EMSCRIPTEN__
    // Let requestAnimationFrame drive the rate (0 = use rAF).
    emscripten_set_main_loop(web_tick, 0, 1);
    return 0;
#else
    while (app.tick()) {}
    return 0;
#endif
}
