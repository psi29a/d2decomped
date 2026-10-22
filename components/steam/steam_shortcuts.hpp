// SPDX-License-Identifier: GPL-3.0-or-later
// Steam's non-Steam-game list: userdata/<account>/config/shortcuts.vdf.
//
// Binary KeyValues: each node is a type byte, a NUL-terminated key, then
// its value. 0x00 = map (nodes until 0x08), 0x01 = string (NUL-ended),
// 0x02 = int32, 0x03 = float, 0x07 = uint64; all little-endian. The file
// is one map "shortcuts" whose children "0", "1", ... are the games.
// Unknown scalar values are kept as raw bytes, so a round trip is exact.
//
// Library art sits beside it in config/grid/, named by the shortcut's
// appid: <appid>p.png (portrait), <appid>.png (wide), <appid>_hero.png,
// <appid>_logo.png, <appid>_icon.png.
#pragma once

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <ios>
#include <iterator>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace d2d::steam {

namespace fs = std::filesystem;

enum class Type : std::uint8_t { Map = 0, String = 1, Int = 2, Float = 3, Uint64 = 7 };
constexpr char kEnd = 0x08;

struct Node {
    Type type = Type::Map;
    std::string key;
    std::string value;            // string text, or a scalar's raw bytes
    std::vector<Node> children;   // Map only
};

namespace detail {

inline std::size_t scalar_size(Type type) {
    switch (type) {
    case Type::Int: case Type::Float: return 4;
    case Type::Uint64: return 8;
    default: throw std::runtime_error("shortcuts.vdf: unknown value type");
    }
}

inline std::string read_cstring(std::string_view data, std::size_t& pos) {
    const std::size_t nul = data.find('\0', pos);
    if (nul == std::string_view::npos) throw std::runtime_error("shortcuts.vdf: unterminated string");
    std::string text(data.substr(pos, nul - pos));
    pos = nul + 1;
    return text;
}

// Nodes until 0x08; `top` also accepts the end of the data.
inline std::vector<Node> read_map(std::string_view data, std::size_t& pos, bool top) {
    std::vector<Node> nodes;
    while (true) {
        if (pos >= data.size()) {
            if (top) return nodes;
            throw std::runtime_error("shortcuts.vdf: truncated");
        }
        const char type_byte = data[pos++];
        if (type_byte == kEnd) return nodes;
        Node node;
        node.type = Type(std::uint8_t(type_byte));
        node.key = read_cstring(data, pos);
        if (node.type == Type::Map) {
            node.children = read_map(data, pos, false);
        } else if (node.type == Type::String) {
            node.value = read_cstring(data, pos);
        } else {
            const std::size_t size = scalar_size(node.type);
            if (pos + size > data.size()) throw std::runtime_error("shortcuts.vdf: truncated");
            node.value = std::string(data.substr(pos, size));
            pos += size;
        }
        nodes.push_back(std::move(node));
    }
}

inline void write_map(std::string& out, const std::vector<Node>& nodes) {
    for (const Node& node : nodes) {
        out += char(node.type);
        out += node.key;
        out += '\0';
        if (node.type == Type::Map) write_map(out, node.children);
        else out += node.value;
        if (node.type == Type::String) out += '\0';
    }
    out += kEnd;
}

inline bool same_key(std::string_view lhs, std::string_view rhs) {
    return std::ranges::equal(lhs, rhs, [](char left, char right) {
        return std::tolower(static_cast<unsigned char>(left)) == std::tolower(static_cast<unsigned char>(right));
    });
}

// zlib's CRC-32, the one Steam's legacy shortcut ids use.
inline std::uint32_t crc32(std::string_view text) {
    std::uint32_t crc = 0xffffffffU;
    for (const char byte : text) {
        crc ^= static_cast<unsigned char>(byte);
        for (int bit = 0; bit < 8; ++bit) crc = (crc >> 1) ^ (0xedb88320U & (0U - (crc & 1U)));
    }
    return ~crc;
}

}  // namespace detail

inline std::vector<Node> parse(std::string_view data) {
    std::size_t pos = 0;
    return detail::read_map(data, pos, true);
}

inline std::string serialize(const std::vector<Node>& nodes) {
    std::string out;
    detail::write_map(out, nodes);
    return out;
}

inline Node* find(std::vector<Node>& nodes, std::string_view key) {
    const auto found = std::ranges::find_if(nodes, [&](const Node& node) { return detail::same_key(node.key, key); });
    return found == nodes.end() ? nullptr : &*found;
}

inline std::string text(std::vector<Node>& nodes, std::string_view key) {
    const Node* node = find(nodes, key);
    return node && node->type == Type::String ? node->value : std::string{};
}

inline void set_text(std::vector<Node>& nodes, std::string_view key, std::string value) {
    if (Node* node = find(nodes, key)) {
        node->type = Type::String;
        node->value = std::move(value);
    } else {
        nodes.push_back({Type::String, std::string(key), std::move(value), {}});
    }
}

inline std::uint32_t number(std::vector<Node>& nodes, std::string_view key) {
    const Node* node = find(nodes, key);
    if (!node || node->type != Type::Int || node->value.size() != 4) return 0;
    std::uint32_t result = 0;
    for (int i = 3; i >= 0; --i) result = (result << 8) | static_cast<unsigned char>(node->value[std::size_t(i)]);
    return result;
}

inline void set_number(std::vector<Node>& nodes, std::string_view key, std::uint32_t value) {
    std::string bytes(4, '\0');
    for (std::size_t i = 0; i < 4; ++i) bytes[i] = char((value >> (8 * i)) & 0xffU);
    if (Node* node = find(nodes, key)) {
        node->type = Type::Int;
        node->value = std::move(bytes);
    } else {
        nodes.push_back({Type::Int, std::string(key), std::move(bytes), {}});
    }
}

struct Shortcut {
    std::string name;             // AppName
    std::string exe;              // quoted, as Steam writes it
    std::string start_dir;        // quoted
    std::string launch_options;
    std::string flatpak_id;       // FlatpakAppID; empty outside Flatpak
};

// Adds `shortcut`, or updates the entry that already launches it (same
// Flatpak id in LaunchOptions, same Exe, or same AppName: a hand-made
// "Add a Non-Steam Game" entry). Returns that entry; its appid names its art.
inline Node& upsert(std::vector<Node>& file, const Shortcut& shortcut) {
    Node* list = find(file, "shortcuts");
    if (!list) {
        file.push_back({Type::Map, "shortcuts", {}, {}});
        list = &file.back();
    }
    Node* entry = nullptr;
    int next_index = 0;
    for (Node& candidate : list->children) {
        next_index = std::max(next_index, std::atoi(candidate.key.c_str()) + 1);
        if (entry || candidate.type != Type::Map) continue;
        auto& fields = candidate.children;
        const bool same_flatpak = !shortcut.flatpak_id.empty()
            && text(fields, "LaunchOptions").find(shortcut.flatpak_id) != std::string::npos;
        if (same_flatpak || text(fields, "Exe") == shortcut.exe || text(fields, "AppName") == shortcut.name)
            entry = &candidate;
    }
    if (!entry) {
        Node fresh{Type::Map, std::to_string(next_index), {}, {}};
        auto& fields = fresh.children;
        set_number(fields, "appid", detail::crc32(shortcut.exe + shortcut.name) | 0x80000000U);
        set_text(fields, "AppName", shortcut.name);
        set_text(fields, "Exe", {});
        set_text(fields, "StartDir", {});
        set_text(fields, "icon", {});
        set_text(fields, "ShortcutPath", {});
        set_text(fields, "LaunchOptions", {});
        set_number(fields, "IsHidden", 0);
        set_number(fields, "AllowDesktopConfig", 1);
        set_number(fields, "AllowOverlay", 1);
        set_number(fields, "OpenVR", 0);
        set_number(fields, "Devkit", 0);
        set_text(fields, "DevkitGameID", {});
        set_number(fields, "DevkitOverrideAppID", 0);
        set_number(fields, "LastPlayTime", 0);
        set_text(fields, "FlatpakAppID", {});
        fields.push_back({Type::Map, "tags", {}, {}});
        list->children.push_back(std::move(fresh));
        entry = &list->children.back();
    }
    auto& fields = entry->children;
    if (number(fields, "appid") == 0)   // very old entries have none
        set_number(fields, "appid", detail::crc32(shortcut.exe + shortcut.name) | 0x80000000U);
    set_text(fields, "Exe", shortcut.exe);
    set_text(fields, "StartDir", shortcut.start_dir);
    set_text(fields, "LaunchOptions", shortcut.launch_options);
    set_text(fields, "FlatpakAppID", shortcut.flatpak_id);
    return *entry;
}

// Every Steam account's config dir under `home` (native Steam; the Deck's
// is ~/.local/share/Steam, ~/.steam/steam links to it on most distros).
inline std::vector<fs::path> account_configs(const fs::path& home) {
    std::vector<fs::path> configs;
    for (const char* root : {".local/share/Steam/userdata", ".steam/steam/userdata"}) {
        std::error_code error;
        for (const auto& account : fs::directory_iterator(home / root, error)) {
            const std::string name = account.path().filename().string();
            if (name == "0" || name.empty() || !std::ranges::all_of(name, [](char digit) { return std::isdigit(static_cast<unsigned char>(digit)) != 0; }))
                continue;
            const fs::path config = fs::weakly_canonical(account.path() / "config", error);
            if (!error && fs::is_directory(config) && std::ranges::find(configs, config) == configs.end())
                configs.push_back(config);
        }
    }
    return configs;
}

// Reads `config`/shortcuts.vdf (missing = empty), upserts with the icon at
// grid/<appid>_icon.png, writes it back after keeping the old one as
// shortcuts.vdf.d2d-backup. Returns the appid. Throws on a file it can't
// parse, leaving it untouched.
inline std::uint32_t add_to_account(const fs::path& config, const Shortcut& shortcut) {
    const fs::path vdf = config / "shortcuts.vdf";
    std::string data;
    std::error_code exists_error;
    const bool exists = fs::exists(vdf, exists_error);
    if (exists_error) throw std::runtime_error("can't look at " + vdf.string() + ": " + exists_error.message());
    if (exists) {   // one that's there but can't be read whole is left alone, never replaced
        std::ifstream input{vdf, std::ios::binary};
        if (!input) throw std::runtime_error("can't read " + vdf.string());
        data.assign(std::istreambuf_iterator<char>(input), {});
        if (input.bad()) throw std::runtime_error("can't read " + vdf.string());
    }
    std::vector<Node> file = parse(data);
    auto& fields = upsert(file, shortcut).children;
    const std::uint32_t appid = number(fields, "appid");
    set_text(fields, "icon", (config / "grid" / (std::to_string(appid) + "_icon.png")).string());
    if (exists) fs::copy_file(vdf, config / "shortcuts.vdf.d2d-backup", fs::copy_options::overwrite_existing);
    const fs::path temp = config / "shortcuts.vdf.d2d-new";
    std::ofstream out(temp, std::ios::binary | std::ios::trunc);
    const std::string bytes = serialize(file);
    out.write(bytes.data(), std::streamsize(bytes.size()));
    out.close();   // flushed and closed before it can replace the real one
    if (!out) {
        std::error_code ignored;
        fs::remove(temp, ignored);
        throw std::runtime_error("can't write " + temp.string());
    }
    fs::rename(temp, vdf);
    return appid;
}

}  // namespace d2d::steam
