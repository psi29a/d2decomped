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

namespace d2d::app {

struct CharacterStore {
    std::filesystem::path dir;
    const d2d::d2s::ItemTables* tables = nullptr;

    [[nodiscard]] std::filesystem::path path(const std::string& name) const { return dir / (name + ".d2s"); }

    // Writes the character; "" when it's done, else why not (nothing written).
    std::string save(const d2d::d2s::Header& h, const d2d::d2s::Stats& st, const std::vector<d2d::d2s::Item>& items,
                     const std::vector<d2d::d2s::Item>* corpse = nullptr) const {
        namespace fs = std::filesystem;
        if (!tables) return "no item tables";
        if (h.name.empty() || h.name.find_first_of("/\\.") != std::string::npos) return "bad name";
        const auto file = path(h.name);
        std::vector<char> raw;
        if (std::ifstream in(file, std::ios::binary); in) raw.assign(std::istreambuf_iterator<char>(in), {});
        std::vector<std::byte> out;
        try {
            out = d2d::d2s::write_save(std::as_bytes(std::span(raw)), h, st, items, *tables, corpse);
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

// A character made in d2d (the char-create screen): CharStats.txt's start
// (stats, life = vit + hpadd, mana = int, stamina; the start skill at 1;
// its items: rarm / larm in the hands, potions in the belt, the rest in
// the inventory), level 1, normal difficulty, no hotkeys.
// ponytail: stacks (javelins) roll their quantity as a drop does.
struct NewCharacter { d2d::d2s::Header header; d2d::d2s::Stats stats; std::vector<d2d::d2s::Item> items; };
inline NewCharacter new_character(const Scene& s, int cls, const std::string& name, bool hardcore, bool expansion, d2d::rules::Rng& rng) {
    using namespace d2d::d2s;
    NewCharacter n;
    const auto c = std::size_t(std::clamp(cls, 0, 6));
    const auto& cs = s.class_start[c];
    auto& h = n.header;
    h.version = kMaxVersion;
    h.name = name;
    h.cls = std::uint8_t(c);
    h.level = 1;
    h.status = std::uint8_t((hardcore ? 0x04 : 0) | (expansion ? 0x20 : 0));
    h.hotkeys.fill(0xffff);
    h.set_look(s.starting_gear[c]);
    h.difficulty = { 0x80, 0, 0 };
    auto& st = n.stats;
    st.v[kStr] = cs.str; st.v[kDex] = cs.dex; st.v[kEne] = cs.ene; st.v[kVit] = cs.vit;
    st.v[kLife] = st.v[kMaxLife] = std::int64_t(cs.vit + cs.hpadd) << 8;
    st.v[kMana] = st.v[kMaxMana] = std::int64_t(cs.ene) << 8;
    st.v[kStamina] = st.v[kMaxStamina] = std::int64_t(cs.stamina) << 8;
    st.v[kLevel] = 1;
    if (!cs.start_skill.empty()) {
        const auto& ids = s.skills.class_ids[c];
        for (std::size_t i = 0; i < ids.size() && i < st.skills.size(); ++i)
            if (const auto* k = s.skills.get(ids[i]); k) {
                auto lower = [](std::string v) { for (auto& ch : v) ch = char(std::tolower(static_cast<unsigned char>(ch))); return v; };
                if (lower(k->name) == lower(cs.start_skill)) st.skills[i] = 1;
            }
    }
    int belt = 0, inv = 0;
    for (const auto& e : cs.items)
        for (int k = 0; k < std::max(e.count, 1); ++k) {
            auto it = d2d::rules::generate_item(s.rules, e.code, 1, 2, rng);
            if (e.loc == "rarm" || e.loc == "larm") { it.location = 1; it.slot = e.loc == "rarm" ? 4 : 5; }
            else if (e.code.starts_with("hp") || e.code.starts_with("mp")) { it.location = 2; it.column = belt++; }
            else { it.location = 0; it.panel = 1; it.column = inv++; }
            n.items.push_back(std::move(it));
        }
    return n;
}

}  // namespace d2d::app
