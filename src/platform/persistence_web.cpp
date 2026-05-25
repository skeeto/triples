#include "platform/persistence.hpp"

#include <emscripten.h>

#include <cstdlib>
#include <cstring>

namespace triples::platform {

namespace {

// JS shims around window.localStorage. Returned strings are malloc'd on the
// JS side (via stringToNewUTF8) and freed here.
EM_JS(char*, ts_read, (const char* key), {
    try {
        const v = window.localStorage.getItem(UTF8ToString(key));
        if (v === null) return 0;
        return stringToNewUTF8(v);
    } catch (e) {
        return 0;
    }
});

EM_JS(void, ts_write, (const char* key, const char* value), {
    try {
        window.localStorage.setItem(UTF8ToString(key), UTF8ToString(value));
    } catch (e) {
        // QuotaExceededError, SecurityError, etc. — silently ignore.
    }
});

EM_JS(void, ts_remove, (const char* key), {
    try { window.localStorage.removeItem(UTF8ToString(key)); } catch (e) {}
});

struct WebStore : PersistenceStore {
    std::optional<std::string> read(std::string_view key) override {
        std::string k(key);
        char* v = ts_read(k.c_str());
        if (!v) return std::nullopt;
        std::string out(v);
        std::free(v);
        return out;
    }
    void write(std::string_view key, std::string_view value) override {
        std::string k(key), v(value);
        ts_write(k.c_str(), v.c_str());
    }
    void remove(std::string_view key) override {
        std::string k(key);
        ts_remove(k.c_str());
    }
};

}  // namespace

std::unique_ptr<PersistenceStore> make_store() {
    return std::make_unique<WebStore>();
}

}  // namespace triples::platform
