#pragma once
#include <memory>
#include <optional>
#include <string>
#include <string_view>

namespace triples::platform {

// Tiny key-value persistence. Two keys are used: "game.current" (live state)
// and "game.highscores" (top-8 entries). Values are compact text; the store
// just deals in strings.
struct PersistenceStore {
    virtual ~PersistenceStore() = default;
    virtual std::optional<std::string> read(std::string_view key) = 0;
    virtual void write(std::string_view key, std::string_view value) = 0;
    virtual void remove(std::string_view key) = 0;
};

// Returns a backing-store appropriate for the platform.
//   web:   localStorage via EM_JS
//   macOS: ~/Library/Application Support/Triples/
//   Linux: $XDG_DATA_HOME/triples/ (default ~/.local/share/triples)
//   win:   %APPDATA%/Triples/
std::unique_ptr<PersistenceStore> make_store();

}  // namespace triples::platform
