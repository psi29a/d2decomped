// Definitions for install.hpp: the probes, classification, VS_FIXEDFILEINFO,
// Wine .reg text, Wine path mapping and the product.db reader.
#include "install.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <system_error>
#include <utility>

#if defined(_WIN32)
#  include <windows.h>
#endif

namespace d2d::install {

namespace fs = std::filesystem;

namespace {

constexpr std::string_view kBlizzardKey  = R"(Software\Blizzard Entertainment\Diablo II)";
constexpr std::string_view kUninstallKey = R"(Software\Microsoft\Windows\CurrentVersion\Uninstall)";

char lower(char c) { return char(std::tolower(static_cast<unsigned char>(c))); }

bool iequals(std::string_view a, std::string_view b) {
    return a.size() == b.size() && std::equal(a.begin(), a.end(), b.begin(), [](char x, char y) { return lower(x) == lower(y); });
}

bool istarts_with(std::string_view text, std::string_view prefix) {
    return text.size() >= prefix.size() && iequals(text.substr(0, prefix.size()), prefix);
}

// UTF-8 in, a path out on every OS (Windows paths are wide).
fs::path utf8_path(std::string_view text) { return fs::path(std::u8string(text.begin(), text.end())); }

std::string path_utf8(const fs::path& path) {
    const auto text = path.u8string();
    return std::string(text.begin(), text.end());
}

std::optional<std::string> read_file(const fs::path& path, std::uintmax_t cap) {
    std::error_code error;
    const auto size = fs::file_size(path, error);
    if (error || size > cap) return std::nullopt;
    std::ifstream input(path, std::ios::binary);
    if (!input) return std::nullopt;
    std::string bytes(std::size_t(size), '\0');
    input.read(bytes.data(), std::streamsize(size));
    if (!input) return std::nullopt;
    return bytes;
}

bool mpq_magic(const fs::path& path) {
    std::ifstream input(path, std::ios::binary);
    char magic[4] = {};
    if (!input.read(magic, 4)) return false;
    // MPQ\x1A: the archive header; MPQ\x1B: a user-data block in front of it.
    return magic[0] == 'M' && magic[1] == 'P' && magic[2] == 'Q' && (magic[3] == '\x1A' || magic[3] == '\x1B');
}

bool is_dir(const fs::path& path) {
    std::error_code error;
    return fs::is_directory(path, error);
}

// Directory entries without throwing (permission errors end the walk).
std::vector<fs::path> entries(const fs::path& dir) {
    std::vector<fs::path> out;
    std::error_code error;
    for (auto it = fs::directory_iterator(dir, fs::directory_options::skip_permission_denied, error);
         !error && it != fs::directory_iterator(); it.increment(error))
        out.push_back(it->path());
    std::sort(out.begin(), out.end());   // stable order for the list and the tests
    return out;
}

std::uint32_t le32(std::string_view bytes, std::size_t at) {
    return std::uint32_t(std::uint8_t(bytes[at])) | std::uint32_t(std::uint8_t(bytes[at + 1])) << 8 |
           std::uint32_t(std::uint8_t(bytes[at + 2])) << 16 | std::uint32_t(std::uint8_t(bytes[at + 3])) << 24;
}

void append_utf8(std::string& out, std::uint32_t code) {
    if (code < 0x80) out += char(code);
    else if (code < 0x800) { out += char(0xC0 | code >> 6); out += char(0x80 | (code & 0x3F)); }
    else { out += char(0xE0 | code >> 12); out += char(0x80 | (code >> 6 & 0x3F)); out += char(0x80 | (code & 0x3F)); }
}

// Wine's string escapes: \\ \" \n \r \t \0 and \xH..HHHH (a UTF-16 unit).
// ponytail: surrogate pairs come out as two 3-byte sequences (CESU-8); a
// non-BMP path then won't exist and the default-folder probes cover it.
std::string unescape(std::string_view text) {
    std::string out;
    for (std::size_t i = 0; i < text.size(); ++i) {
        if (text[i] != '\\' || i + 1 == text.size()) { out += text[i]; continue; }
        const char next = text[++i];
        switch (next) {
        case 'n': out += '\n'; break;
        case 'r': out += '\r'; break;
        case 't': out += '\t'; break;
        case '0': out += '\0'; break;
        case 'x': {
            std::uint32_t code = 0;
            int digits = 0;
            while (digits < 4 && i + 1 < text.size() && std::isxdigit(static_cast<unsigned char>(text[i + 1]))) {
                const char digit = lower(text[++i]);
                code = code * 16 + std::uint32_t(digit <= '9' ? digit - '0' : digit - 'a' + 10);
                ++digits;
            }
            append_utf8(out, code);
            break;
        }
        default: out += next;   // \\ and \" and anything unknown
        }
    }
    return out;
}

// A "quoted \"string\"" at the start of `text`: its raw inside and the rest after it.
std::optional<std::pair<std::string_view, std::string_view>> quoted(std::string_view text) {
    if (text.empty() || text.front() != '"') return std::nullopt;
    for (std::size_t i = 1; i < text.size(); ++i) {
        if (text[i] == '\\') { ++i; continue; }
        if (text[i] == '"') return std::pair{ text.substr(1, i - 1), text.substr(i + 1) };
    }
    return std::nullopt;
}

// Calls fn(section, line) for each value line of a .reg text; section unescaped.
template <class Fn>
void for_each_reg_line(std::string_view text, Fn&& fn) {
    std::string section;
    while (!text.empty()) {
        const auto newline = text.find('\n');
        auto line = text.substr(0, newline);
        text = newline == text.npos ? std::string_view{} : text.substr(newline + 1);
        if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
        if (line.empty()) continue;
        if (line.front() == '[') {
            const auto close = line.rfind(']');
            section = close == line.npos ? std::string{} : unescape(line.substr(1, close - 1));
            fn(std::string_view(section), std::string_view{});
            continue;
        }
        fn(std::string_view(section), line);
    }
}

// The folder of a Windows file path in an uninstall value:
// `"C:\Games\Diablo II\Game.exe",0` or `C:\...\Uninstall.exe /arg`.
std::string windows_parent(std::string_view value) {
    std::string_view path = value;
    if (!path.empty() && path.front() == '"') {
        path.remove_prefix(1);
        path = path.substr(0, path.find('"'));
    } else if (const auto exe = [&] {
                   for (std::size_t i = 0; i + 4 <= path.size(); ++i)
                       if (iequals(path.substr(i, 4), ".exe")) return i + 4;
                   return path.npos;
               }(); exe != path.npos) {
        path = path.substr(0, exe);
    }
    const auto slash = path.find_last_of("\\/");
    return slash == path.npos ? std::string{} : std::string(path.substr(0, slash));
}

std::string clean_windows_path(std::string_view value) {
    while (!value.empty() && (value.front() == '"' || value.front() == ' ')) value.remove_prefix(1);
    while (!value.empty() && (value.back() == '"' || value.back() == ' ' || value.back() == '\\' || value.back() == '/'))
        value.remove_suffix(1);
    return std::string(value);
}

std::string pretty(const fs::path& path, const fs::path& home) {
    const auto text = path_utf8(path), home_text = path_utf8(home);
    if (!home_text.empty() && text.starts_with(home_text) && (text.size() == home_text.size() || text[home_text.size()] == '/'))
        return "~" + text.substr(home_text.size());
    return text;
}

// Collects classified hits; detect() dedupes and sorts afterwards.
struct Probe {
    std::vector<Install>& out;
    void add(const fs::path& dir, const std::string& source, const fs::path& virtual_store = {}) {
        if (dir.empty() || !is_dir(dir)) return;
        if (auto hit = classify(dir, source, virtual_store)) out.push_back(std::move(*hit));
    }
};

using MapPath = std::function<std::optional<fs::path>(std::string_view windows_path)>;

// W1–W3: Blizzard's InstallPath in both hives, then the uninstall keys.
void probe_registry(const Registry& registry, const Subkeys& subkeys, const MapPath& map, const std::string& label,
                    const std::function<void(const fs::path&, const std::string&)>& add) {
    if (!registry) return;
    auto hive_name = [](Hive hive) { return hive == Hive::current_user ? "HKCU" : "HKLM"; };
    auto try_add = [&](std::string_view windows_path, const std::string& source) {
        const auto cleaned = clean_windows_path(windows_path);
        if (cleaned.empty()) return;
        if (auto host = map(cleaned)) add(*host, label + source);
    };
    for (const Hive hive : { Hive::current_user, Hive::local_machine })
        if (const auto path = registry(hive, kBlizzardKey, "InstallPath"))
            try_add(*path, std::string("registry ") + hive_name(hive));
    if (!subkeys) return;
    for (const Hive hive : { Hive::local_machine, Hive::current_user })
        for (const auto& sub : subkeys(hive, kUninstallKey)) {
            const std::string key = std::string(kUninstallKey) + "\\" + sub;
            const auto name = registry(hive, key, "DisplayName");
            if (!name || !name->starts_with("Diablo II")) continue;
            const std::string source = std::string("uninstall ") + hive_name(hive);
            if (const auto location = registry(hive, key, "InstallLocation"); location && !clean_windows_path(*location).empty())
                try_add(*location, source);
            else if (const auto icon = registry(hive, key, "DisplayIcon"))
                try_add(windows_parent(*icon), source);
            else if (const auto uninstall = registry(hive, key, "UninstallString"))
                try_add(windows_parent(*uninstall), source);
        }
}

void probe_product_db(const fs::path& file, const MapPath& map, const std::string& label,
                      const std::function<void(const fs::path&, const std::string&)>& add) {
    const auto bytes = read_file(file, 8u << 20);
    if (!bytes) return;
    for (const auto& entry : read_product_db(std::as_bytes(std::span(bytes->data(), bytes->size()))))
        if (auto host = map(clean_windows_path(entry.path))) add(*host, label + "product.db " + entry.code);
}

constexpr std::string_view kD2RFolders[] = {
    "Diablo II Resurrected", R"(Battle.net\Diablo II Resurrected)", R"(Steam\steamapps\common\Diablo II Resurrected)",
};

#if defined(_WIN32)
std::wstring widen(std::string_view text) {
    if (text.empty()) return {};
    std::wstring out(std::size_t(MultiByteToWideChar(CP_UTF8, 0, text.data(), int(text.size()), nullptr, 0)), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.data(), int(text.size()), out.data(), int(out.size()));
    return out;
}

std::string narrow(std::wstring_view text) {
    if (text.empty()) return {};
    std::string out(std::size_t(WideCharToMultiByte(CP_UTF8, 0, text.data(), int(text.size()), nullptr, 0, nullptr, nullptr)), '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.data(), int(text.size()), out.data(), int(out.size()), nullptr, nullptr);
    return out;
}

// D2 is 32-bit, so its HKLM values sit in the 32-bit view
// (WOW6432Node on 64-bit Windows); the 64-bit view is read too, it's cheap.
std::optional<std::string> windows_registry(Hive hive, std::string_view key, std::string_view value) {
    const HKEY root = hive == Hive::current_user ? HKEY_CURRENT_USER : HKEY_LOCAL_MACHINE;
    const auto wkey = widen(key), wvalue = widen(value);
    for (const DWORD view : { DWORD(RRF_SUBKEY_WOW6432KEY), DWORD(RRF_SUBKEY_WOW6464KEY) }) {
        DWORD size = 0;
        if (RegGetValueW(root, wkey.c_str(), wvalue.c_str(), RRF_RT_REG_SZ | view, nullptr, nullptr, &size) != ERROR_SUCCESS ||
            size == 0)
            continue;
        std::wstring buffer(size / sizeof(wchar_t), L'\0');
        if (RegGetValueW(root, wkey.c_str(), wvalue.c_str(), RRF_RT_REG_SZ | view, nullptr, buffer.data(), &size) != ERROR_SUCCESS)
            continue;
        buffer.resize(wcsnlen(buffer.c_str(), buffer.size()));
        return narrow(buffer);
    }
    return std::nullopt;
}

std::vector<std::string> windows_subkeys(Hive hive, std::string_view key) {
    const HKEY root = hive == Hive::current_user ? HKEY_CURRENT_USER : HKEY_LOCAL_MACHINE;
    std::vector<std::string> out;
    for (const REGSAM view : { REGSAM(KEY_WOW64_32KEY), REGSAM(KEY_WOW64_64KEY) }) {
        HKEY handle{};
        if (RegOpenKeyExW(root, widen(key).c_str(), 0, KEY_ENUMERATE_SUB_KEYS | view, &handle) != ERROR_SUCCESS) continue;
        for (DWORD index = 0;; ++index) {
            wchar_t name[256];
            DWORD length = 256;
            const auto status = RegEnumKeyExW(handle, index, name, &length, nullptr, nullptr, nullptr, nullptr);
            if (status == ERROR_MORE_DATA) continue;
            if (status != ERROR_SUCCESS) break;
            auto text = narrow(std::wstring_view(name, length));
            if (std::none_of(out.begin(), out.end(), [&](const std::string& seen) { return iequals(seen, text); }))
                out.push_back(std::move(text));
        }
        RegCloseKey(handle);
    }
    return out;
}
#endif

}  // namespace

auto find_file(const fs::path& dir, std::string_view name) -> std::optional<fs::path> {
    std::error_code error;
    const auto exact = dir / utf8_path(name);
    if (fs::exists(exact, error)) return exact;
    for (const auto& entry : entries(dir))
        if (iequals(path_utf8(entry.filename()), name)) return entry;
    return std::nullopt;
}

auto file_version(const fs::path& pe) -> std::optional<std::array<std::uint16_t, 4>> {
    // ponytail: scans the whole file (Game.exe 1.14d is ~3.5 MB) for the
    // VS_FIXEDFILEINFO signature instead of walking the resource tree;
    // capped at 64 MB.
    const auto bytes = read_file(pe, 64u << 20);
    if (!bytes || bytes->size() < 64 || !bytes->starts_with("MZ")) return std::nullopt;
    const std::string_view data(*bytes);
    for (std::size_t at = 0; at + 16 <= data.size(); at += 4) {
        if (le32(data, at) != 0xFEEF04BD || le32(data, at + 4) != 0x00010000) continue;   // signature, struct version 1.0
        const auto ms = le32(data, at + 8), ls = le32(data, at + 12);
        return std::array<std::uint16_t, 4>{ std::uint16_t(ms >> 16), std::uint16_t(ms), std::uint16_t(ls >> 16), std::uint16_t(ls) };
    }
    return std::nullopt;
}

auto wine_reg_value(std::string_view reg_text, std::string_view key, std::string_view name) -> std::optional<std::string> {
    std::optional<std::string> found;
    for_each_reg_line(reg_text, [&](std::string_view section, std::string_view line) {
        if (found || line.empty() || !iequals(section, key)) return;
        const auto value_name = quoted(line);
        if (!value_name || !iequals(unescape(value_name->first), name)) return;
        auto rest = value_name->second;
        if (rest.empty() || rest.front() != '=') return;
        rest.remove_prefix(1);
        if (rest.starts_with("str(2):")) rest.remove_prefix(7);   // REG_EXPAND_SZ
        if (const auto value = quoted(rest)) found = unescape(value->first);
    });
    return found;
}

auto wine_reg_subkeys(std::string_view reg_text, std::string_view key) -> std::vector<std::string> {
    std::vector<std::string> out;
    const std::string prefix = std::string(key) + "\\";
    for_each_reg_line(reg_text, [&](std::string_view section, std::string_view line) {
        if (!line.empty() || !istarts_with(section, prefix) || section.size() == prefix.size()) return;
        auto child = section.substr(prefix.size());
        child = child.substr(0, child.find('\\'));
        if (std::none_of(out.begin(), out.end(), [&](const std::string& seen) { return iequals(seen, child); }))
            out.emplace_back(child);
    });
    return out;
}

auto wine_to_host(const fs::path& prefix, std::string_view windows_path) -> std::optional<fs::path> {
    if (windows_path.size() < 2 || windows_path[1] != ':' || !std::isalpha(static_cast<unsigned char>(windows_path[0])))
        return std::nullopt;
    const char letter = lower(windows_path[0]);
    // c: is drive_c; other letters go through the dosdevices symlinks (z: -> /).
    fs::path host = letter == 'c' ? prefix / "drive_c" : prefix / "dosdevices" / (std::string(1, letter) + ":");
    if (!is_dir(host)) return std::nullopt;
    auto rest = windows_path.substr(2);
    while (!rest.empty()) {
        const auto slash = rest.find_first_of("\\/");
        const auto part = rest.substr(0, slash);
        rest = slash == rest.npos ? std::string_view{} : rest.substr(slash + 1);
        if (part.empty() || part == ".") continue;
        // Wine matches each component case-insensitively; so do we.
        const auto next = find_file(host, part);
        host = next ? *next : host / utf8_path(part);
    }
    return host;
}

auto read_product_db(std::span<const std::byte> bytes) -> std::vector<ProductInstall> {
    // Protobuf wire format, three nested messages (TACTLib ProtoDatabase.cs):
    // Database{1: ProductInstall}, ProductInstall{2: code, 3: UserSettings},
    // UserSettings{1: install_path}. Unknown fields are skipped; a bad
    // length or wire type ends that message.
    struct Field { std::uint64_t number; std::span<const std::byte> bytes; };
    auto fields = [](std::span<const std::byte> message) {
        std::vector<Field> out;
        std::size_t at = 0;
        auto varint = [&]() -> std::optional<std::uint64_t> {
            std::uint64_t value = 0;
            for (int shift = 0; shift < 64 && at < message.size(); shift += 7) {
                const auto byte = std::uint8_t(message[at++]);
                value |= std::uint64_t(byte & 0x7F) << shift;
                if (!(byte & 0x80)) return value;
            }
            return std::nullopt;
        };
        while (at < message.size()) {
            const auto tag = varint();
            if (!tag) break;
            const auto wire = *tag & 7;
            if (wire == 0) { if (!varint()) break; }
            else if (wire == 1 || wire == 5) { const std::size_t skip = wire == 1 ? 8 : 4; if (message.size() - at < skip) break; at += skip; }
            else if (wire == 2) {
                const auto length = varint();
                if (!length || *length > message.size() - at) break;
                out.push_back({ *tag >> 3, message.subspan(at, std::size_t(*length)) });
                at += std::size_t(*length);
            } else break;
        }
        return out;
    };
    auto text = [](std::span<const std::byte> span) { return std::string(reinterpret_cast<const char*>(span.data()), span.size()); };
    std::vector<ProductInstall> out;
    for (const auto& install : fields(bytes)) {
        if (install.number != 1) continue;
        ProductInstall entry;
        for (const auto& field : fields(install.bytes)) {
            if (field.number == 2) entry.code = text(field.bytes);
            if (field.number == 3)
                for (const auto& setting : fields(field.bytes))
                    if (setting.number == 1) entry.path = text(setting.bytes);
        }
        if (!entry.path.empty()) out.push_back(std::move(entry));
    }
    return out;
}

auto classify(const fs::path& dir, std::string source, const fs::path& virtual_store) -> std::optional<Install> {
    Install install;
    install.dir = dir;
    install.source = std::move(source);
    if (find_file(dir, "D2R.exe") || (find_file(dir, ".build.info") && is_dir(dir / "Data" / "data"))) {
        install.kind = Kind::resurrected;
        return install;
    }
    const auto d2data = find_file(dir, "d2data.mpq");
    if (!d2data || !mpq_magic(*d2data)) return std::nullopt;   // a stale registry path, or not D2
    // UAC VirtualStore: files the game wrote without elevation (a patched
    // Game.exe, patch_d2.mpq) are what it saw; they win over the originals.
    auto pick = [&](std::string_view name) {
        if (!virtual_store.empty())
            if (auto hit = find_file(virtual_store, name)) return hit;
        return find_file(dir, name);
    };
    // ponytail: bin/ is the launcher's dev-only binary import (PLAN.md
    // phase 4 removes it); it goes when that does.
    auto exe = pick("Game.exe");
    if (!exe) exe = find_file(dir / "bin", "Game.exe");
    if (exe) {
        install.game_exe = *exe;
        if (const auto version = file_version(*exe)) {
            const auto [major, minor, build, revision] = *version;
            install.file_version = std::to_string(major) + "." + std::to_string(minor) + "." + std::to_string(build) + "." +
                                   std::to_string(revision);
            install.version = *version == std::array<std::uint16_t, 4>{ 1, 14, 3, 71 } ? Version::v114d
                            : major == 1 && minor == 14                                ? Version::v114_other
                            : major == 1 && minor < 14                                 ? Version::legacy
                                                                                       : Version::unknown;
        }
    }
    // 1.14 linked the D2*.dll into Game.exe; a D2Client.dll means 1.13 or older.
    if (install.version == Version::unknown && install.file_version.empty() && pick("D2Client.dll"))
        install.version = Version::legacy;
    if (const auto patch = pick("patch_d2.mpq")) install.patch_mpq = *patch;
    install.expansion = find_file(dir, "d2exp.mpq").has_value();
    for (const char* name : { "d2char.mpq", "d2sfx.mpq", "d2speech.mpq", "d2music.mpq", "d2video.mpq" })
        if (!find_file(dir, name)) install.missing.emplace_back(name);
    if (install.expansion)
        for (const char* name : { "d2xtalk.mpq", "d2xmusic.mpq", "d2xvideo.mpq" })
            if (!find_file(dir, name)) install.missing.emplace_back(name);
    return install;
}

bool usable(const Install& install) { return install.kind == Kind::classic && install.expansion; }

std::string problem(const Install& install) {
    if (install.kind == Kind::resurrected)
        return "Diablo II: Resurrected stores its data differently; d2d needs classic Diablo II (2000/2001)";
    if (!install.expansion) return "Lord of Destruction needed (no d2exp.mpq)";
    switch (install.version) {
    case Version::v114d: return {};
    case Version::v114_other:
    case Version::legacy: return "version " + install.file_version + ", not 1.14d: add LODPatch_114d.exe";
    case Version::unknown: return install.patch_mpq.empty() ? "version unknown, no patch_d2.mpq: add LODPatch_114d.exe" : std::string{};
    }
    return {};
}

std::string title(const Install& install) {
    if (install.kind == Kind::resurrected) return "Diablo II: Resurrected";
    std::string text = install.expansion ? "Diablo II: Lord of Destruction" : "Diablo II";
    if (install.version == Version::v114d) text += " 1.14d";
    else if (!install.file_version.empty()) text += " " + install.file_version;
    return text;
}

auto wine_prefixes(const fs::path& home, std::string_view wineprefix, bool search_more) -> std::vector<fs::path> {
    std::vector<fs::path> out;
    auto add = [&](const fs::path& prefix) {
        if (prefix.empty() || !is_dir(prefix / "drive_c")) return;
        std::error_code error;
        const auto canonical = fs::weakly_canonical(prefix, error);
        if (std::none_of(out.begin(), out.end(), [&](const fs::path& seen) { return fs::weakly_canonical(seen, error) == canonical; }))
            out.push_back(prefix);
    };
    auto each = [&](const fs::path& parent, const fs::path& tail = {}) {
        for (const auto& entry : entries(parent)) add(tail.empty() ? entry : entry / tail);
    };
    add(utf8_path(wineprefix));
    if (home.empty()) return out;
    add(home / ".wine");
    // Linux. ponytail: Steam libraries outside ~/.steam (libraryfolders.vdf)
    // and Lutris games outside ~/Games (pga.db, sqlite) aren't read.
    each(home / "Games");                                                   // Lutris
    each(home / ".local/share/bottles/bottles");                            // Bottles
    each(home / ".var/app/com.usebottles.bottles/data/bottles/bottles");    // Bottles (Flatpak)
    each(home / ".steam/steam/steamapps/compatdata", "pfx");                // Proton
    each(home / ".local/share/Steam/steamapps/compatdata", "pfx");
    // macOS wrappers.
    each(home / "Library/Application Support/CrossOver/Bottles");           // CrossOver
    each(home / "Applications", "Contents/SharedSupport/prefix");           // Porting Kit
    each(home / "Applications/Wineskin", "Contents/SharedSupport/prefix");  // Wineskin
    // Another app's container: macOS 14+ asks the user first (TCC), so only on request.
    if (search_more) each(home / "Library/Containers/com.isaacmarovitz.Whisky/Bottles");
    return out;
}

auto system_environment(bool search_more) -> Environment {
    Environment env;
    auto var = [](const char* name) -> std::string {
        const char* value = std::getenv(name);
        return value ? value : "";
    };
#if defined(_WIN32)
    // ponytail: getenv is the ANSI code page; a non-ASCII %ProgramFiles%
    // or profile path needs _wgetenv.
    env.home           = var("USERPROFILE");
    env.program_data   = var("ProgramData");
    env.local_app_data = var("LOCALAPPDATA");
    for (const char* name : { "ProgramFiles(x86)", "ProgramFiles" })
        if (const auto dir = var(name);
            !dir.empty() && std::find(env.program_files.begin(), env.program_files.end(), fs::path(dir)) == env.program_files.end())
            env.program_files.emplace_back(dir);
    env.registry = windows_registry;
    env.subkeys  = windows_subkeys;
#else
    env.home = var("HOME");
#  if defined(__APPLE__)
    env.program_data = "/Users/Shared";
    env.app_dirs = { "/Applications" };
    if (!env.home.empty()) env.app_dirs.push_back(env.home / "Applications");
#  endif
    env.wine_prefixes = wine_prefixes(env.home, var("WINEPREFIX"), search_more);
#endif
    return env;
}

auto detect(const Environment& env) -> std::vector<Install> {
    std::vector<Install> found;
    Probe probe{ found };

    // Native Windows: registry, uninstall keys, default folders, product.db.
    auto virtual_store = [&](const fs::path& dir) {
        return env.local_app_data.empty() ? fs::path{} : env.local_app_data / "VirtualStore" / dir.relative_path();
    };
    auto native_add = [&](const fs::path& dir, const std::string& source) { probe.add(dir, source, virtual_store(dir)); };
    const MapPath native = [](std::string_view path) -> std::optional<fs::path> { return utf8_path(path); };
    probe_registry(env.registry, env.subkeys, native, "", native_add);
    for (const auto& program_files : env.program_files) native_add(program_files / "Diablo II", "default folder");
    if (!env.program_data.empty()) probe_product_db(env.program_data / "Battle.net/Agent/product.db", native, "", native_add);
    for (const auto& program_files : env.program_files)
        for (const auto folder : kD2RFolders) {
            std::string relative(folder);
            std::replace(relative.begin(), relative.end(), '\\', '/');
            native_add(program_files / utf8_path(relative), "default folder");
        }

    // macOS: the native 1.14 client's folder (MPQs beside or inside the bundle).
    for (const auto& apps : env.app_dirs) {
        probe.add(apps / "Diablo II", "Applications");
        probe.add(apps / "Diablo II" / "Diablo II.app" / "Contents" / "Resources", "Applications");
    }

    // Wine prefixes: the same registry probes over user.reg (HKCU) and
    // system.reg (HKLM), paths mapped into the prefix.
    for (const auto& prefix : env.wine_prefixes) {
        const std::string label = "Wine " + pretty(prefix, env.home) + ": ";
        const auto user = read_file(prefix / "user.reg", 256u << 20).value_or("");
        const auto system = read_file(prefix / "system.reg", 256u << 20).value_or("");
        auto wow = [](std::string_view key) {   // a 64-bit prefix keeps 32-bit HKLM software here
            return std::string("Software\\Wow6432Node\\") + std::string(key.substr(std::string_view("Software\\").size()));
        };
        const Registry registry = [&](Hive hive, std::string_view key, std::string_view value) -> std::optional<std::string> {
            if (hive == Hive::current_user) return wine_reg_value(user, key, value);
            if (auto hit = wine_reg_value(system, wow(key), value)) return hit;
            return wine_reg_value(system, key, value);
        };
        const Subkeys subkeys = [&](Hive hive, std::string_view key) {
            if (hive == Hive::current_user) return wine_reg_subkeys(user, key);
            auto keys = wine_reg_subkeys(system, wow(key));
            for (auto& sub : wine_reg_subkeys(system, key))
                if (std::none_of(keys.begin(), keys.end(), [&](const std::string& seen) { return iequals(seen, sub); }))
                    keys.push_back(std::move(sub));
            return keys;
        };
        const MapPath map = [&](std::string_view path) { return wine_to_host(prefix, path); };
        auto add = [&](const fs::path& dir, const std::string& source) { probe.add(dir, source); };
        probe_registry(registry, subkeys, map, label, add);
        for (const char* program_files : { R"(C:\Program Files (x86)\)", R"(C:\Program Files\)" }) {
            if (auto dir = map(std::string(program_files) + "Diablo II")) add(*dir, label + "default folder");
            for (const auto folder : kD2RFolders)
                if (auto dir = map(std::string(program_files) + std::string(folder))) add(*dir, label + "default folder");
        }
        if (auto db = map(R"(C:\ProgramData\Battle.net\Agent\product.db)")) probe_product_db(*db, map, label, add);
    }

    // Unique by canonical dir, first probe's label kept.
    std::vector<Install> unique;
    std::vector<std::string> seen;
    for (auto& install : found) {
        std::error_code error;
        auto key = path_utf8(fs::weakly_canonical(install.dir, error));
        std::transform(key.begin(), key.end(), key.begin(), lower);   // Windows and macOS paths ignore case
        if (std::find(seen.begin(), seen.end(), key) != seen.end()) continue;
        seen.push_back(std::move(key));
        unique.push_back(std::move(install));
    }
    auto rank = [](const Install& install) {
        if (install.kind == Kind::resurrected) return 3;
        if (!usable(install)) return 2;
        return install.version == Version::v114d ? 0 : 1;
    };
    std::stable_sort(unique.begin(), unique.end(), [&](const Install& a, const Install& b) { return rank(a) < rank(b); });
    return unique;
}

auto resolve_data_dir(std::string_view cli, std::string_view env, const fs::path& exe_dir, const fs::path& cwd,
                      std::string_view cfg_data) -> DataDir {
    auto has_data = [](const fs::path& dir) { return !dir.empty() && find_file(dir, "d2data.mpq").has_value(); };
    // 1. Given explicitly: used as is, no drop-in check.
    if (!cli.empty() || !env.empty()) {
        const std::string from = cli.empty() ? "D2_MPQ_DIR" : "--data";
        const auto dir = utf8_path(cli.empty() ? env : cli);
        if (has_data(dir)) return { dir, from, {} };
        return { {}, from, from + " " + path_utf8(dir) + ": no d2data.mpq there" };
    }
    // 2. Drop-in: d2data.mpq beside the binary or in the working dir.
    for (const auto& dir : { exe_dir, cwd })
        if (has_data(dir)) return { dir, "drop-in", {} };
    // 3. d2d.cfg (the launcher writes it).
    if (!cfg_data.empty()) {
        const auto dir = utf8_path(cfg_data);
        if (has_data(dir)) return { dir, "d2d.cfg", {} };
        return { {}, "d2d.cfg", "d2d.cfg data = " + path_utf8(dir) + ": no d2data.mpq there (pick the install again in the launcher, or fix the line)" };
    }
    return { {}, {}, "no Diablo II data: pass --data <dir with d2data.mpq>, set D2_MPQ_DIR, put d2data.mpq beside d2d, "
                     "or set data = <dir> in d2d.cfg (the launcher writes it)" };
}

}  // namespace d2d::install
