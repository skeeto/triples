# Triples

A faithful clone of [Threes!](https://asherv.com/threes/) in C++ with
SDL3. The signature drag-based swipe feel — tiles slide progressively under
your finger, snap back below 25% commit, and lead-tile slows against a stuck
wall — is replicated precisely, which is what most 2048-style clones lose.
Mobile web is the primary target; native macOS / Linux / Windows builds are
also supported.

## Building

Requires a C++20 compiler and CMake 3.24+. SDL3 is fetched via FetchContent
(release-3.2.30, SHA256-pinned), no system install required. Fonts are
embedded into the binary at build time; audio is procedurally synthesized.

### Native (macOS / Linux / Windows)

```sh
cmake -S . -B build
cmake --build build
./build/triples              # Linux/Windows
open build/triples.app        # macOS — proper bundle with Dock icon
```

On macOS the build produces `Triples.app` with the multi-resolution `.icns`
inside; drag it to `/Applications` (or wherever) and it launches like a
normal Mac app. On Windows the `.exe` is the Windows-subsystem GUI flavor
(no console pops up), with the multi-resolution icon and VERSIONINFO baked
in via the resource compiler.

### Web (Emscripten)

```sh
emcmake cmake -S . -B build-web
cmake --build build-web
python3 -m http.server -d build-web    # then open index.html
```

`build-web/` is the deployable artifact: `index.html` + `index.js` +
`index.wasm` + `triples-favicon.png`.

### Windows cross build from a Unix host (mingw-w64)

Useful for verifying the Windows-only build pieces (`.rc` + icon, Windows
subsystem, `persistence_win.cpp`) without leaving the host:

```sh
brew install mingw-w64        # macOS;  apt install mingw-w64  on Debian
cmake --preset mingw
cmake --build --preset mingw
```

`build-mingw/triples.exe` is a standalone, statically linked PE32+ binary.

## Running tests

The pure game logic is `#include <SDL3/SDL.h>`-free and unit-tested:

```sh
cmake --build build
ctest --test-dir build --output-on-failure
```

## Controls

- **Touch / mouse**: drag a tile in a direction. Past 25% of a cell on
  release commits the move; less than that snaps back. You can drag past
  the start point in the opposite direction to back out of a locked
  direction mid-gesture.
- **Keyboard**: arrow keys or WASD; `R`, `Enter`, or `Space` to start a
  new game.
- **Magic Trackpad (macOS, native build)**: two-finger pan on the
  trackpad — uses the trackpad gesture phases, not scroll-wheel
  inertia, so the drag-feel matches touch.

## Regenerating the icon

The application icon (`.ico` + `.icns` + embedded RGBA + web favicon) is
generated from one C++ source so changing the design only touches one
file:

```sh
c++ -O2 -std=c++17 -Ithird_party -o /tmp/genicon tools/genicon.cpp
/tmp/genicon
```

Outputs `src/triples.ico`, `src/triples.icns`, `src/resources/icon.hpp`,
and `shell/triples-favicon.png`. All are committed; the tool only needs to
be rerun when the design changes.

## Layout

```
src/game/        pure game logic — SDL-free, fully unit-tested
src/render/      SDL3 rendering + animation
src/input/       drag state machine + macOS trackpad shim
src/audio/       procedural SFX (no asset files at runtime)
src/platform/    per-OS persistence (localStorage on web, %APPDATA% /
                 $XDG_DATA_HOME / ~/Library elsewhere)
shell/           Emscripten HTML shell + favicon
tools/           icon generator
tests/           game-logic unit tests
```
