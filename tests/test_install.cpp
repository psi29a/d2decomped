// Install detection over a synthetic machine in a temp dir: MPQ magic,
// hand-made VS_FIXEDFILEINFO blobs, hand-written Wine .reg text and a
// hand-encoded product.db. No Blizzard bytes. Also d2d's data-dir order.
// Prints what detect() finds on this machine (no assertions on that).
#include <install.hpp>

#include <algorithm>
#include <array>
#include <cassert>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace fs = std::filesystem;
using namespace d2d::install;

namespace {

void write(const fs::path& path, const std::string& bytes) {
    fs::create_directories(path.parent_path());
    std::ofstream(path, std::ios::binary) << bytes;
}

void mpq(const fs::path& path) { write(path, std::string("MPQ\x1A", 4) + std::string(28, '\0')); }

std::string le32(std::uint32_t value) {
    return { char(value), char(value >> 8), char(value >> 16), char(value >> 24) };
}

// "MZ", padding, then a VS_FIXEDFILEINFO head: signature, struct version, file version MS/LS.
void pe(const fs::path& path, std::uint16_t a, std::uint16_t b, std::uint16_t c, std::uint16_t d) {
    write(path, "MZ" + std::string(0x102, '\0') + le32(0xFEEF04BD) + le32(0x00010000) +
                    le32(std::uint32_t(a) << 16 | b) + le32(std::uint32_t(c) << 16 | d) + std::string(64, '\0'));
}

std::string varint(std::uint64_t value) {
    std::string out;
    do {
        out += char((value & 0x7F) | (value > 0x7F ? 0x80 : 0));
        value >>= 7;
    } while (value);
    return out;
}
std::string field(int number, const std::string& bytes) { return varint(std::uint64_t(number) << 3 | 2) + varint(bytes.size()) + bytes; }
std::string product(const std::string& code, const std::string& path) {
    return field(1, field(1, "uid_" + code) + field(2, code) + field(3, field(1, path) + field(2, "us")));
}

std::span<const std::byte> span(const std::string& bytes) { return std::as_bytes(std::span(bytes.data(), bytes.size())); }

// Path -> (size, mtime) of everything under root, symlinks not followed.
std::map<fs::path, std::pair<std::uintmax_t, fs::file_time_type>> snapshot(const fs::path& root) {
    std::map<fs::path, std::pair<std::uintmax_t, fs::file_time_type>> out;
    for (const auto& entry : fs::recursive_directory_iterator(root)) {
        if (entry.is_symlink()) { out[entry.path()] = {}; continue; }
        out[entry.path()] = { entry.is_regular_file() ? entry.file_size() : 0, entry.last_write_time() };
    }
    return out;
}

// A classic folder: d2data (+ d2exp when lod), Game.exe at `version` unless all zero.
void classic(const fs::path& dir, bool lod, std::array<std::uint16_t, 4> version) {
    mpq(dir / "d2data.mpq");
    if (lod) mpq(dir / "d2exp.mpq");
    if (version[0]) pe(dir / "Game.exe", version[0], version[1], version[2], version[3]);
}

}  // namespace

int run() {
    const auto root = fs::temp_directory_path() / "d2d_test_install";
    fs::remove_all(root);
    const auto lod114d = root / "lod114d", lod114b = root / "lod114b", lod113 = root / "lod113", lod109 = root / "lod109";
    const auto nolod = root / "nolod", mac = root / "mac", bare = root / "bare", d2r = root / "d2r", d2r_casc = root / "d2r_casc";
    const auto stale = root / "stale", fake = root / "fakempq", vs_dir = root / "vs_dir", vs_twin = root / "vs_twin";

    classic(lod114d, true, { 1, 14, 3, 71 });
    mpq(lod114d / "patch_d2.mpq");
    mpq(lod114d / "D2Char.MPQ");                    // mixed case, still found
    classic(lod114b, true, { 1, 14, 1, 68 });
    mpq(lod113 / "D2DATA.MPQ");                     // upper case, no Game.exe, D2Client.dll
    mpq(lod113 / "D2EXP.MPQ");
    write(lod113 / "D2Client.dll", "");
    classic(lod109, true, { 1, 0, 13, 60 });
    classic(nolod, false, { 1, 14, 3, 71 });
    classic(mac, true, { 0, 0, 0, 0 });
    mpq(mac / "patch_d2.mpq");
    classic(bare, true, { 0, 0, 0, 0 });
    classic(root / "devbin", true, { 0, 0, 0, 0 });   // the launcher's bin/ import, no patch_d2.mpq
    pe(root / "devbin" / "bin" / "game.exe", 1, 14, 3, 71);
    write(d2r / "D2R.exe", "MZ");
    write(d2r_casc / ".build.info", "Branch!STRING:0|Product!STRING:0\n");
    fs::create_directories(d2r_casc / "Data" / "data");
    fs::create_directories(stale);
    write(fake / "d2data.mpq", "not an mpq");
    classic(vs_dir, true, { 1, 14, 1, 68 });        // VirtualStore twin holds the patched files
    pe(vs_twin / "Game.exe", 1, 14, 3, 71);
    mpq(vs_twin / "patch_d2.mpq");

    // --- Wine prefix: 64-bit layout, a dosdevices d: symlink ------------------
    const auto prefix = root / "home" / ".wine", ddrive = root / "ddrive";
    fs::create_directories(prefix / "drive_c" / "Program Files (x86)");
    fs::create_directories(prefix / "dosdevices");
    // Windows needs admin or developer mode for symlinks; Wine never runs there anyway.
    std::error_code no_link;
    fs::create_directory_symlink(ddrive, prefix / "dosdevices" / "d:", no_link);
    classic(ddrive / "Games" / "Diablo II", true, { 1, 14, 3, 71 });
    classic(prefix / "drive_c" / "Program Files" / "Diablo II", false, { 1, 14, 3, 71 });   // classic, no LoD
    classic(prefix / "drive_c" / "Games" / "D2 114b", true, { 1, 14, 1, 68 });              // only product.db knows it
    write(prefix / "drive_c" / "Program Files (x86)" / "Diablo II Resurrected" / "D2R.exe", "MZ");
    write(prefix / "user.reg",
          "WINE REGISTRY Version 2\n;; All keys relative to \\\\User\\\\S-1-5-21\n\n#arch=win64\n\n"
          "[Software\\\\Blizzard Entertainment\\\\Diablo II] 1700000000\n#time=1d9\n"
          "\"InstallPath\"=\"C:\\\\Old CD\\\\Diablo II\"\n"           // stale: dropped
          "\"Resolution\"=dword:00000001\n");
    write(prefix / "system.reg",
          "WINE REGISTRY Version 2\n\n"
          "[Software\\\\Wow6432Node\\\\Blizzard Entertainment\\\\Diablo II] 1700000000\n"
          "\"InstallPath\"=\"d:\\\\games\\\\diablo ii\\\\\"\n\n"       // case and trailing slash differ
          "[Software\\\\Wow6432Node\\\\Microsoft\\\\Windows\\\\CurrentVersion\\\\Uninstall\\\\Diablo II Resurrected] 1700000000\n"
          "\"DisplayName\"=\"Diablo II Resurrected\"\n"
          "\"InstallLocation\"=\"C:\\\\Program Files (x86)\\\\Diablo II Resurrected\"\n\n"
          "[Software\\\\Wow6432Node\\\\Microsoft\\\\Windows\\\\CurrentVersion\\\\Uninstall\\\\Notepad++] 1700000000\n"
          "\"DisplayName\"=\"Notepad++\"\n");
    write(prefix / "drive_c" / "ProgramData" / "Battle.net" / "Agent" / "product.db",
          product("osi", "C:/Program Files (x86)/Diablo II Resurrected") + product("zz", "C:/Games/D2 114b"));

    // --- native Windows-shaped machine behind a lambda registry ---------------
    const auto pf86 = root / "pf86", pf = root / "pf", lad = root / "lad", apps = root / "apps", pd = root / "pd";
    const auto hklm_dir = root / "hklm" / "Diablo II", icon_dir = root / "icon" / "Diablo II";
    classic(pf86 / "Diablo II", true, { 1, 14, 1, 68 });
    pe(lad / "VirtualStore" / (pf86 / "Diablo II").relative_path() / "Game.exe", 1, 14, 3, 71);   // the patched one won
    classic(hklm_dir, true, { 1, 14, 3, 71 });
    classic(icon_dir, true, { 1, 13, 3, 60 });
    classic(apps / "Diablo II" / "Diablo II.app" / "Contents" / "Resources", true, { 0, 0, 0, 0 });
    write(pf / "Diablo II Resurrected" / "D2R.exe", "MZ");
    write(pd / "Battle.net" / "Agent" / "product.db", product("osi", (pf / "Diablo II Resurrected").string()));

    // --- d2d's data-dir order --------------------------------------------------
    const auto exe_dir = root / "exe", cwd = root / "cwd", cfg_dir = root / "cfg";
    mpq(exe_dir / "d2data.mpq");
    mpq(cwd / "D2DATA.MPQ");
    mpq(cfg_dir / "d2data.mpq");
    fs::create_directories(root / "empty");

    // --- Wine prefixes to discover --------------------------------------------
    const auto home = root / "home";
    fs::create_directories(home / "Games" / "diablo-ii" / "drive_c");
    fs::create_directories(home / "Games" / "not-a-prefix");
    fs::create_directories(home / ".steam/steam/steamapps/compatdata/2536520/pfx/drive_c");
    fs::create_directories(home / "Library/Containers/com.isaacmarovitz.Whisky/Bottles/B1/drive_c");
    fs::create_directories(root / "custom" / "drive_c");

    const auto before = snapshot(root);

    // --- file_version ---------------------------------------------------------
    assert((file_version(lod114d / "Game.exe") == std::array<std::uint16_t, 4>{ 1, 14, 3, 71 }));
    assert((file_version(lod114b / "Game.exe") == std::array<std::uint16_t, 4>{ 1, 14, 1, 68 }));
    assert((file_version(lod109 / "Game.exe") == std::array<std::uint16_t, 4>{ 1, 0, 13, 60 }));
    assert(!file_version(d2r / "D2R.exe"));            // too short
    assert(!file_version(lod114d / "d2data.mpq"));     // not a PE
    assert(!file_version(root / "missing.exe"));

    // --- find_file ------------------------------------------------------------
    assert(find_file(lod113, "d2data.mpq") && fs::exists(*find_file(lod113, "d2data.mpq")));
    assert(find_file(lod114d, "d2char.mpq"));
    assert(!find_file(lod114d, "d2sfx.mpq"));
    assert(!find_file(root / "nowhere", "d2data.mpq"));

    // --- classify -------------------------------------------------------------
    auto d = classify(lod114d, "t");
    assert(d && d->kind == Kind::classic && d->version == Version::v114d && d->expansion && usable(*d));
    assert(d->file_version == "1.14.3.71" && !d->patch_mpq.empty() && problem(*d).empty());
    assert(title(*d) == "Diablo II: Lord of Destruction 1.14d" && d->source == "t");
    assert(std::find(d->missing.begin(), d->missing.end(), "d2char.mpq") == d->missing.end());
    assert(std::find(d->missing.begin(), d->missing.end(), "d2sfx.mpq") != d->missing.end());
    auto b = classify(lod114b, "t");
    assert(b && b->version == Version::v114_other && usable(*b) && problem(*b).find("LODPatch_114d.exe") != std::string::npos);
    assert(title(*b) == "Diablo II: Lord of Destruction 1.14.1.68");
    auto old = classify(lod113, "t");
    assert(old && old->version == Version::legacy && old->expansion && old->game_exe.empty());
    assert(classify(lod109, "t")->version == Version::legacy);
    auto no_lod = classify(nolod, "t");
    assert(no_lod && !usable(*no_lod) && problem(*no_lod).find("Lord of Destruction needed") != std::string::npos);
    auto native_mac = classify(mac, "t");
    assert(native_mac && native_mac->version == Version::unknown && usable(*native_mac) && problem(*native_mac).empty());
    assert(problem(*classify(bare, "t")).find("LODPatch_114d.exe") != std::string::npos);
    auto devbin = classify(root / "devbin", "t");
    assert(devbin->version == Version::v114d && problem(*devbin).find("no patch_d2.mpq") != std::string::npos);
    auto r = classify(d2r, "t");
    assert(r && r->kind == Kind::resurrected && !usable(*r) && problem(*r).find("Resurrected") != std::string::npos);
    assert(classify(d2r_casc, "t")->kind == Kind::resurrected);
    assert(!classify(stale, "t"));
    assert(!classify(fake, "t"));                      // d2data.mpq without the MPQ magic
    assert(!classify(root / "nowhere", "t"));
    auto overlay = classify(vs_dir, "t", vs_twin);
    assert(overlay && overlay->version == Version::v114d && overlay->patch_mpq.parent_path() == vs_twin);
    assert(classify(vs_dir, "t")->version == Version::v114_other);

    // --- Wine .reg text -------------------------------------------------------
    const std::string reg =
        "[Software\\\\Blizzard Entertainment\\\\Diablo II] 1700000000\n"
        "\"InstallPath\"=\"C:\\\\Games\\\\Caf\\xe9 \\\"D2\\\"\"\n"
        "\"Expand\"=str(2):\"%ProgramFiles%\\\\D2\"\n"
        "\"Number\"=dword:00000001\n"
        "[Software\\\\Blizzard Entertainment\\\\Diablo II\\\\VideoConfig] 1700000000\n"
        "[Software\\\\Blizzard Entertainment\\\\Starcraft] 1700000000\n"
        "\"InstallPath\"=\"C:\\\\SC\"\n";
    assert(wine_reg_value(reg, R"(software\blizzard entertainment\diablo ii)", "installpath") == "C:\\Games\\Caf\xc3\xa9 \"D2\"");
    assert(wine_reg_value(reg, R"(Software\Blizzard Entertainment\Diablo II)", "Expand") == "%ProgramFiles%\\D2");
    assert(!wine_reg_value(reg, R"(Software\Blizzard Entertainment\Diablo II)", "Number"));
    assert(!wine_reg_value(reg, R"(Software\Blizzard Entertainment\Diablo II)", "Missing"));
    assert(wine_reg_value(reg, R"(Software\Blizzard Entertainment\Starcraft)", "InstallPath") == "C:\\SC");
    assert((wine_reg_subkeys(reg, R"(Software\Blizzard Entertainment)") == std::vector<std::string>{ "Diablo II", "Starcraft" }));
    assert(!wine_reg_value("", "a", "b") && !wine_reg_value("[unterminated\n\"a\"=\"b", "x", "a"));

    // --- Wine paths -----------------------------------------------------------
    assert(wine_to_host(prefix, R"(C:\Program Files (x86))") == prefix / "drive_c" / "Program Files (x86)");
    const auto via_d = wine_to_host(prefix, R"(D:\GAMES\diablo ii)");
    assert(no_link || (via_d && fs::exists(*via_d / "d2data.mpq")));
    assert(!wine_to_host(prefix, R"(E:\Games)"));      // no such drive
    assert(!wine_to_host(prefix, "relative\\path") && !wine_to_host(prefix, ""));

    // --- product.db -----------------------------------------------------------
    const std::string db = varint(5 << 3 | 0) + varint(300) + varint(6 << 3 | 5) + "abcd" +   // skipped fields
                           product("osi", "C:/D2R") + product("d2x", "C:/Classic") + field(1, field(2, "nopath"));
    const auto entries = read_product_db(span(db));
    assert(entries.size() == 2 && entries[0].code == "osi" && entries[0].path == "C:/D2R" && entries[1].path == "C:/Classic");
    assert(read_product_db(span(db.substr(0, db.size() - 20))).size() == 1);         // truncated: what parsed so far
    assert(read_product_db(span(varint(1 << 3 | 2) + varint(1000) + "short")).empty());  // over-long length
    assert(read_product_db(span(std::string("\x0f\xff\xff", 3))).empty());               // bad wire type
    assert(read_product_db({}).empty());

    // --- detect over the Wine prefix ------------------------------------------
    Environment wine_env;
    wine_env.home = root / "home";
    wine_env.wine_prefixes = { prefix };
    const auto wine = detect(wine_env);
    for (const auto& install : wine) std::printf("wine: %-45s %s  [%s]\n", title(install).c_str(), install.dir.string().c_str(), install.source.c_str());
    if (!no_link) {
    assert(wine.size() == 4);
    assert(wine[0].version == Version::v114d && wine[0].source == "Wine ~/.wine: registry HKLM");
    assert(wine[1].version == Version::v114_other && wine[1].source == "Wine ~/.wine: product.db zz");
    assert(!wine[2].expansion && wine[2].source == "Wine ~/.wine: default folder");
    assert(wine[3].kind == Kind::resurrected && wine[3].source == "Wine ~/.wine: uninstall HKLM");   // product.db's osi deduped
    }

    // --- detect over a Windows-shaped machine ---------------------------------
    Environment win;
    win.home = root / "home";
    win.program_files = { pf86, pf };
    win.local_app_data = lad;
    win.program_data = pd;
    win.app_dirs = { apps };
    win.registry = [&](Hive hive, std::string_view key, std::string_view value) -> std::optional<std::string> {
        const std::string k(key), v(value);
        if (k == R"(Software\Blizzard Entertainment\Diablo II)" && v == "InstallPath")
            return hive == Hive::current_user ? (root / "gone" / "Diablo II").string() : hklm_dir.string() + "\\";
        if (k.ends_with(R"(\Uninstall\Diablo II)") && v == "DisplayName") return "Diablo II";
        if (k.ends_with(R"(\Uninstall\Diablo II)") && v == "DisplayIcon") return "\"" + (icon_dir / "Game.exe").string() + "\",0";
        if (k.ends_with(R"(\Uninstall\Other)") && v == "DisplayName") return "Other";
        if (k.ends_with(R"(\Uninstall\Other)") && v == "InstallLocation") return (root / "lod113").string();
        return std::nullopt;
    };
    win.subkeys = [](Hive hive, std::string_view) {
        return hive == Hive::local_machine ? std::vector<std::string>{ "Diablo II", "Other" } : std::vector<std::string>{};
    };
    const auto machine = detect(win);
    for (const auto& install : machine) std::printf("win:  %-45s %s  [%s]\n", title(install).c_str(), install.dir.string().c_str(), install.source.c_str());
    assert(machine.size() == 5);
    assert(machine[0].dir == hklm_dir && machine[0].source == "registry HKLM");
    assert(machine[1].dir == pf86 / "Diablo II" && machine[1].version == Version::v114d && machine[1].source == "default folder");
    assert(machine[2].dir == icon_dir && machine[2].version == Version::legacy && machine[2].source == "uninstall HKLM");
    assert(machine[3].source == "Applications" && machine[3].version == Version::unknown);
    assert(machine[4].kind == Kind::resurrected && machine[4].source == "product.db osi");   // not lod113: wrong DisplayName

    // --- Wine prefix discovery ------------------------------------------------
    const auto found = wine_prefixes(home, (root / "custom").string(), false);
    assert(found.size() == 4 && found[0] == root / "custom" && found[1] == prefix);
    assert(found[2] == home / "Games" / "diablo-ii" && found[3] == home / ".steam/steam/steamapps/compatdata/2536520/pfx");
    assert(wine_prefixes(home, (root / "custom").string(), true).size() == 5);   // + Whisky, on request only
    assert(wine_prefixes(home, "", false).size() == 3);

    // --- d2d's data dir: first hit wins, each a hard stop ---------------------
    const auto none = root / "empty";
    auto data = resolve_data_dir(cfg_dir.string(), "", exe_dir, cwd, "");
    assert(data.dir == cfg_dir && data.from == "--data" && data.error.empty());
    data = resolve_data_dir(none.string(), cfg_dir.string(), exe_dir, cwd, cfg_dir.string());
    assert(data.dir.empty() && data.error.find("--data") == 0);                 // wrong --data: no fallback
    data = resolve_data_dir("", cfg_dir.string(), exe_dir, cwd, "");
    assert(data.dir == cfg_dir && data.from == "D2_MPQ_DIR");
    data = resolve_data_dir("", none.string(), exe_dir, cwd, cfg_dir.string());
    assert(data.dir.empty() && data.error.find("D2_MPQ_DIR") == 0);
    data = resolve_data_dir("", "", exe_dir, cwd, "");
    assert(data.dir == exe_dir && data.from == "drop-in");
    data = resolve_data_dir("", "", none, cwd, cfg_dir.string());
    assert(data.dir == cwd && data.from == "drop-in");                          // D2DATA.MPQ: case-insensitive
    data = resolve_data_dir("", "", none, none, cfg_dir.string());
    assert(data.dir == cfg_dir && data.from == "d2d.cfg");
    data = resolve_data_dir("", "", none, none, (root / "gone").string());
    assert(data.dir.empty() && data.error.find("d2d.cfg") == 0);
    data = resolve_data_dir("", "", none, none, "");
    assert(data.dir.empty() && data.error.find("no Diablo II data") == 0);

    // Read only: nothing the probes touched changed or appeared.
    assert(snapshot(root) == before);
    fs::remove_all(root);

    if (const char* dir = std::getenv("D2_MPQ_DIR"); dir && *dir) {
        const auto real = classify(dir, "D2_MPQ_DIR");
        assert(real && real->kind == Kind::classic);
        std::printf("D2_MPQ_DIR: %s, %s\n", title(*real).c_str(), problem(*real).c_str());
    }
    std::printf("This machine:\n");
    for (const auto& install : detect(system_environment()))
        std::printf("  %-45s %s  [%s]%s%s\n", title(install).c_str(), install.dir.string().c_str(), install.source.c_str(),
                    problem(install).empty() ? "" : "  ", problem(install).c_str());
    std::printf("OK\n");
    return 0;
}

// An uncaught filesystem_error is a silent abort on Windows; say what it was.
int main() {
    try {
        return run();
    } catch (const std::exception& error) {
        std::fprintf(stderr, "test_install: %s\n", error.what());
        return 1;
    }
}
