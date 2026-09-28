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

    d2d::mpq::Archive archive(d2data);
    const auto raw = archive.read(R"(data\local\LNG\ENG\string.tbl)");

    d2d::tbl::Table table(raw);
    std::printf("string.tbl entries: %zu\n", table.size());
    assert(table.size() > 1000);   // full string table is ~5-15k entries.

    // Sanity: a well-known key. D2 stores UI strings with formatting-code
    // prefixes/suffixes (colour codes like "\x01c8"), so equality against a
    // bare English word is fragile — check for presence + contents that end
    // in the expected word instead.
    auto ends_with = [](std::u16string_view text, std::u16string_view suf) {
        return text.size() >= suf.size()
            && text.substr(text.size() - suf.size()) == suf;
    };
    auto cancel = table.get("cancel");
    assert(cancel.has_value());
    assert(*cancel == u"CANCEL");    // D2 button labels are ALLCAPS.
    assert(ends_with(*cancel, u"ANCEL"));

    // Missing keys return nullopt, not an empty string.
    assert(!table.get("this key does not exist").has_value());
    assert(!table.get(std::uint16_t{0xFFFF}).has_value());

    // ID-based lookup: pull "cancel" back out via its Index field. We don't
    // know the exact ID a-priori, so scan every u16 to find the one that
    // yields the same value. Confirms the by-id map is consistent with the
    // by-key map for at least this canary entry.
    bool id_hit = false;
    for (std::uint32_t i = 0; i < 0x10000 && !id_hit; ++i) {
        if (auto found = table.get(std::uint16_t(i));
            found && *found == *cancel) { id_hit = true; }
    }
    assert(id_hit);

    std::printf("OK\n");
    return 0;
}
