#include "platform/persistence.hpp"

#include <shlobj.h>
#include <windows.h>

#include <filesystem>
#include <fstream>
#include <sstream>

namespace triples::platform {

namespace {

std::filesystem::path resolve_base_dir() {
    char path[MAX_PATH];
    if (SUCCEEDED(SHGetFolderPathA(nullptr, CSIDL_APPDATA, nullptr, 0, path))) {
        return std::filesystem::path(path) / "Triples";
    }
    return {};
}

std::filesystem::path path_for_key(const std::filesystem::path& base, std::string_view key) {
    return base / (std::string(key) + ".dat");
}

struct WinStore : PersistenceStore {
    std::filesystem::path base;
    bool                  base_ok = false;

    WinStore() {
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
    return std::make_unique<WinStore>();
}

}  // namespace triples::platform
