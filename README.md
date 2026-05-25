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

The whole site is just four files — `index.html`, `index.js`,
`index.wasm`, and `triples-favicon.png`. To extract those alone (e.g.
for a `gh-pages` branch) without any of the CMake build-tree noise:

```sh
cmake --install build-web --prefix dist
# `dist/` now has exactly the four deployable files.
```

### Windows cross build from a Unix host (mingw-w64)

Useful for verifying the Windows-only build pieces (`.rc` + icon, Windows
subsystem, `persistence_win.cpp`) without leaving the host:

```sh
brew install mingw-w64        # macOS;  apt install mingw-w64  on Debian
cmake --preset mingw
cmake --build --preset mingw
```

`build-mingw/triples.exe` is a standalone, statically linked PE32+ binary.

### iOS Simulator

Requires Xcode + an iOS-Simulator runtime installed. The CMake build emits
an Xcode project; `cmake --build` drives `xcodebuild` under the hood. We're
targeting the Simulator only here — device builds need a paid Apple
Developer signing identity and an Apple-issued provisioning profile.

```sh
# Configure (~2 minutes — SDL3 reconfigures for iOS the first time):
cmake -G Xcode -S . -B build-ios \
  -DCMAKE_SYSTEM_NAME=iOS \
  -DCMAKE_OSX_SYSROOT=iphonesimulator \
  -DCMAKE_OSX_ARCHITECTURES=arm64 \
  -DCMAKE_OSX_DEPLOYMENT_TARGET=14.0

# Build the .app:
cmake --build build-ios --config Release

# Install into the booted Simulator and launch:
open -a Simulator                # boots a default device if none is booted
xcrun simctl install booted \
  build-ios/Release-iphonesimulator/Triples.app
xcrun simctl launch booted com.nullprogram.triples
```

On Intel Macs use `-DCMAKE_OSX_ARCHITECTURES=x86_64`. To build for a real
device later, swap `iphonesimulator` → `iphoneos` and add your Apple
Developer signing identity (you'll also need a registered bundle
identifier and a provisioning profile from the Developer portal).

`tools/genicon.cpp` doesn't yet emit iOS asset-catalog icons — the
Simulator will show the default generic icon on the home screen until we
wire that up.

### Android (APK, sideload)

Builds a debug-signed `.apk` you can install on a phone with `adb`.
Build flow is Gradle-driven (it invokes our CMake under the hood):

```sh
cd android-project
./gradlew assembleDebug      # first build is slow — fetches Gradle deps + SDL3
adb install -r app/build/outputs/apk/debug/app-debug.apk
```

The APK ships with both `arm64-v8a` (any modern phone) and `x86_64`
(Android Studio's emulator on Apple Silicon hosts) ABI variants.

**Prereqs.** Three Homebrew installs:

```sh
brew install --cask android-ndk android-commandlinetools
brew install openjdk@21       # AGP 8 + Gradle 8.12 want JDK 17 or 21,
                              # NOT the current homebrew openjdk (26)
```

(`android-studio` works in place of `android-commandlinetools` — Studio
ships an SDK Manager. But the commandlinetools cask is enough to build
from the CLI and doesn't require launching the IDE.)

One-time SDK package install (Android platform + build tools + a CMake
new enough to handle our `cmake_minimum_required(VERSION 3.24)` —
AGP's bundled CMake 3.22.1 doesn't):

```sh
yes | sdkmanager "platforms;android-35" "build-tools;35.0.0" \
                 "platform-tools" "cmake;3.30.5"
```

Tell Gradle where everything is via `android-project/local.properties`
(NOT committed — paths are per-machine):

```properties
sdk.dir=/opt/homebrew/share/android-commandlinetools
ndk.dir=/opt/homebrew/share/android-ndk
```

And tell Gradle to use JDK 21 either through `JAVA_HOME` (`export
JAVA_HOME=/opt/homebrew/opt/openjdk@21/libexec/openjdk.jdk/Contents/Home`
in your shell) or via `org.gradle.java.home=...` in
`android-project/gradle.properties`.

> The `ndkVersion` in `app/build.gradle` is pinned to the version
> Homebrew's `android-ndk` cask currently ships. If `brew upgrade
> android-ndk` lands a newer revision, bump that string to match
> `/opt/homebrew/share/android-ndk/source.properties` →
> `Pkg.Revision`.

To sideload, enable Developer Options + USB debugging on the phone,
plug it in, accept the RSA fingerprint prompt, then run the `adb install`
above. Emulator alternative: open Android Studio's AVD Manager, boot any
arm64-v8a image, then the same `adb install` command targets it.

## Packaging

For everything except iOS, CPack assembles the deliverable directly out of
the build tree:

```sh
cmake -S . -B build
cmake --build build
(cd build && cpack)
```

- macOS: `triples-<VERSION>-Darwin.dmg` (DragNDrop). Mount it, drag
  `Triples.app` to `/Applications`. The app is ad-hoc re-signed during
  install so Gatekeeper allows the first launch.
- Windows (native or mingw cross): `triples-<VERSION>-win64.zip` with
  `triples.exe` inside.

The web flavor doesn't use CPack — see the "Web (Emscripten)" section
above for the `cmake --install` flow that drops the deployable files into
a clean directory.

iOS isn't packaged via CPack either: CPack has no .ipa generator, and for
Simulator testing you just hand `xcrun simctl install` the `.app` from
the build directly. Device + App Store distribution needs
`xcodebuild archive` + `xcodebuild -exportArchive`, which is a separate
flow tied to a real Apple Developer signing identity.

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
                 $XDG_DATA_HOME / ~/Library / SDL_GetPrefPath sandbox)
shell/           Emscripten HTML shell + favicon
android-project/ Gradle scaffolding (Java SDLActivity subclass, manifest,
                 resources, adaptive-icon mipmaps) — points at the root
                 CMakeLists for native builds
tools/           icon generator
tests/           game-logic unit tests
```
