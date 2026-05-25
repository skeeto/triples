#import <AppKit/AppKit.h>

#include <SDL3/SDL.h>

#include <deque>
#include <mutex>

#include "input/trackpad_macos.hpp"

namespace triples::input {

namespace {

std::mutex                  g_mu;
std::deque<TrackpadEvent>   g_queue;
id                          g_monitor = nil;
float                       g_accum_x = 0.0f;
float                       g_accum_y = 0.0f;
bool                        g_active  = false;

}  // namespace

void macos_trackpad_init(SDL_Window* w) {
    if (g_monitor) return;
    if (!w) return;

    g_monitor = [NSEvent
        addLocalMonitorForEventsMatchingMask:NSEventMaskScrollWheel
        handler:^NSEvent*(NSEvent* e) {
            if (!e.hasPreciseScrollingDeltas) return e;
            if (e.momentumPhase != NSEventPhaseNone) {
                // Ignore inertial momentum — we want raw finger control.
                return e;
            }
            TrackpadEvent ev;
            // With natural scrolling on (the default), NSEvent scroll deltas
            // already match finger direction on both axes.
            float dx = static_cast<float>(e.scrollingDeltaX);
            float dy = static_cast<float>(e.scrollingDeltaY);
            switch (e.phase) {
                case NSEventPhaseBegan:
                    g_accum_x = 0.0f;
                    g_accum_y = 0.0f;
                    g_active = true;
                    ev.phase = TrackpadEvent::Phase::Began;
                    ev.dx = 0.0f;
                    ev.dy = 0.0f;
                    break;
                case NSEventPhaseChanged:
                    if (!g_active) return e;
                    g_accum_x += dx;
                    g_accum_y += dy;
                    ev.phase = TrackpadEvent::Phase::Changed;
                    ev.dx = g_accum_x;
                    ev.dy = g_accum_y;
                    break;
                case NSEventPhaseEnded:
                case NSEventPhaseCancelled:
                    if (!g_active) return e;
                    g_active = false;
                    ev.phase = TrackpadEvent::Phase::Ended;
                    ev.dx = g_accum_x;
                    ev.dy = g_accum_y;
                    break;
                default:
                    return e;
            }
            {
                std::lock_guard<std::mutex> lock(g_mu);
                g_queue.push_back(ev);
                if (g_queue.size() > 256) g_queue.pop_front();
            }
            // Consume so the event doesn't scroll some unintended target.
            return nil;
        }];
}

bool macos_trackpad_poll(TrackpadEvent* out) {
    std::lock_guard<std::mutex> lock(g_mu);
    if (g_queue.empty()) return false;
    *out = g_queue.front();
    g_queue.pop_front();
    return true;
}

}  // namespace triples::input
