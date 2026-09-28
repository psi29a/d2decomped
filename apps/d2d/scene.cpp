// Definitions for scene.hpp: the Scene's sprites loaded on first use.
#include "scene.hpp"
#include "common.hpp"
#include "load.hpp"

namespace d2d::client {

const std::uint8_t* Scene::monster_map(const Npc& n) const {
    if (n.colour >= 8 && n.colour < 38) return rand_transforms.size() >= std::size_t(n.colour - 7) * 256 ? rand_transforms.data() + (n.colour - 8) * 256 : nullptr;
    if (n.colour < 2 || n.colour > 7 || n.code.empty()) return nullptr;
    auto [it, fresh] = palshifts.try_emplace(n.code);
    if (fresh)
        if (auto b = mpqs.try_read(R"(data\global\monsters\)" + n.code + R"(\COF\palshift.dat)"))
            it->second.assign(reinterpret_cast<const std::uint8_t*>(b->data()), reinterpret_cast<const std::uint8_t*>(b->data()) + b->size());
    return it->second.size() >= std::size_t(n.colour + 1) * 256 ? it->second.data() + n.colour * 256 : nullptr;
}

const d2d::dcc::Sprite* Scene::overlay_sprite(const OverlayInfo& o) const {
    auto [it, fresh] = overlay_sprites.try_emplace(&o);
    if (fresh)
        if (auto b = mpqs.try_read(R"(data\global\overlays\)" + o.file + ".dcc"))
            try { it->second = d2d::dcc::Sprite(*b); } catch (const std::exception& e) { d2d::log::warn("overlay {}: {}", o.file, e.what()); }
    return it->second.directions() && it->second.frames_per_direction() ? &it->second : nullptr;
}

const Scene::PlayerAnim& Scene::composite(int d2s_class, int mode, const Appearance& gfx) const {
    std::array<std::uint8_t, 34> key{ std::uint8_t(d2s_class), std::uint8_t(mode) };
    std::copy(gfx.begin(), gfx.end(), key.begin() + 2);
    auto it = composites.find(key);
    if (it == composites.end()) {
        it = composites.emplace(key, load_composite(mpqs, comp, colormaps, d2s_class, mode, gfx)).first;
        if (const auto a = anim_data.find(it->second.name); a != anim_data.end())
            std::tie(it->second.speed, it->second.frames, it->second.action) = std::tuple{ a->second.speed, a->second.frames, a->second.action };
    }
    return it->second;
}

const Scene::PlayerAnim& Scene::npc_anim(const Npc& n, std::string_view mode) const {
    const std::string m(mode);
    auto key = n.root + "/" + n.code + "/" + m + "/" + n.base_w;
    for (const auto& c : n.comp) key += "/" + c;
    auto it = npc_anims.find(key);
    if (it == npc_anims.end()) {
        it = npc_anims.emplace(key, load_npc_composite(mpqs, n, m)).first;
        if (const auto a = anim_data.find(it->second.name); a != anim_data.end())
            std::tie(it->second.speed, it->second.frames, it->second.action) = std::tuple{ a->second.speed, a->second.frames, a->second.action };
    }
    return it->second;
}

const d2d::dc6::Sprite* Scene::flippy(const std::string& code) const {
    const auto info = rules.item_info.find(code);
    if (info == rules.item_info.end() || info->second.flippy.empty()) return nullptr;
    auto [it, fresh] = flippy_sprites.try_emplace(info->second.flippy);
    if (fresh)
        if (auto b = mpqs.try_read(R"(data\global\items\)" + info->second.flippy + ".dc6"))
            it->second = d2d::dc6::Sprite(*b);
    return it->second ? &*it->second : nullptr;
}

const d2d::dc6::Sprite* Scene::item_sprite(const d2d::d2s::Item& item) const {
    const auto info = rules.item_info.find(item.code);
    if (info == rules.item_info.end()) return nullptr;
    auto pick = [](const std::vector<std::string>& v, int i) {
        return i >= 0 && std::size_t(i) < v.size() ? v[std::size_t(i)] : std::string{};
    };
    std::string file = item.quality == 7 ? pick(unique_inv, item.unique_id)
                     : item.quality == 5 ? pick(set_inv, item.set_id) : std::string{};
    if (file.empty() && item.picture >= 0 && item.picture < 6)
        if (const auto g = type_invgfx.find(info->second.type); g != type_invgfx.end())
            file = g->second[std::size_t(item.picture)];
    if (file.empty()) file = info->second.invfile;
    if (file.empty()) return nullptr;
    auto [it, fresh] = item_sprites.try_emplace(file);
    if (fresh)
        if (auto b = mpqs.try_read(R"(data\global\items\)" + file + ".dc6"))
            it->second = d2d::dc6::Sprite(*b);
    return it->second ? &*it->second : nullptr;
}

}  // namespace d2d::client
