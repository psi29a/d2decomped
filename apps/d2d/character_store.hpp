// The CharacterStore (docs/design/multiplayer.md, rule 7): where the game
// server loads characters from and writes them to, kept apart from the live
// game. Locally, the .d2s files in the save directory (d2s_write.hpp).
// A save is written only when the new bytes parse back as the same
// character; into a temp file renamed over the old one; and the first time
// d2d writes over a save it didn't make, the original is kept as .d2s.bak.
#pragma once

#include <d2s_write.hpp>

#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace {

struct CharacterStore {
    std::filesystem::path dir;
    const d2d::d2s::ItemTables* tables = nullptr;

    [[nodiscard]] std::filesystem::path path(const std::string& name) const { return dir / (name + ".d2s"); }

    // Writes the character; "" when it's done, else why not (nothing written).
    std::string save(const d2d::d2s::Header& h, const d2d::d2s::Stats& st, const std::vector<d2d::d2s::Item>& items) const {
        namespace fs = std::filesystem;
        if (!tables) return "no item tables";
        if (h.name.empty() || h.name.find_first_of("/\\.") != std::string::npos) return "bad name";
        const auto file = path(h.name);
        std::vector<char> raw;
        if (std::ifstream in(file, std::ios::binary); in) raw.assign(std::istreambuf_iterator<char>(in), {});
        std::vector<std::byte> out;
        try {
            out = d2d::d2s::write_save(std::as_bytes(std::span(raw)), h, st, items, *tables);
            const auto back = d2d::d2s::parse_header(out);
            const auto bst = d2d::d2s::parse_stats(out, *tables);
            const auto bitems = d2d::d2s::parse_items(out, *tables);
            if (back.name != h.name || bst.get(d2d::d2s::kLevel) != st.get(d2d::d2s::kLevel) || bitems.size() != items.size())
                return "the written save didn't read back";
        } catch (const std::exception& e) {
            return std::string("write failed: ") + e.what();
        }
        std::error_code ec;
        fs::create_directories(dir, ec);
        if (const auto bak = fs::path(file).replace_extension(".d2s.bak"); !raw.empty() && !fs::exists(bak))
            fs::copy_file(file, bak, ec);
        const auto tmp = fs::path(file).replace_extension(".d2s.tmp");
        {
            std::ofstream o(tmp, std::ios::binary | std::ios::trunc);
            o.write(reinterpret_cast<const char*>(out.data()), std::streamsize(out.size()));
            if (!o) return "couldn't write " + tmp.string();
        }
        fs::rename(tmp, file, ec);
        return ec ? "couldn't replace " + file.string() + ": " + ec.message() : std::string{};
    }
};

}  // namespace
