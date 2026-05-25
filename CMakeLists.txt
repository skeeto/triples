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

# --- SDL3 via FetchContent (static, pinned to release-3.2.30) -----------------
include(FetchContent)
set(FETCHCONTENT_QUIET OFF)
set(SDL_SHARED OFF CACHE BOOL "" FORCE)
set(SDL_STATIC ON  CACHE BOOL "" FORCE)
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

# WIN32: link the GUI ("windows") subsystem so launching the .exe doesn't
# open a console window — SDL3 supplies the WinMain entry point.
# MACOSX_BUNDLE: produce Triples.app so Dock / Cmd-Tab show the bundled icon
# and metadata rather than a generic terminal binary.
# Both flags are silently ignored on platforms that don't apply.
add_executable(triples WIN32 MACOSX_BUNDLE
    ${APP_SOURCES} ${GAME_SOURCES} ${RENDER_SOURCES} ${INPUT_SOURCES} ${AUDIO_SOURCES}
    ${EMBEDDED_RESOURCE_SOURCES}
)
target_include_directories(triples PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/third_party
)
target_link_libraries(triples PRIVATE SDL3::SDL3-static)

if(EMSCRIPTEN)
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
if(NOT EMSCRIPTEN)
    enable_testing()
    add_subdirectory(tests)
endif()
