#pragma once
// macOS Magic Trackpad → synthetic pointer events. Translates two-finger
// precise scrolling (`NSEventTypeScrollWheel` with `hasPreciseScrollingDeltas`)
// into drag-style pointer events so we can iterate on swipe feel locally.

struct SDL_Window;

namespace triples::input {

struct TrackpadEvent {
    enum class Phase { Began, Changed, Ended };
    Phase phase;
    float dx, dy;   // accumulated delta from the gesture start, in screen pixels
};

// Install the NSEvent monitor on the given SDL window. Safe to call once at
// startup. No-op on non-Apple builds.
void macos_trackpad_init(SDL_Window* w);

// Pop one trackpad event from the queue. Returns false if empty. Drain in the
// app's main loop each tick.
bool macos_trackpad_poll(TrackpadEvent* out);

}  // namespace triples::input
