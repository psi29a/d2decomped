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
        d2d::txt::Table table(std::as_bytes(std::span(src, std::strlen(src))));
        assert(table.size() == 3);                          // Expansion + blank dropped
        assert(table.get(1, "code") == "9ha");
        assert(table.get(1, "ALTERNATEGFX") == "hax");
        assert(table.get(2, "code").empty());               // short row
        assert(table.get(0, "nope").empty());
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
    auto tbl = [&](const char* name) {
        return d2d::txt::Table(mpqs.read(std::string(R"(data\global\excel\)") + name + ".txt"));
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
    for (const auto& truth_row : truth) {
        if (table[std::size_t(truth_row.idx)].code != truth_row.code) {
            std::printf("idx %d: got '%s' want '%s'\n", truth_row.idx,
                        table[std::size_t(truth_row.idx)].code.c_str(), truth_row.code);
            ++bad;
        }
    }
    assert(bad == 0);
    assert(d2d::compcode::kWClass[std::size_t(table[4].wclass)] == "1hs");  // hand axe
    assert(table[0].code.empty());             // 0 = no component
    assert(table[79].armor && !table[4].armor);

    // Weapon class from real saves' RH/LH/SH bytes (d2s class id first).
    using d2d::compcode::weapon_class;
    struct { const char* who; int cls; int right_hand, left_hand, shield; const char* want; } cases[] = {
        {"Lyndon BA flail+club",   4, 15,   12,   0xff, "1ss"},
        {"Joanna AM bow in LH",    0, 0xff, 55,   0xff, "bow"},
        {"Mule AS two claws",      6, 43,   43,   0xff, "ht1"},
        {"Eirena SO orb+tower",    1, 53,   0xff, 82,   "1hs"},
        {"Fedora NE wand",         2, 11,   0xff, 0xff, "1hs"},
        {"Dark_Savant PA dagger",  3, 25,   0xff, 93,   "1ht"},
        {"lone giant sword",       4, 24,   0xff, 0xff, "2hs"},
        {"claw on a barbarian",    4, 43,   0xff, 0xff, "hth"},
        {"empty hands",            5, 0xff, 0xff, 0xff, "hth"},
    };
    const char* class_codes[7] = {"AM", "SO", "NE", "PA", "BA", "DZ", "AI"};
    const bool have_chars = fs::exists(dir / "d2char.mpq");
    if (have_chars) mpqs.push(dir / "d2char.mpq");
    for (const auto& test_case : cases) {
        const auto got = weapon_class(test_case.cls, table, std::uint8_t(test_case.right_hand),
                                      std::uint8_t(test_case.left_hand), std::uint8_t(test_case.shield));
        if (got != test_case.want) std::printf("%s: got '%.*s' want '%s'\n", test_case.who,
                                       int(got.size()), got.data(), test_case.want);
        assert(got == test_case.want);
        if (have_chars) {   // the animation D2 would load must exist
            std::string cof = std::string(R"(data\global\CHARS\)") + class_codes[test_case.cls] +
                              R"(\COF\)" + class_codes[test_case.cls] + "TN" + std::string(got) + ".cof";
            assert(mpqs.contains(cof));
        }
    }
    std::printf("OK\n");
    return 0;
}
