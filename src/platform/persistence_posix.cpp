#include "platform/persistence.hpp"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace triples::platform {

namespace {

std::filesystem::path resolve_base_dir() {
    namespace fs = std::filesystem;
#if defined(__APPLE__)
    const char* home = std::getenv("HOME");
    if (!home || !*home) return {};
    return fs::path(home) / "Library" / "Application Support" / "Triples";
#else
    const char* xdg = std::getenv("XDG_DATA_HOME");
    if (xdg && *xdg) return fs::path(xdg) / "triples";
    const char* home = std::getenv("HOME");
    if (!home || !*home) return {};
    return fs::path(home) / ".local" / "share" / "triples";
#endif
}

std::filesystem::path path_for_key(const std::filesystem::path& base, std::string_view key) {
    return base / (std::string(key) + ".dat");
}

struct PosixStore : PersistenceStore {
    std::filesystem::path base;
    bool                  base_ok = false;

    PosixStore() {
        base = resolve_base_dir();
        if (base.empty()) return;
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
        // Atomic-ish: write to .tmp, then rename. Avoids corruption on crash mid-write.
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
    return std::make_unique<PosixStore>();
}

}  // namespace triples::platform
