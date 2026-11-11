// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: dcc font game
// test-sources: apps/d2d/common.cpp apps/d2d/items.cpp apps/d2d/skilltree.cpp
// test-includes: apps/d2d
// FUN_004abf60 icon colour (5 grey, 3 hover, else the act palette), FontFormal10
// and a 4 px left shift from level 10, and blue/red when items change the level.
#include "common.hpp"
#include "scene.hpp"
#include "skilltree.hpp"

#include <d2s_items.hpp>
#include <dc6.hpp>
#include <font.hpp>
#include <palette.hpp>
#include <skills.hpp>

#include <algorithm>
#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <exception>
#include <initializer_list>
#include <vector>

namespace {

constexpr int kSkillId = 36;
constexpr std::uint8_t kIconIndex = 1;
constexpr std::uint8_t kFont16Index = 7;
constexpr std::uint8_t kFormalIndex = 8;

std::vector<std::byte> glyph_sheet(std::uint8_t index) {
    std::vector<std::byte> sprite(28 + 32 + 3);
    auto write32 = [&](std::size_t offset, std::uint32_t value) {
        for (unsigned shift = 0; shift < 4; ++shift)
            sprite[offset + shift] = std::byte((value >> (8 * shift)) & 255);
    };
    write32(0, 6);
    write32(16, 1);
    write32(20, 1);
    write32(24, 28);
    write32(32, 1);
    write32(36, 1);
    write32(56, 3);
    sprite[60] = std::byte{1};
    sprite[61] = std::byte{index};
    sprite[62] = std::byte{0x80};
    return sprite;
}

d2d::font::Font digit_font(std::uint8_t index) {
    std::vector<std::byte> metrics(12 + 10 * 14);
    metrics[0] = std::byte{'W'};
    metrics[1] = std::byte{'o'};
    metrics[2] = std::byte{'o'};
    metrics[3] = std::byte{'!'};
    metrics[4] = std::byte{1};
    for (int digit = 0; digit < 10; ++digit) {
        const std::size_t offset = 12 + std::size_t(digit) * 14;
        metrics[offset] = std::byte(std::uint8_t('0' + digit));
        metrics[offset + 3] = std::byte{1};
        metrics[offset + 4] = std::byte{1};
    }
    return d2d::font::Font(metrics, d2d::dc6::Sprite(glyph_sheet(index)));
}

struct Swatch {
    std::uint8_t index;
    std::uint8_t red;
    std::uint8_t green;
    std::uint8_t blue;
};

d2d::palette::Palette paint(std::initializer_list<Swatch> swatches) {
    std::array<d2d::palette::Rgba, 256> entries{};
    for (const Swatch& swatch : swatches) entries[swatch.index] = { swatch.red, swatch.green, swatch.blue, 255 };
    return d2d::palette::Palette(entries);
}

struct Sample {
    std::uint8_t red, green, blue, alpha;
};

Sample pixel_at(const std::vector<std::uint8_t>& framebuffer, int column, int row) {
    const std::size_t offset = (std::size_t(row) * d2d::client::kScreenWidth + std::size_t(column)) * 4;
    return { framebuffer[offset], framebuffer[offset + 1], framebuffer[offset + 2], framebuffer[offset + 3] };
}

void expect_colour(const std::vector<std::uint8_t>& framebuffer, int column, int row, std::uint8_t red, std::uint8_t green, std::uint8_t blue) {
    const Sample pixel = pixel_at(framebuffer, column, row);
    assert(pixel.red == red && pixel.green == green && pixel.blue == blue && pixel.alpha == 255);
}

void expect_clear(const std::vector<std::uint8_t>& framebuffer, int column, int row) {
    assert(pixel_at(framebuffer, column, row).alpha == 0);
}

}  // namespace

int main() try {
    d2d::client::Scene scene;
    scene.act_pal = paint({
        { kIconIndex, 0, 200, 0 },
        { kFont16Index, 200, 200, 0 },
        { kFormalIndex, 200, 100, 0 },
    });
    scene.act_text_blue = paint({
        { kIconIndex, 0, 80, 220 },
        { kFont16Index, 40, 40, 220 },
        { kFormalIndex, 20, 20, 180 },
    });
    scene.act_text_grey = paint({ { kIconIndex, 70, 70, 70 } });
    scene.act_text_red = paint({
        { kFont16Index, 220, 20, 20 },
        { kFormalIndex, 180, 20, 60 },
    });
    scene.font = digit_font(kFont16Index);
    scene.font_formal10 = digit_font(kFormalIndex);
    scene.skill_icons[0] = d2d::dc6::Sprite(glyph_sheet(kIconIndex));
    scene.rules.class_skills[0].push_back({ "sample", 1, 1, 1, 0, 1, 20, { -1, -1, -1 } });
    scene.skills.class_ids[0] = { kSkillId };

    const auto [icon_left, icon_bottom] = d2d::client::skill_icon_at(scene.rules.class_skills[0][0]);
    const int level_x = icon_left + 48;
    const int formal_x = level_x - 4;
    const int level_y = icon_bottom + 12;
    std::vector<std::uint8_t> framebuffer(std::size_t(d2d::client::kScreenWidth) * d2d::client::kScreenHeight * 4);
    std::array<std::uint8_t, 30> levels{};
    d2d::d2s::Stats stats;
    stats.values[d2d::d2s::kLevel] = 20;
    stats.values[d2d::d2s::kSkillPts] = 1;

    auto draw = [&](int mouse_x, int mouse_y, const d2d::rules::CalcEnv& env) {
        std::fill(framebuffer.begin(), framebuffer.end(), 0);
        d2d::client::draw_skill_tree(framebuffer, scene, 0, 1, levels, stats, env, -1, mouse_x, mouse_y);
    };
    auto with_shown = [](int shown) {
        d2d::rules::CalcEnv env;
        env.level = [=](int id) { return id == kSkillId ? shown : 0; };
        return env;
    };

    draw(-1, -1, {});
    expect_colour(framebuffer, icon_left, icon_bottom, 0, 200, 0);

    scene.rules.class_skills[0][0].req_level = 99;
    draw(-1, -1, {});
    expect_colour(framebuffer, icon_left, icon_bottom, 70, 70, 70);

    draw(icon_left + 8, icon_bottom - 8, {});
    expect_colour(framebuffer, icon_left, icon_bottom, 0, 80, 220);

    scene.rules.class_skills[0][0].req_level = 1;
    stats.values[d2d::d2s::kSkillPts] = 0;
    draw(-1, -1, {});
    expect_colour(framebuffer, icon_left, icon_bottom, 70, 70, 70);

    levels[0] = 1;
    draw(-1, -1, {});
    expect_colour(framebuffer, icon_left, icon_bottom, 0, 200, 0);
    expect_colour(framebuffer, level_x, level_y, 200, 200, 0);
    expect_clear(framebuffer, formal_x, level_y);

    levels[0] = 9;
    stats.values[d2d::d2s::kSkillPts] = 1;
    draw(-1, -1, {});
    expect_colour(framebuffer, level_x, level_y, 200, 200, 0);
    expect_clear(framebuffer, formal_x, level_y);

    levels[0] = 10;
    draw(-1, -1, {});
    expect_colour(framebuffer, formal_x, level_y, 200, 100, 0);
    expect_clear(framebuffer, level_x, level_y);

    levels[0] = 1;
    draw(-1, -1, with_shown(4));
    expect_colour(framebuffer, level_x, level_y, 40, 40, 220);
    expect_clear(framebuffer, formal_x, level_y);

    levels[0] = 9;
    draw(-1, -1, with_shown(10));
    expect_colour(framebuffer, formal_x, level_y, 20, 20, 180);
    expect_clear(framebuffer, level_x, level_y);

    levels[0] = 4;
    draw(-1, -1, with_shown(2));
    expect_colour(framebuffer, level_x, level_y, 220, 20, 20);
    expect_clear(framebuffer, formal_x, level_y);

    levels[0] = 12;
    draw(-1, -1, with_shown(10));
    expect_colour(framebuffer, formal_x, level_y, 180, 20, 60);
    expect_clear(framebuffer, level_x, level_y);

    levels[0] = 0;
    stats.values[d2d::d2s::kSkillPts] = 0;
    draw(-1, -1, with_shown(3));
    expect_colour(framebuffer, icon_left, icon_bottom, 70, 70, 70);
    expect_colour(framebuffer, level_x, level_y, 40, 40, 220);

    return 0;
} catch (const std::exception& error) {
    std::fprintf(stderr, "%s\n", error.what());
    return 1;
}
