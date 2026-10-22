// SPDX-License-Identifier: GPL-3.0-or-later
// shortcuts.vdf: foreign entries survive byte for byte; adding D2Decomp
// twice (or over a hand-made entry) leaves one entry with one appid.
#include <steam_shortcuts.hpp>

#include <cassert>
#include <cstdint>
#include <cstdio>
#include <exception>
#include <filesystem>
#include <fstream>
#include <ios>
#include <iterator>
#include <string>
#include <string_view>
#include <vector>

using namespace std::string_literals;
using namespace std::string_view_literals;
namespace steam = d2d::steam;

int main() try {
    // Another game, as Steam writes it, with a float and a uint64 it may add.
    const std::string foreign =
        "\x00shortcuts\x00"
            "\x00" "0\x00"
                "\x02" "appid\x00\x01\x02\x03\x84"
                "\x01" "AppName\x00Other\x00"
                "\x01" "Exe\x00\"/usr/bin/other\"\x00"
                "\x03" "scale\x00\x00\x00\x80\x3f"
                "\x07" "big\x00\x01\x02\x03\x04\x05\x06\x07\x08"
                "\x00" "tags\x00\x01" "0\x00" "favorite\x00\x08"
            "\x08"
        "\x08"
        "\x08"s;
    std::vector<steam::Node> file = steam::parse(foreign);
    assert(steam::serialize(file) == foreign);

    const steam::Shortcut d2decomp{"D2Decomp", "\"/usr/bin/flatpak\"", "\"/usr/bin/\"",
                                   "run io.github.psi29a.d2decomped", "io.github.psi29a.d2decomped"};
    const std::uint32_t appid = steam::number(steam::upsert(file, d2decomp).children, "appid");
    assert(appid & 0x80000000U);
    assert(steam::number(steam::upsert(file, d2decomp).children, "appid") == appid);
    auto& games = steam::find(file, "shortcuts")->children;
    assert(games.size() == 2 && games[1].key == "1");
    assert(steam::text(games[1].children, "LaunchOptions") == "run io.github.psi29a.d2decomped");
    assert(steam::number(games[0].children, "appid") == 0x84030201U);

    // Round trip of the new file, and the old entry untouched inside it.
    std::vector<steam::Node> again = steam::parse(steam::serialize(file));
    assert(steam::serialize(again) == steam::serialize(file));

    // A hand-made entry (Add a Non-Steam Game on the launcher) is taken over.
    std::vector<steam::Node> manual = steam::parse(foreign);
    auto& other = steam::find(manual, "shortcuts")->children[0].children;
    steam::set_text(other, "AppName", "D2Decomp");
    assert(steam::number(steam::upsert(manual, d2decomp).children, "appid") == 0x84030201U);
    assert(steam::find(manual, "shortcuts")->children.size() == 1);
    assert(steam::text(other, "Exe") == "\"/usr/bin/flatpak\"");

    // Garbage is refused, not rewritten.
    bool threw = false;
    try { steam::parse("\x00shortcuts\x00\x02" "appid\x00\x01"sv); } catch (const std::exception&) { threw = true; }
    assert(threw);

    // add_to_account on a fresh dir: creates the file; a second run backs it up.
    const auto dir = std::filesystem::temp_directory_path() / "d2d_test_steam";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);
    const std::uint32_t first = steam::add_to_account(dir, d2decomp);
    assert(steam::add_to_account(dir, d2decomp) == first);
    assert(std::filesystem::exists(dir / "shortcuts.vdf.d2d-backup"));
    std::string written;
    {   // closed before remove_all: Windows can't delete an open file
        std::ifstream input(dir / "shortcuts.vdf", std::ios::binary);
        written.assign(std::istreambuf_iterator<char>(input), {});
    }
    std::vector<steam::Node> reread = steam::parse(written);
    auto& entry = steam::find(steam::find(reread, "shortcuts")->children, "0")->children;
    assert(steam::text(entry, "icon") == (dir / "grid" / (std::to_string(first) + "_icon.png")).string());
    std::filesystem::remove_all(dir);

    std::puts("test_steam_shortcuts: ok");
    return 0;
} catch (const std::exception& error) {
    std::printf("test_steam_shortcuts: %s\n", error.what());
    return 1;
}
