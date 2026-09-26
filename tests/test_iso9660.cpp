// Smoke test for iso9660::Reader. Points at ~/Downloads/Diablo II + LoD/1.
// INSTALL DISC.ISO when it exists; otherwise reports skipped and passes.
// ponytail: one runnable check that fails loudly if the reader breaks. No
// framework, no fixtures.

#include <iso9660.hpp>

#include <cstdio>
#include <cstdlib>
#include <filesystem>

#define CHECK(x) do { if (!(x)) { \
    std::fprintf(stderr, "FAIL: %s (line %d)\n", #x, __LINE__); return 1; } \
} while (0)

int main() {
    namespace fs = std::filesystem;
    const char* home = std::getenv("HOME");
    if (!home) { std::puts("test_iso9660: no HOME, skipped"); return 0; }

    const fs::path iso = fs::path(home) / "Downloads"
        / "Diablo II + LoD" / "1. INSTALL DISC.ISO";
    if (!fs::exists(iso)) {
        std::printf("test_iso9660: %s not found, skipped\n", iso.string().c_str());
        return 0;
    }

    auto r = iso9660::Reader::open(iso);
    CHECK(r.has_value());

    const auto& es = r->entries();
    CHECK(es.size() > 10);

    std::size_t dirs = 0, files = 0;
    std::uint64_t total = 0;
    for (const auto& e : es) {
        if (e.directory) ++dirs;
        else { ++files; total += e.size; }
    }
    CHECK(dirs > 0);
    CHECK(files > 0);
    CHECK(total > 100ull * 1024 * 1024);   // install disc is ~500 MB

    // Round-trip: read the first small-ish file, confirm we got its bytes.
    for (const auto& e : es) {
        if (!e.directory && e.size > 0 && e.size < 4096) {
            auto bytes = r->read(e);
            CHECK(bytes.size() == e.size);
            std::printf("test_iso9660: OK — %zu entries, %llu bytes total, "
                        "sample=%s (%u B)\n",
                        es.size(), (unsigned long long)total,
                        e.path.c_str(), e.size);
            return 0;
        }
    }
    std::puts("test_iso9660: OK (no small file to round-trip, still valid)");
    return 0;
}
