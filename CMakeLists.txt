cmake_minimum_required(VERSION 3.24)
project(triples VERSION 1.0 LANGUAGES CXX C)

if(APPLE)
    enable_language(OBJCXX)
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

# --- SDL3 via FetchContent (static everywhere, shared on Android) -------------
# Android needs libSDL3.so as a standalone file inside the APK so the Java
# side's System.loadLibrary("SDL3") can find it. Everywhere else we statically
# link SDL3 into the executable for one-file distribution.
include(FetchContent)
set(FETCHCONTENT_QUIET OFF)
if(ANDROID)
    set(SDL_SHARED ON  CACHE BOOL "" FORCE)
    set(SDL_STATIC OFF CACHE BOOL "" FORCE)
else()
    set(SDL_SHARED OFF CACHE BOOL "" FORCE)
    set(SDL_STATIC ON  CACHE BOOL "" FORCE)
endif()
set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
set(SDL_TESTS OFF CACHE BOOL "" FORCE)
set(SDL_INSTALL OFF CACHE BOOL "" FORCE)
FetchContent_Declare(SDL3
    URL      https://github.com/libsdl-org/SDL/releases/download/release-3.2.30/SDL3-3.2.30.tar.gz
    URL_HASH SHA256=4c3b09330d866dc52eb65b66259a6684ad387252ca8c57901b3a2b534eb42e3d
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
FetchContent_MakeAvailable(SDL3)

# --- Embedded resources -------------------------------------------------------
include(${CMAKE_SOURCE_DIR}/cmake/EmbedResource.cmake)
embed_resource(font_inter_bold   assets/fonts/Inter-Bold.ttf)
embed_resource(font_jua_regular  assets/fonts/Jua-Regular.ttf)
# Audio is synthesized procedurally in src/audio/synth.cpp — no asset files.

# --- Source layout ------------------------------------------------------------
set(GAME_SOURCES
    src/game/board.cpp
    src/game/deck.cpp
    src/game/move.cpp
    src/game/rng.cpp
    src/game/score.cpp
    src/game/game_state.cpp
    src/game/serialize.cpp
)
set(RENDER_SOURCES
    src/render/renderer.cpp
    src/render/tile_draw.cpp
    src/render/text.cpp
    src/render/animation.cpp
    src/render/effects.cpp
)
set(INPUT_SOURCES
    src/input/input.cpp
    src/input/drag.cpp
)
set(AUDIO_SOURCES
    src/audio/synth.cpp
    src/audio/mixer.cpp
    src/audio/sfx.cpp
)
set(APP_SOURCES
    src/main.cpp
    src/app.cpp
)

# Android: the Java side loads our code via System.loadLibrary("triples"),
# which requires a SHARED libtriples.so. Everywhere else we build an
# executable — WIN32 picks the GUI subsystem on Windows (no console window),
# MACOSX_BUNDLE produces Triples.app for Dock / Cmd-Tab branding. Both flags
# are silently ignored on platforms they don't apply to.
if(ANDROID)
    add_library(triples SHARED
        ${APP_SOURCES} ${GAME_SOURCES} ${RENDER_SOURCES} ${INPUT_SOURCES} ${AUDIO_SOURCES}
        ${EMBEDDED_RESOURCE_SOURCES}
    )
else()
    add_executable(triples WIN32 MACOSX_BUNDLE
        ${APP_SOURCES} ${GAME_SOURCES} ${RENDER_SOURCES} ${INPUT_SOURCES} ${AUDIO_SOURCES}
        ${EMBEDDED_RESOURCE_SOURCES}
    )
endif()
target_include_directories(triples PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/third_party
)
# Android links against the shared SDL3 (libSDL3.so) Gradle packages
# alongside libtriples.so. Other platforms link the static SDL3 alias.
if(ANDROID)
    target_link_libraries(triples PRIVATE SDL3::SDL3-shared)
else()
    target_link_libraries(triples PRIVATE SDL3::SDL3-static)
endif()

if(ANDROID)
    # Android: app-private writable directory via SDL_GetPrefPath. No
    # special framework links — SDL3 (shared) and the NDK's libm/liblog
    # come in via SDL3::SDL3-shared above.
    target_sources(triples PRIVATE src/platform/persistence_android.cpp)
elseif(EMSCRIPTEN)
    target_sources(triples PRIVATE src/platform/persistence_web.cpp)
    target_link_options(triples PRIVATE
        -sALLOW_MEMORY_GROWTH=1
        -sINITIAL_MEMORY=33554432
        -sSTACK_SIZE=1048576
        -sEXPORTED_RUNTIME_METHODS=['ccall','cwrap']
        --shell-file=${CMAKE_SOURCE_DIR}/shell/index.html
    )
    set_target_properties(triples PROPERTIES
        SUFFIX ".html"
        OUTPUT_NAME "index"
    )
    # Copy the favicon next to index.html so the page's <link rel="icon">
    # resolves to a sibling file. Configure-time copy: regenerated only when
    # CMake reruns, which is fine since the PNG is a committed artifact.
    configure_file(
        ${CMAKE_SOURCE_DIR}/shell/triples-favicon.png
        ${CMAKE_CURRENT_BINARY_DIR}/triples-favicon.png
        COPYONLY)
    # Web deploy: `cmake --install build-web --prefix <dir>` drops just the
    # four files needed to serve the site — no CMakeFiles/, no _deps/, no
    # build-tree noise. Suitable for piping straight into a gh-pages branch.
    install(FILES
        "${CMAKE_CURRENT_BINARY_DIR}/index.html"
        "${CMAKE_CURRENT_BINARY_DIR}/index.js"
        "${CMAKE_CURRENT_BINARY_DIR}/index.wasm"
        "${CMAKE_CURRENT_BINARY_DIR}/triples-favicon.png"
        DESTINATION .)
elseif(CMAKE_SYSTEM_NAME STREQUAL "iOS")
    # iOS shares the Apple POSIX persistence (HOME resolves to the app's
    # sandbox root, so ~/Library/Application Support/Triples lands in the
    # writable per-app container). No trackpad shim — that's Cocoa/NSEvent,
    # macOS-only — and no AppKit framework, which iOS doesn't ship.
    target_sources(triples PRIVATE src/platform/persistence_posix.cpp)
    target_link_libraries(triples PRIVATE "-framework CoreFoundation")
    # iOS needs LSRequiresIPhoneOS, UIDeviceFamily, supported orientations,
    # and (iOS 13+) UILaunchScreen — none of which CMake's default macOS
    # Info.plist template carries. Use a fully custom plist instead.
    configure_file(${CMAKE_SOURCE_DIR}/src/Info-iOS.plist.in
                   ${CMAKE_CURRENT_BINARY_DIR}/Info-iOS.plist @ONLY)
    # Home-screen icons — bundled at the .app root (NOT under Resources/);
    # CFBundleIcons in Info-iOS.plist references the stems directly.
    set(_triples_ios_icons
        ${CMAKE_CURRENT_SOURCE_DIR}/src/AppIcon60x60@2x.png
        ${CMAKE_CURRENT_SOURCE_DIR}/src/AppIcon60x60@3x.png
        ${CMAKE_CURRENT_SOURCE_DIR}/src/AppIcon76x76@2x.png)
    target_sources(triples PRIVATE ${_triples_ios_icons})
    set_source_files_properties(${_triples_ios_icons} PROPERTIES
        MACOSX_PACKAGE_LOCATION "")
    set_target_properties(triples PROPERTIES
        MACOSX_BUNDLE_INFO_PLIST            ${CMAKE_CURRENT_BINARY_DIR}/Info-iOS.plist
        MACOSX_BUNDLE_BUNDLE_NAME           "Triples"
        MACOSX_BUNDLE_BUNDLE_VERSION        "${PROJECT_VERSION}"
        MACOSX_BUNDLE_SHORT_VERSION_STRING  "${PROJECT_VERSION}"
        MACOSX_BUNDLE_GUI_IDENTIFIER        "com.nullprogram.triples"
        XCODE_ATTRIBUTE_PRODUCT_BUNDLE_IDENTIFIER "com.nullprogram.triples"
        XCODE_ATTRIBUTE_TARGETED_DEVICE_FAMILY    "1,2"
        # Skip code signing in the simulator build — local testing only.
        XCODE_ATTRIBUTE_CODE_SIGN_IDENTITY        ""
        XCODE_ATTRIBUTE_CODE_SIGNING_REQUIRED     "NO"
        XCODE_ATTRIBUTE_CODE_SIGNING_ALLOWED      "NO")
elseif(APPLE)
    target_sources(triples PRIVATE
        src/platform/persistence_posix.cpp
        src/input/trackpad_macos.mm
    )
    set_source_files_properties(src/input/trackpad_macos.mm
        PROPERTIES COMPILE_FLAGS "-fobjc-arc")
    target_link_libraries(triples PRIVATE
        "-framework AppKit"
        "-framework CoreFoundation"
    )
    # Bundle: drop src/triples.icns into Triples.app/Contents/Resources/ and
    # populate Info.plist via MACOSX_BUNDLE_* properties. CMake auto-builds
    # the Info.plist from these.
    set(_triples_icns "${CMAKE_CURRENT_SOURCE_DIR}/src/triples.icns")
    target_sources(triples PRIVATE "${_triples_icns}")
    set_source_files_properties("${_triples_icns}" PROPERTIES
        MACOSX_PACKAGE_LOCATION "Resources")
    set_target_properties(triples PROPERTIES
        MACOSX_BUNDLE_BUNDLE_NAME           "Triples"
        MACOSX_BUNDLE_BUNDLE_VERSION        "${PROJECT_VERSION}"
        MACOSX_BUNDLE_SHORT_VERSION_STRING  "${PROJECT_VERSION}"
        MACOSX_BUNDLE_GUI_IDENTIFIER        "com.nullprogram.triples"
        MACOSX_BUNDLE_ICON_FILE             "triples.icns")
elseif(WIN32)
    target_sources(triples PRIVATE src/platform/persistence_win.cpp)
    target_link_libraries(triples PRIVATE shell32)
    # Compile src/triples.ico into the .exe via a generated .rc file
    # (resource script). The .rc is configure_file'd from the .rc.in template
    # so @PROJECT_VERSION@ substitutes cleanly without windres -D quoting.
    # windres needs the .ico on its include path; the .rc references it by
    # bare filename.
    enable_language(RC)
    configure_file(${CMAKE_CURRENT_SOURCE_DIR}/src/triples.rc.in
                   ${CMAKE_CURRENT_BINARY_DIR}/triples.rc @ONLY)
    configure_file(${CMAKE_CURRENT_SOURCE_DIR}/src/triples.ico
                   ${CMAKE_CURRENT_BINARY_DIR}/triples.ico COPYONLY)
    target_sources(triples PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/triples.rc)
else()
    target_sources(triples PRIVATE src/platform/persistence_posix.cpp)
endif()

# --- Tests (desktop only) -----------------------------------------------------
# Skip on web, iOS, and Android — none of these toolchains is set up to run
# host-built test binaries from the build directory.
if(NOT EMSCRIPTEN AND NOT CMAKE_SYSTEM_NAME STREQUAL "iOS" AND NOT ANDROID)
    enable_testing()
    add_subdirectory(tests)
endif()

# --- Packaging via CPack ------------------------------------------------------
# macOS: produces triples-<VERSION>.dmg containing Triples.app — the standard
# drag-to-Applications install. The bundle is ad-hoc re-signed during install
# because CPack's pipeline (install_name_tool rewrites, layout copies) breaks
# the build-time signature; without re-signing Gatekeeper refuses to open the
# app on first launch from /Applications.
#
# Windows (incl. the mingw cross): produces a ZIP with triples.exe inside.
#
# iOS isn't packaged via CPack — the deliverable for Simulator testing is the
# Xcode-built .app directory, which `xcrun simctl install` handles directly.
# Android isn't either — Gradle assembles the .so + Java + resources into an
# .apk, separate from this CMake build.
if(NOT EMSCRIPTEN AND NOT CMAKE_SYSTEM_NAME STREQUAL "iOS" AND NOT ANDROID)
    install(TARGETS triples
        RUNTIME DESTINATION .
        BUNDLE  DESTINATION .)

    if(APPLE)
        install(CODE [[
            message(STATUS "Ad-hoc re-signing ${CMAKE_INSTALL_PREFIX}/triples.app")
            execute_process(
                COMMAND codesign --force --sign -
                        "${CMAKE_INSTALL_PREFIX}/triples.app")
        ]])
    endif()

    set(CPACK_PACKAGE_NAME                "triples")
    set(CPACK_PACKAGE_VERSION             "${PROJECT_VERSION}")
    set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Triples — a Threes! clone in C++/SDL3")
    set(CPACK_PACKAGE_VENDOR              "nullprogram")
    set(CPACK_VERBATIM_VARIABLES          YES)
    set(CPACK_INCLUDE_TOPLEVEL_DIRECTORY  OFF)  # flat layout in the .dmg/.zip

    if(APPLE)
        set(CPACK_GENERATOR     "DragNDrop")
        set(CPACK_DMG_VOLUME_NAME "Triples ${PROJECT_VERSION}")
    elseif(WIN32)
        set(CPACK_GENERATOR "ZIP")
        # Friendlier filename than CMake's default "Windows-AMD64".
        if(CMAKE_SYSTEM_NAME STREQUAL "Windows")
            set(CPACK_SYSTEM_NAME "win64")
        endif()
    endif()

    include(CPack)
endif()
