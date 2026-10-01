// D2Decomp install detection: find the user's classic Diablo II (1.14d,
// with Lord of Destruction) where it is installed and read it in place.
// Diablo II: Resurrected is recognised and refused. docs/design/install-detect.md.
//
// Plain C++ (no Qt, no StormLib). Only reads: no copy, no rename, no
// registry writes. The registry is asked for install locations only
// (InstallPath, uninstall DisplayName/InstallLocation/DisplayIcon/
// UninstallString); nothing else under Blizzard's key is read.
// Everything that touches the machine goes through Environment, so tests
// fake a whole machine in a temp dir.
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace d2d::install {

enum class Kind    { classic, resurrected };
enum class Version { v114d, v114_other, legacy, unknown };

struct Install {
    std::filesystem::path dir;        // where d2data.mpq is; read in place
    std::filesystem::path game_exe;   // empty if none
    std::filesystem::path patch_mpq;  // patch_d2.mpq (maybe in VirtualStore), empty if none
    Kind        kind = Kind::classic;
    Version     version = Version::unknown;
    std::string file_version;         // "1.14.3.71", empty if no Game.exe
    bool        expansion = false;    // d2exp.mpq
    std::vector<std::string> missing; // optional MPQs not found
    std::string source;               // "registry HKCU", "Wine ~/.wine: default folder", ...
};

// Usable by d2d: classic with Lord of Destruction. Version may be off
// (the launcher then points `patch =` at LODPatch_114d.exe).
bool usable(const Install&);
// Why it can't be used, or what's off; empty when it's a plain 1.14d.
std::string problem(const Install&);
// "Diablo II: Lord of Destruction 1.14d" and the like.
std::string title(const Install&);

enum class Hive { current_user, local_machine };

using Registry = std::function<std::optional<std::string>(Hive, std::string_view key, std::string_view value)>;
using Subkeys  = std::function<std::vector<std::string>(Hive, std::string_view key)>;

struct Environment {
    std::filesystem::path home, program_data, local_app_data;   // program_data: Windows %ProgramData%, macOS /Users/Shared
    std::vector<std::filesystem::path> program_files;   // Windows: (x86) first
    std::vector<std::filesystem::path> app_dirs;        // macOS: /Applications, ~/Applications
    std::vector<std::filesystem::path> wine_prefixes;   // already discovered
    Registry registry;                                  // Windows only; empty elsewhere
    Subkeys  subkeys;
};

// The real machine. search_more adds probes that may prompt (macOS TCC: Whisky's container).
auto system_environment(bool search_more = false) -> Environment;
// Wine prefixes under `home` (Linux: ~/.wine, Lutris, Bottles, Proton;
// macOS: CrossOver, Porting Kit, Whisky with search_more), after `wineprefix` ($WINEPREFIX).
auto wine_prefixes(const std::filesystem::path& home, std::string_view wineprefix, bool search_more)
    -> std::vector<std::filesystem::path>;
// Unique by canonical dir; usable 1.14d first, then other usable classic,
// then classic without LoD, then D2R.
auto detect(const Environment&) -> std::vector<Install>;
// `virtual_store`: a UAC VirtualStore twin of `dir`; its Game.exe / patch_d2.mpq win.
auto classify(const std::filesystem::path& dir, std::string source,
              const std::filesystem::path& virtual_store = {}) -> std::optional<Install>;

// `dir / name`, matching the name case-insensitively when the exact
// spelling isn't there (Wine and Linux copies: D2DATA.MPQ, D2Data.mpq).
auto find_file(const std::filesystem::path& dir, std::string_view name) -> std::optional<std::filesystem::path>;

// Where d2d's MPQs are, first hit wins and each is a hard stop
// (decision 5): --data / $D2_MPQ_DIR as given, then a drop-in d2data.mpq
// beside the binary or in the working dir, then `data =` in d2d.cfg.
struct DataDir {
    std::filesystem::path dir;   // empty on error
    std::string from;            // "--data", "D2_MPQ_DIR", "drop-in", "d2d.cfg"
    std::string error;           // set when nothing usable: say why and what to do
};
auto resolve_data_dir(std::string_view cli, std::string_view env, const std::filesystem::path& exe_dir,
                      const std::filesystem::path& cwd, std::string_view cfg_data) -> DataDir;

// Pieces, public for tests.
auto file_version(const std::filesystem::path& pe) -> std::optional<std::array<std::uint16_t, 4>>;
auto wine_reg_value(std::string_view reg_text, std::string_view key,
                    std::string_view name) -> std::optional<std::string>;
auto wine_reg_subkeys(std::string_view reg_text, std::string_view key) -> std::vector<std::string>;
auto wine_to_host(const std::filesystem::path& prefix, std::string_view windows_path)
    -> std::optional<std::filesystem::path>;
struct ProductInstall { std::string code, path; };
auto read_product_db(std::span<const std::byte>) -> std::vector<ProductInstall>;

}  // namespace d2d::install
