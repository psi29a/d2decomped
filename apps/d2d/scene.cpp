// Definitions for scene.hpp: the Scene's sprites loaded on first use.
#include "scene.hpp"
#include "common.hpp"
#include "load.hpp"

namespace d2d::client {

const std::uint8_t* Scene::monster_map(const Npc& npc) const {
    if (npc.colour >= 8 && npc.colour < 38) return rand_transforms.size() >= std::size_t(npc.colour - 7) * 256 ? rand_transforms.data() + (npc.colour - 8) * 256 : nullptr;
    if (npc.colour < 2 || npc.colour > 7 || npc.code.empty()) return nullptr;
    auto [found, fresh] = palshifts.try_emplace(npc.code);
    if (fresh)
        if (auto bytes = mpqs.try_read(R"(data\global\monsters\)" + npc.code + R"(\COF\palshift.dat)"))
            found->second.assign(reinterpret_cast<const std::uint8_t*>(bytes->data()), reinterpret_cast<const std::uint8_t*>(bytes->data()) + bytes->size());
    return found->second.size() >= std::size_t(npc.colour + 1) * 256 ? found->second.data() + npc.colour * 256 : nullptr;
}

const d2d::dcc::Sprite* Scene::overlay_sprite(const OverlayInfo& overlay) const {
    auto [found, fresh] = overlay_sprites.try_emplace(&overlay);
    if (fresh)
        if (auto bytes = mpqs.try_read(R"(data\global\overlays\)" + overlay.file + ".dcc"))
            try { found->second = d2d::dcc::Sprite(*bytes); } catch (const std::exception& error) { d2d::log::warn("overlay {}: {}", overlay.file, error.what()); }
    return found->second.directions() && found->second.frames_per_direction() ? &found->second : nullptr;
}

const Scene::PlayerAnim& Scene::composite(int d2s_class, int mode, const Appearance& gfx) const {
    std::array<std::uint8_t, 34> key{ std::uint8_t(d2s_class), std::uint8_t(mode) };
    std::copy(gfx.begin(), gfx.end(), key.begin() + 2);
    auto found = composites.find(key);
    if (found == composites.end()) {
        found = composites.emplace(key, load_composite(mpqs, comp, colormaps, d2s_class, mode, gfx)).first;
        if (const auto anim_info = anim_data.find(found->second.name); anim_info != anim_data.end())
            std::tie(found->second.speed, found->second.frames, found->second.action) = std::tuple{ anim_info->second.speed, anim_info->second.frames, anim_info->second.action };
    }
    return found->second;
}

const Scene::PlayerAnim& Scene::npc_anim(const Npc& npc, std::string_view mode) const {
    const std::string mode_text(mode);
    auto key = npc.root + "/" + npc.code + "/" + mode_text + "/" + npc.base_w;
    for (const auto& component : npc.comp) key += "/" + component;
    auto found = npc_anims.find(key);
    if (found == npc_anims.end()) {
        found = npc_anims.emplace(key, load_npc_composite(mpqs, npc, mode_text)).first;
        if (const auto anim_info = anim_data.find(found->second.name); anim_info != anim_data.end())
            std::tie(found->second.speed, found->second.frames, found->second.action) = std::tuple{ anim_info->second.speed, anim_info->second.frames, anim_info->second.action };
    }
    return found->second;
}

const d2d::dc6::Sprite* Scene::flippy(const std::string& code) const {
    const auto info = rules.item_info.find(code);
    if (info == rules.item_info.end() || info->second.flippy.empty()) return nullptr;
    auto [found, fresh] = flippy_sprites.try_emplace(info->second.flippy);
    if (fresh)
        if (auto bytes = mpqs.try_read(R"(data\global\items\)" + info->second.flippy + ".dc6"))
            found->second = d2d::dc6::Sprite(*bytes);
    return found->second ? &*found->second : nullptr;
}

const d2d::dc6::Sprite* Scene::item_sprite(const d2d::d2s::Item& item) const {
    const auto info = rules.item_info.find(item.code);
    if (info == rules.item_info.end()) return nullptr;
    auto pick = [](const std::vector<std::string>& names, int index) {
        return index >= 0 && std::size_t(index) < names.size() ? names[std::size_t(index)] : std::string{};
    };
    std::string file = item.quality == 7 ? pick(unique_inv, item.unique_id)
                     : item.quality == 5 ? pick(set_inv, item.set_id) : std::string{};
    if (file.empty() && item.picture >= 0 && item.picture < 6)
        if (const auto found = type_invgfx.find(info->second.type); found != type_invgfx.end())
            file = found->second[std::size_t(item.picture)];
    if (file.empty()) file = info->second.invfile;
    if (file.empty()) return nullptr;
    auto [found, fresh] = item_sprites.try_emplace(file);
    if (fresh)
        if (auto bytes = mpqs.try_read(R"(data\global\items\)" + file + ".dc6"))
            found->second = d2d::dc6::Sprite(*bytes);
    return found->second ? &*found->second : nullptr;
}

}  // namespace d2d::client
