// The game menu and the mini-panel (docs/research/re/menu.md).
#include "gamemenu.hpp"

#include "common.hpp"

#include <algorithm>
#include <span>
#include <string>

namespace d2d::client {

namespace {

constexpr int kW = int(kScreenWidth), kH = int(kScreenHeight);

// A menu row (0x550 bytes in game.exe): type -1 title, 0 button, 1 choice,
// 2 slider; xp rows are LoD's only; count / def the choices or slider
// steps and the start; centred sliders (+0x128) draw OptBarC.
struct Row {
    int type;
    bool xp;
    std::string_view name;
    int count, def;
    bool centred;
    std::array<std::string_view, 4> choices;
};
struct Menu {
    int step, text_dy, pent_dy, slider_dy;   // the header's {step, text, cursor, slider} y offsets
    std::span<const Row> rows;
};
constexpr Row kMain[] = {                          // 0x713044
    { 0, false, "Options", 0, 0, false, {} },
    { 0, false, "Exit", 0, 0, false, {} },
    { 0, false, "ReturnToGame", 0, 0, false, {} },
};
constexpr Row kOptions[] = {                       // 0x714210
    { 0, false, "SoundOptions", 0, 0, false, {} },
    { 0, false, "VideoOptions", 0, 0, false, {} },
    { 0, false, "AutoMapOptions", 0, 0, false, {} },
    { 0, false, "CfgOptions", 0, 0, false, {} },
    { 0, false, "Previous", 0, 0, false, {} },
};
constexpr Row kSound[] = {                         // 0x714224, rows at 0x715cc8
    { -1, false, "SoundOptions", 0, 0, false, {} },
    { 2, false, "Sound", 21, 0, false, {} },
    { 2, false, "Music", 21, 0, false, {} },
    { 1, false, "3DSound", 2, 1, false, { "SmallOff", "SmallOn" } },
    { 1, false, "EAX", 2, 1, false, { "SmallOff", "SmallOn" } },
    { 2, false, "3DBias", 21, 0, true, {} },
    { 1, false, "NPCSpeech", 3, 2, false, { "AudioOnly", "TextOnly", "AudioText" } },
    { 0, false, "SPrevious", 0, 0, false, {} },
};
constexpr Row kVideo[] = {                         // LoD 0x71875c (classic 0x718748: no Resolution)
    { -1, false, "VideoOptions", 0, 0, false, {} },
    { 1, true, "Resolution", 2, 1, false, { "640x480", "800x600" } },   // ponytail: d2d runs at 800x600
    { 1, false, "LightQuality", 3, 2, false, { "Low", "Medium", "High" } },
    { 1, false, "BlendShadow", 2, 1, false, { "SmallOff", "SmallOn" } },
    { 1, false, "Perspective", 2, 1, false, { "SmallOff", "SmallOn" } },
    { 2, false, "Gamma", 21, 0, true, {} },
    { 2, false, "Contrast", 100, 50, true, {} },
    { 0, false, "SPrevious", 0, 0, false, {} },
};
constexpr Row kAutomap[] = {                       // LoD 0x71d734 (classic 0x71d720: no AutoMapMode)
    { -1, false, "AutoMapOptions", 0, 0, false, {} },
    { 1, true, "AutoMapMode", 2, 0, false, { "Full", "Mini" } },
    { 1, false, "AutoMapFade", 4, 0, false, { "SmallNo", "Center", "Everything", "Auto" } },
    { 1, false, "AutoMapCenter", 2, 1, false, { "SmallNo", "SmallYes" } },
    { 1, false, "AutoMapParty", 2, 1, false, { "SmallNo", "SmallYes" } },
    { 1, false, "AutoMapPartyNames", 2, 1, false, { "SmallNo", "SmallYes" } },
    { 0, false, "SPrevious", 0, 0, false, {} },
};
constexpr Menu kMenus[] = {
    { 50, 39, 51, 0, kMain }, { 50, 39, 51, 0, kOptions }, { 45, 34, 49, 36, kSound },
    { 45, 34, 49, 36, kVideo }, { 45, 34, 49, 36, kAutomap },
};

const d2d::palette::Palette& palette(const Scene& scene) { return scene.act1_pal.entries().empty() ? scene.pal : scene.act1_pal; }

// A frame by its bottom (FUN_004f6480); draw mode 1 (half) blends it
// half over what's there, 5 draws it as is.
void cel(std::vector<std::uint8_t>& framebuffer, const Scene& scene, const d2d::dc6::Sprite& sprite, int frame, int x, int bottom, bool half) {
    if (frame < 0 || frame >= int(sprite.frames_per_direction())) return;
    const auto& image = sprite.frame(0, std::uint32_t(frame));
    const auto& pal = palette(scene);
    const int top = bottom - int(image.height);
    for (int y = 0; y < int(image.height); ++y)
        for (int column = 0; column < int(image.width); ++column) {
            const int px = x + column, py = top + y;
            const auto index = image.pixels[std::size_t(y) * image.width + std::size_t(column)];
            if (index == 0 || px < 0 || px >= kW || py < 0 || py >= kH) continue;
            const auto colour = pal[index];
            auto* pixel = &framebuffer[(std::size_t(py) * kScreenWidth + std::size_t(px)) * 4];
            pixel[0] = half ? std::uint8_t((pixel[0] + colour.r) / 2) : colour.r;
            pixel[1] = half ? std::uint8_t((pixel[1] + colour.g) / 2) : colour.g;
            pixel[2] = half ? std::uint8_t((pixel[2] + colour.b) / 2) : colour.b;
        }
}

// A whole cel, its frames side by side 256 apart (FUN_00502680); align 1
// centres on x, 2 ends at it.
void cels(std::vector<std::uint8_t>& framebuffer, const Scene& scene, const d2d::dc6::Sprite& sprite, int x, int bottom, int align, bool half) {
    const int frames = int(sprite.frames_per_direction());
    unsigned total = 0;
    for (int i = 0; i < frames; ++i) total += sprite.frame(0, std::uint32_t(i)).width;
    if (align == 1) x += -1 - int(total >> 1);
    else if (align == 2) x -= int(total);
    for (int i = 0; i < frames; ++i) cel(framebuffer, scene, sprite, i, x + 0x100 * i, bottom, half);
}

const d2d::dc6::Sprite* text_image(const Scene& scene, std::string_view name) {
    const auto found = scene.menu_text.find(std::string(name));
    return found != scene.menu_text.end() ? &found->second : nullptr;
}

// A black rect (FUN_0046efd0 -> FUN_004f6300) at draw mode 0 / 1 / 2:
// 25 / 50 / 75% dark.
// unverified: modes 0 and 2 as 25 / 75% (mode 1, half, is npc-talk.md's).
void darken(std::vector<std::uint8_t>& framebuffer, int x, int y, int width, int height, int mode) {
    const int keep = 3 - mode;                       // quarters of the colour kept
    for (int py = std::max(0, y); py < std::min(kH, y + height); ++py)
        for (int px = std::max(0, x); px < std::min(kW, x + width); ++px) {
            auto* pixel = &framebuffer[(std::size_t(py) * kScreenWidth + std::size_t(px)) * 4];
            for (int c = 0; c < 3; ++c) pixel[c] = std::uint8_t(pixel[c] * keep / 4);
        }
}

// The knob's x past the bar's left end (FUN_0047e260: double -265 / (count-1) * value, truncated).
int knob(int count, int val) { return int(265.0 / double(count - 1) * double(val)); }

}  // namespace

std::vector<std::string_view> game_menu_images() {
    std::vector<std::string_view> out;
    for (const auto& menu : kMenus)
        for (const auto& row : menu.rows) {
            out.push_back(row.name);
            for (const auto choice : row.choices) if (!choice.empty()) out.push_back(choice);
        }
    return out;
}

GameMenu::GameMenu() {
    for (std::size_t m = 0; m < std::size(kMenus); ++m)
        for (std::size_t r = 0; r < kMenus[m].rows.size(); ++r) value[m][r] = kMenus[m].rows[r].def;
}

void GameMenu::show(int which) {
    menu = which;
    sel = int(rows().size()) - 1;
    held = drag = false;
}

std::vector<int> GameMenu::rows() const {
    std::vector<int> out;
    for (std::size_t r = 0; r < kMenus[menu].rows.size(); ++r)
        if (expansion || !kMenus[menu].rows[r].xp) out.push_back(int(r));
    return out;
}

int GameMenu::top() const {
    const int count = int(rows().size());
    return (kH - 80) / 2 - kMenus[menu].step * count / 2;
}

bool GameMenu::enabled(int row) const {
    const auto name = kMenus[menu].rows[std::size_t(row)].name;
    // 4df940 / 4df980 (a 3D provider), EAX, 4f5170 (the video driver's perspective)
    return name != "3DSound" && name != "EAX" && name != "3DBias" && name != "Perspective";
}

int GameMenu::hit(int mouse_y) const {
    const auto shown = rows();
    const int count = int(shown.size()), step = kMenus[menu].step;
    const int mid = (kH - 80) / 2, half = step * count / 2;
    if (mouse_y <= mid - half || mouse_y >= mid + half) return -1;
    int found = -1;
    for (int k = 0; k < count; ++k)
        if (mouse_y >= top() + step * k + count) found = k;     // sic: + count, not + step
    if (found < 0) return -1;
    const auto& row = kMenus[menu].rows[std::size_t(shown[std::size_t(found)])];
    if (row.type == -1 || !enabled(shown[std::size_t(found)])) return -1;
    return found;
}

void GameMenu::changed(Audio& audio, int row) {
    const auto name = kMenus[menu].rows[std::size_t(row)].name;
    // 47cda0 / 47cdf0: "Master Volume" / "Music Volume" = value * 100 / 20
    if (name == "Sound") audio.master_volume = value[std::size_t(menu)][std::size_t(row)] * 100 / 20;
    else if (name == "Music") audio.music_volume = value[std::size_t(menu)][std::size_t(row)] * 100 / 20;
    else return;
    audio.apply_volume();
    volume_changed = true;
}

GameMenu::Action GameMenu::activate(const Scene& scene, Audio& audio) {
    const int row_index = rows()[std::size_t(sel)];
    const auto& row = kMenus[menu].rows[std::size_t(row_index)];
    if (!enabled(row_index)) return kNone;
    if (row.type == 1) {
        auto& val = value[std::size_t(menu)][std::size_t(row_index)];
        if (++val > row.count - 1) val = 0;
        changed(audio, row_index);
        audio.play_sfx(scene, 1, 1.f, 0);
        return kNone;
    }
    if (row.type != 0) return kNone;
    Action action = kNone;
    if (row.name == "Options") show(1);                       // 0x47f2a0
    else if (row.name == "Exit") action = kExit;              // 0x47f2d0
    else if (row.name == "ReturnToGame") action = kClose;     // 0x47f300
    else if (row.name == "SoundOptions") {                    // 0x47f310; the sliders' init 47cdc0 / 47ce10
        value[2][1] = (audio.master_volume + 1) * 20 / 100;
        value[2][2] = (audio.music_volume + 1) * 20 / 100;
        show(2);
    }
    else if (row.name == "VideoOptions") show(3);
    else if (row.name == "AutoMapOptions") show(4);
    else if (row.name == "CfgOptions") action = kControls;    // 0x47f400: UI 0xb
    else if (row.name == "Previous") show(0);                 // 0x47f430
    else if (row.name == "SPrevious") show(1);                // 0x47f460
    audio.play_sfx(scene, 2, 1.f, 0);
    return action;
}

void GameMenu::slide(const Scene& scene, Audio& audio, int mouse_x, int mouse_y) {
    if (!drag && hit(mouse_y) != sel) return;
    const int row_index = rows()[std::size_t(sel)];
    const auto& row = kMenus[menu].rows[std::size_t(row_index)];
    auto& val = value[std::size_t(menu)][std::size_t(row_index)];
    const int old = val;
    if (enabled(row_index) && row.type == 2) {
        const bool labelled = text_image(scene, row.name) != nullptr;
        const int strip = kW / 2 + (labelled ? -0x3b : -0x90), base = kW / 2 + (labelled ? -0x3c : -0x91) + 12;
        if (drag || (mouse_x > strip && mouse_x < strip + 0x121)) {
            if (mouse_x < base) val = 0;
            else if (mouse_x > base + 0x109) val = row.count - 1;
            else val = int(double(mouse_x - base) / double(float(265.0 / double(row.count - 1) * 0.5)) + 1.0) / 2;
            drag = true;
        }
    }
    if (val != old) { changed(audio, row_index); audio.play_sfx(scene, 1, 1.f, 0); }
}

GameMenu::Action GameMenu::input(const Scene& scene, Audio& audio, const Mouse& mouse, const std::vector<SDL_Keycode>& keys, std::uint32_t now_ms) {
    Action action = kNone;
    const auto shown = rows();
    const int count = int(shown.size());
    for (const auto key : keys) {
        if (key == SDLK_DOWN || key == SDLK_UP) {          // 47d8a0 / 47d920: past titles and greyed rows
            const int dir = key == SDLK_DOWN ? 1 : count - 1;
            int next = sel;
            do next = (next + dir) % count;
            while (next != sel && (kMenus[menu].rows[std::size_t(shown[std::size_t(next)])].type == -1 || !enabled(shown[std::size_t(next)])));
            sel = next;
            audio.play_sfx(scene, 1, 1.f, 0);
        } else if (key == SDLK_LEFT || key == SDLK_RIGHT) {   // 47d9a0 / 47da90
            const int row_index = shown[std::size_t(sel)];
            const auto& row = kMenus[menu].rows[std::size_t(row_index)];
            auto& val = value[std::size_t(menu)][std::size_t(row_index)];
            const int old = val;
            if (!enabled(row_index)) continue;
            if (row.type == 1) val = key == SDLK_LEFT ? (val == 0 ? row.count - 1 : val - 1) : (val + 1 > row.count - 1 ? 0 : val + 1);
            else if (row.type == 2) val = std::clamp(val + (key == SDLK_LEFT ? -1 : 1), 0, row.count - 1);
            if (val != old) { changed(audio, row_index); audio.play_sfx(scene, 1, 1.f, 0); }
        } else if (key == SDLK_RETURN || key == SDLK_KP_ENTER) {   // 47db80
            if (const auto done = activate(scene, audio); done != kNone) return done;
        }
    }
    if (mouse.press_this_frame) {                          // 47d7f0
        if (const int over = hit(mouse.y); over != -1) sel = over;
        slide(scene, audio, mouse.x, mouse.y);
        held = true;
    }
    if (mouse.release_this_frame) {                        // 47d840
        if (held && hit(mouse.y) == sel) action = activate(scene, audio);
        held = drag = false;
    }
    if (mouse.x != last_x || mouse.y != last_y) {          // FUN_0047e3d0: a move drags or hovers
        last_x = mouse.x; last_y = mouse.y;
        if (held) slide(scene, audio, mouse.x, mouse.y);
        else if (const int over = hit(mouse.y); over != -1) sel = over;
    }
    if (now_ms - pent_ms > 1000) pent_ms = now_ms;
    for (; now_ms - pent_ms >= 40; pent_ms += 40) pent = (pent + 1) & 7;   // FUN_00454850's ticks
    return action;
}

void GameMenu::draw(std::vector<std::uint8_t>& framebuffer, const Scene& scene) const {
    const auto& def = kMenus[menu];
    const auto shown = rows();
    const int first = top();
    for (std::size_t k = 0; k < shown.size(); ++k) {
        const int row_index = shown[k];
        const auto& row = def.rows[std::size_t(row_index)];
        const int y = first + def.step * int(k), text_y = y + def.text_dy;
        const bool half = !enabled(row_index);              // draw mode 1, else 5
        const auto* label = text_image(scene, row.name);
        if (row.type <= 0) {
            if (label) cels(framebuffer, scene, *label, kW / 2, text_y, 1, half);
        } else if (row.type == 1) {
            if (label) cels(framebuffer, scene, *label, kW / 2 - 230, text_y, 0, half);
            if (const auto* choice = text_image(scene, row.choices[std::size_t(value[std::size_t(menu)][std::size_t(row_index)])]))
                cels(framebuffer, scene, *choice, kW / 2 + 230, text_y, 2, half);
        } else {
            if (label) cels(framebuffer, scene, *label, kW / 2 - 230, text_y, 0, half);
            // FUN_0047e260: the strip darkened either side of the knob, the
            // bar, the skull.
            const int val = value[std::size_t(menu)][std::size_t(row_index)];
            const int slider_y = y + def.slider_dy;
            const int bar_x = kW / 2 + (label ? -0x3c : -0x91);
            const int knob_x = bar_x + knob(row.count, val);
            const int strip_x = kW / 2 + (label ? -0x3b : -0x90), filled = knob_x - bar_x + 12;
            darken(framebuffer, strip_x, slider_y - 30, filled, 30, row.centred ? 1 : 2);
            darken(framebuffer, strip_x + filled, slider_y - 30, 0x121 - filled, 30, row.centred ? 1 : 0);
            cels(framebuffer, scene, row.centred ? scene.opt_bar_c : scene.opt_bar, label ? kW / 2 + 0xe6 : kW / 2, slider_y, label ? 2 : 1, half);
            cel(framebuffer, scene, scene.opt_skull, 0, knob_x, slider_y - (row.centred ? 0 : 1) - 1, half);
        }
    }
    // The pentagrams either side of the selection, spinning opposite ways.
    int widest = 0;
    for (std::uint32_t i = 0; i < scene.pentspin.frames_per_direction(); ++i) widest = std::max(widest, int(scene.pentspin.frame(0, i).width));
    const int pent_y = first + def.step * sel + def.pent_dy;
    cel(framebuffer, scene, scene.pentspin, pent ? 8 - pent : 0, kW / 2 - widest - 0xf9, pent_y, false);
    cel(framebuffer, scene, scene.pentspin, pent, kW / 2 + 0xf9, pent_y, false);
}

namespace {

constexpr std::array<int, 7> kMiniActions = { 0, 1, 2, 4, 5, 6, 7 };   // single player: no party (3)

// The first button's x (FUN_0047e8b0): centred, or beside the panels open.
int mini_x0(bool left, bool right) { return right ? kW / 2 - 0xca : left ? kW / 2 + 0x39 : kW / 2 - 0x4a; }

bool on_menu_button(int mouse_x, int mouse_y) {           // FUN_00497780
    return mouse_x >= kW / 2 - 8 && mouse_x <= kW / 2 + 5 && mouse_y >= kH - 0x27 && mouse_y <= kH - 0xd;
}

}  // namespace

bool MiniPanel::input(const Scene& scene, Audio& audio, const Mouse& mouse, bool left, bool right, int& action) {
    action = -1;
    bool took = false;
    // The HUD button: a press holds it with a click (0x499621), let go over
    // it and the mini-panel opens or closes (0x499a82).
    if (mouse.press_this_frame && on_menu_button(mouse.x, mouse.y)) {
        button_down = true;
        audio.play_sfx(scene, 4, 1.f, 0);
        took = true;
    }
    if (mouse.release_this_frame && button_down) {
        if (on_menu_button(mouse.x, mouse.y)) open = !open;
        button_down = false;
        took = true;
    }
    if (!open || (left && right)) { down.fill(false); return took; }   // both sides open: hidden (0x7bc970)
    const int centre = kW / 2 + (right ? -0x76 : 0) + (left ? 0x77 : 0);
    const bool in_box = mouse.x > centre - 0x54 && mouse.x < centre + 0x59 && mouse.y > kH - 0x45 && mouse.y < kH - 0x2f;
    const int x0 = mini_x0(left, right);
    if (mouse.press_this_frame && in_box) {                // FUN_0047ef30
        for (std::size_t i = 0; i < down.size(); ++i) {
            const int button_x = x0 + 0x15 * int(i);
            if (mouse.x > button_x && mouse.x < button_x + 0x14) { down[i] = true; audio.play_sfx(scene, 4, 1.f, 0); }
        }
        took = true;
    }
    if (mouse.release_this_frame && std::ranges::any_of(down, [](bool held) { return held; })) {   // FUN_0047ed90
        for (std::size_t i = 0; i < down.size(); ++i) {
            const int button_x = x0 + 0x15 * int(i);
            if (in_box && mouse.x > button_x && mouse.x < button_x + 0x14) action = kMiniActions[i];
        }
        down.fill(false);
        took = true;
    }
    return took;
}

void MiniPanel::draw(std::vector<std::uint8_t>& framebuffer, const Scene& scene, bool left, bool right, int mouse_x, int mouse_y) const {
    if (open && !(left && right)) {                        // FUN_0047f710
        const int panel_x = right ? kW / 2 - 0xcd : left ? kW / 2 + 0x38 - 2 : kW / 2 - 0x4a - 3;
        cel(framebuffer, scene, scene.minipanel, 0, panel_x, kH - 0x2f, false);
        const int x0 = mini_x0(left, right);
        for (std::size_t i = 0; i < kMiniActions.size(); ++i)
            cel(framebuffer, scene, scene.minipanel_btn, 2 * kMiniActions[i] + (down[i] ? 1 : 0), x0 + 0x15 * int(i), kH - 0x32, false);
    }
    // FUN_004977c0: frames 0 / 1 closed, 2 / 3 open, the odd one held down.
    const int frame = (open ? 2 : 0) + (button_down && on_menu_button(mouse_x, mouse_y) ? 1 : 0);
    cel(framebuffer, scene, scene.menu_button, frame, kW / 2 - 8, kH - 16, false);
}

}  // namespace d2d::client
