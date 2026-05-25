#pragma once
#include <cstdint>

namespace triples::input {

// Unified pointer-event abstraction. SDL touch / mouse / synthetic trackpad
// events all turn into one of these.
enum class PointerKind : std::uint8_t { Down, Move, Up, Cancel };

struct PointerEvent {
    PointerKind kind;
    float       x = 0.0f;   // device pixels, relative to canvas top-left
    float       y = 0.0f;
    // Source tag so we can ignore mouse events while a touch is active and
    // vice-versa. Synthetic Magic Trackpad events use Source::Trackpad.
    enum class Source : std::uint8_t { Mouse, Touch, Trackpad } source = Source::Mouse;
};

}  // namespace triples::input
