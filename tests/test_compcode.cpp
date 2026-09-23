// Rebuild D2's composite component table from the real 1.14d excel data
// and check it against appearance bytes of real saves, paired with the
// items those characters had equipped (see docs/research/re/compcode.md).
#include <compcode.hpp>
#include <mpq.hpp>
#include <txt.hpp>

#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>

namespace fs = std::filesystem;

int main() {
    // txt::Table basics: CRLF, case-insensitive columns, separator rows.
    {
        const char* src = "name\tCode\talternateGfx\r\nHand Axe\thax\thax\r\n"
                          "Expansion\r\n\r\nHatchet\t9ha\thax\r\nshort\r\n";
        d2d::txt::Table t(std::as_bytes(std::span(src, std::strlen(src))));
        assert(t.size() == 3);                          // Expansion + blank dropped
        assert(t.get(1, "code") == "9ha");
        assert(t.get(1, "ALTERNATEGFX") == "hax");
        assert(t.get(2, "code").empty());               // short row
        assert(t.get(0, "nope").empty());
    }

    const char* env = std::getenv("D2_MPQ_DIR");
    const fs::path dir = env ? fs::path(env)
        : fs::path(std::getenv("HOME") ? std::getenv("HOME") : "") / "Workspace/private/diablo2";
    if (!fs::exists(dir / "d2data.mpq")) {
        std::printf("SKIP: %s not found\n", (dir / "d2data.mpq").string().c_str());
        return 0;
    }
    d2d::mpq::Stack mpqs;
    if (fs::exists(dir / "d2exp.mpq")) mpqs.push(dir / "d2exp.mpq");
    mpqs.push(dir / "d2data.mpq");
    auto tbl = [&](const char* n) {
        return d2d::txt::Table(mpqs.read(std::string(R"(data\global\excel\)") + n + ".txt"));
    };
    const auto table = d2d::compcode::build(tbl("ItemTypes"), tbl("weapons"),
                                            tbl("armor"), tbl("misc"));
    assert(table.size() == 0x100);

    // Appearance byte -> equipped item's graphic code, from 19 real saves.
    struct { int idx; const char* code; } truth[] = {
        {1, "lit"}, {2, "med"}, {3, "hvy"},
        {4, "hax"}, {11, "bwn"}, {12, "clb"}, {13, "mac"}, {15, "fla"}, {16, "mau"},
        {19, "flc"}, {25, "dgr"}, {28, "pil"}, {34, "hal"}, {38, "sst"},
        {43, "clw"}, {44, "skr"}, {51, "ob1"}, {53, "ob4"},
        {57, "cap"}, {60, "fhl"}, {61, "ghm"}, {62, "crn"},
        {79, "buc"}, {80, "lrg"}, {81, "kit"}, {82, "tow"}, {83, "bhm"},
        {89, "ba1"}, {90, "ba3"}, {91, "ba5"}, {93, "pa3"}, {94, "pa5"},
    };
    int bad = 0;
    for (const auto& t : truth) {
        if (table[std::size_t(t.idx)].code != t.code) {
            std::printf("idx %d: got '%s' want '%s'\n", t.idx,
                        table[std::size_t(t.idx)].code.c_str(), t.code);
            ++bad;
        }
    }
    assert(bad == 0);
    assert(table[4].wclass == "1hs");          // hand axe swings one-handed
    assert(table[0].code.empty());             // 0 = no component
    std::printf("OK\n");
    return 0;
}
