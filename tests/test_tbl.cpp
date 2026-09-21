// Parse a real string.tbl out of d2data.mpq and verify a couple of known
// English strings. Skips when the MPQ isn't reachable (same rule as test_mpq).
#include <mpq.hpp>
#include <tbl.hpp>

#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <filesystem>

namespace fs = std::filesystem;

int main() {
    const char* env = std::getenv("D2_MPQ_DIR");
    const fs::path dir = env ? fs::path(env)
        : fs::path(std::getenv("HOME") ? std::getenv("HOME") : "")
            / "Workspace/private/diablo2";
    const auto d2data = dir / "d2data.mpq";
    if (!fs::exists(d2data)) {
        std::printf("SKIP: %s not found\n", d2data.string().c_str());
        return 0;
    }

    d2d::mpq::Archive a(d2data);
    const auto raw = a.read(R"(data\local\LNG\ENG\string.tbl)");

    d2d::tbl::Table t(raw);
    std::printf("string.tbl entries: %zu\n", t.size());
    assert(t.size() > 1000);   // full string table is ~5-15k entries.

    // Sanity: a well-known key. D2 stores UI strings with formatting-code
    // prefixes/suffixes (colour codes like "\x01c8"), so equality against a
    // bare English word is fragile — check for presence + contents that end
    // in the expected word instead.
    auto ends_with = [](std::u16string_view s, std::u16string_view suf) {
        return s.size() >= suf.size()
            && s.substr(s.size() - suf.size()) == suf;
    };
    auto cancel = t.get("cancel");
    assert(cancel.has_value());
    assert(*cancel == u"CANCEL");    // D2 button labels are ALLCAPS.
    assert(ends_with(*cancel, u"ANCEL"));

    // Missing keys return nullopt, not an empty string.
    assert(!t.get("this key does not exist").has_value());

    std::printf("OK\n");
    return 0;
}
