#include "platform/persistence.hpp"

#include <SDL3/SDL.h>

#include <filesystem>
#include <fstream>
#include <sstream>

namespace triples::platform {

namespace {

// Resolve the app's writable per-user directory. On Android SDL maps this
// to /data/data/com.nullprogram.triples/files/ (or the equivalent app-private
// path the OS hands out). Returns "" on failure.
std::filesystem::path resolve_base_dir() {
    char* p = SDL_GetPrefPath("nullprogram", "triples");
    if (!p) return {};
    std::filesystem::path base(p);
    SDL_free(p);
    return base;
}

std::filesystem::path path_for_key(const std::filesystem::path& base,
                                   std::string_view key) {
    return base / (std::string(key) + ".dat");
}

struct AndroidStore : PersistenceStore {
    std::filesystem::path base;
    bool                  base_ok = false;

    AndroidStore() {
        base = resolve_base_dir();
        if (base.empty()) return;
        // SDL_GetPrefPath creates the directory itself, but make sure for
        // belt-and-suspenders. Cheap if it already exists.
        std::error_code ec;
        std::filesystem::create_directories(base, ec);
        base_ok = !ec;
    }

    std::optional<std::string> read(std::string_view key) override {
        if (!base_ok) return std::nullopt;
        std::ifstream in(path_for_key(base, key), std::ios::binary);
        if (!in) return std::nullopt;
        std::ostringstream ss;
        ss << in.rdbuf();
        return ss.str();
    }

    void write(std::string_view key, std::string_view value) override {
        if (!base_ok) return;
        auto p = path_for_key(base, key);
        // Atomic-ish: write to .tmp then rename. Avoids corruption on a
        // crash or OS kill mid-write.
        auto tmp = p;
        tmp += ".tmp";
        {
            std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
            if (!out) return;
            out.write(value.data(), static_cast<std::streamsize>(value.size()));
        }
        std::error_code ec;
        std::filesystem::rename(tmp, p, ec);
    }

    void remove(std::string_view key) override {
        if (!base_ok) return;
        std::error_code ec;
        std::filesystem::remove(path_for_key(base, key), ec);
    }
};

}  // namespace

std::unique_ptr<PersistenceStore> make_store() {
    return std::make_unique<AndroidStore>();
}

}  // namespace triples::platform
