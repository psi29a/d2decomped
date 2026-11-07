// SPDX-License-Identifier: GPL-3.0-or-later
// The Scene's rendering support: overlay sprites and drawn-sprite tracking.
#include "scene.hpp"

#include <dc6.hpp>
#include <dcc.hpp>
#include <log.hpp>

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <format>
#include <fstream>
#include <ios>
#include <ostream>
#include <span>
#include <string>
#include <string_view>
#include <unordered_set>
#include <utility>
#include <vector>

namespace d2d::client {

// Present a DC6's frames through the DCC interface used by the renderer.
d2d::dcc::Sprite dc6_as_dcc(std::span<const std::byte> bytes) {
    const d2d::dc6::Sprite cels(bytes);
    std::vector<d2d::dcc::Frame> frames;
    for (const auto& cel : cels.frames()) {
        const int width = int(cel.width), height = int(cel.height), top = cel.offset_y - height + 1;
        frames.push_back({ width, height, cel.offset_x, cel.offset_y, cel.offset_x, top, cel.offset_x + width, cel.offset_y + 1, cel.pixels });
    }
    return d2d::dcc::Sprite::from_frames(std::uint8_t(cels.directions()), std::int32_t(cels.frames_per_direction()), std::move(frames));
}

// Load each overlay once, using the real asset loader for either sprite format.
const d2d::dcc::Sprite* Scene::overlay_sprite(const OverlayInfo& overlay) const {
    auto [found, fresh] = overlay_sprites.try_emplace(&overlay);
    if (fresh) {
        // Overlay row 0x7a (mephisto) is a DC6 (FUN_005fe610); the rest DCCs.
        const std::string path = R"(data\global\overlays\)" + overlay.file;
        try {
            if (auto bytes = mpqs.try_read(path + ".dcc")) found->second = d2d::dcc::Sprite(*bytes);
            else if (auto cels = mpqs.try_read(path + ".dc6")) found->second = dc6_as_dcc(*cels);
        } catch (const std::exception& error) { d2d::log::warn("overlay {}: {}", overlay.file, error.what()); }
    }
    const bool usable = found->second.directions() && found->second.frames_per_direction();
    return usable ? &found->second : nullptr;
}

// Record each sprite once when the optional animation audit is enabled.
void Scene::note_drawn(std::string_view kind, std::string_view name) {
    static std::unordered_set<std::string> drawn;
    static std::ofstream out = [] {
        const char* path = std::getenv("D2D_ANIM_LOG");
        return path && *path ? std::ofstream(path, std::ios::app) : std::ofstream{};
    }();
    if (!out.is_open() || name.empty()) return;
    if (auto line = std::format("{} {}", kind, name); drawn.insert(line).second) out << line << '\n' << std::flush;
}

}  // namespace d2d::client
