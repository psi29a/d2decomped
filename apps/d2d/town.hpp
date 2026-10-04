// SPDX-License-Identifier: GPL-3.0-or-later
// The town: the in-game character's world state (position, walking,
// panels, NPC menus, store, merc, ...) and one frame of play — input,
// movement, NPCs, then the render.
#pragma once

#include "audio.hpp"
#include "common.hpp"
#include "gamemenu.hpp"
#include "netgame.hpp"
#include "panels.hpp"
#include "platform.hpp"
#include "scene.hpp"
#include "skillbar.hpp"
#include "ui.hpp"
#include "world_view.hpp"

#include <d2s_items.hpp>
#include <quests.hpp>
#include <rules.hpp>
#include <shrines.hpp>
#include <weather.hpp>

#include <cstdint>
#include <filesystem>
#include <map>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace d2d::client {

// The hovered monster's name on its life bar, top centre: a dark red bar
// as wide as the name plus a margin, filled by its share of life left.
// ponytail: D2's own bar (game.exe draws it with the MonsterIndicators
// font and per-type colours) isn't traced; this is its look by eye.
void draw_monster_bar(std::vector<std::uint8_t>& framebuffer, const Scene& scene, const Monster& monster);

// The client's drawing of what the World told it (View): the ground
// items, fires, the merc, pets, missiles and monsters near (cx, cy) as
// units the world draws by depth (npc -2 the merc, -3 pets, -10 - i
// monster i of the View, -1000 - i ground item i).
//
// A unit's states' look (Scene::StateInfo) onto it: the colour shift of
// the one with the highest colorpri, each one's overlays while it lasts,
// its cast overlay once from when the client first saw it (StateClock).
// ponytail: an overlay's light (Radius) isn't stamped; LoopWaitTime isn't
// applied.
struct StateClock {
    struct Seen { std::uint32_t start = 0, last = 0; };
    std::map<std::pair<int, const GameData::StateInfo*>, Seen> seen;   // by (unit key, state)
    std::uint32_t now = 0;
};
inline void dress(const Scene& scene, Unit& unit, int key, std::span<const std::string_view> names, StateClock* clk) {
    int pri = -1;
    for (const auto name : names) {
        const auto found = scene.states.find(std::string(name));
        if (name.empty() || found == scene.states.end()) continue;
        const auto& state = found->second;
        // colorshift counts from 1: "blue" (108) is table 107, blue; 108
        // is purple. "red" (100) → 99 red, "poison" (104) → 103 green.
        if (state.shift >= 1 && state.shift <= 111 && state.pri > pri && scene.colour_shifts.size() >= 111 * 256) {
            pri = state.pri;
            unit.shift = scene.colour_shifts.data() + (state.shift - 1) * 256;
        }
        std::uint32_t start = 0;
        if (clk) {
            auto [seen, fresh] = clk->seen.try_emplace({ key, &state }, StateClock::Seen{ clk->now, clk->now });
            if (!fresh && clk->now - seen->second.last > 200) seen->second.start = clk->now;   // gone a while: a new one
            seen->second.last = clk->now;
            start = seen->second.start;
        }
        for (const auto* overlay : state.over) if (overlay) unit.overs.push_back({ overlay, start, false });
        if (state.cast) unit.overs.push_back({ state.cast, start, true });
    }
}
// The states on a monster and on the player, by States.txt name.
inline std::vector<std::string_view> monster_states(const Scene& scene, const Monster& monster, std::uint32_t now, int player_aura = 0) {
    std::vector<std::string_view> out;
    if (!monster.alive()) return out;
    if (now < monster.poison_until) out.push_back("poison");
    if (now < monster.chill_until) out.push_back("cold");
    if (now < monster.stun_until) out.push_back("stunned");
    for (const auto& effect : { monster.curse, monster.cry })
        if (effect.skill >= 0 && now < effect.until)
            if (const auto* skill = scene.skills.get(effect.skill)) out.push_back(skill->auratarget);
    if (monster.aura > 0)
        if (const auto* skill = scene.skills.get(monster.aura)) out.push_back(skill->aurastate);
    if (monster.in_aura && player_aura > 0)
        if (const auto* skill = scene.skills.get(player_aura)) out.push_back(skill->auratarget);
    return out;
}
inline std::vector<std::string_view> player_states(const Scene& scene, const View& view) {
    std::vector<std::string_view> out;
    if (view.poisoned) out.push_back("poison");
    if (view.chilled) out.push_back("cold");
    for (const int buff : view.buffs) if (const auto* skill = scene.skills.get(buff)) out.push_back(skill->aurastate);
    if (view.aura > 0) if (const auto* skill = scene.skills.get(view.aura)) out.push_back(skill->aurastate);
    if (const auto state = d2d::rules::shrine_state(view.boost_code); !state.empty()) out.push_back(state);   // its overlays
    return out;
}
constexpr std::uint32_t kPortalOpenMs = 15 * 40 * 256 / 200;
void view_units(const Scene& scene, const View& view, float camera_x, float camera_y, const std::string* merc_label, std::vector<Unit>& out,
                std::span<const View::Shot> effects = {}, std::uint32_t now_ms = 0, const std::string* corpse_name = nullptr, int cls = 0,
                StateClock* clk = nullptr);
// Over the world: the hovered (else attacked) monster's life bar, the
// death message.
void view_overlays(std::vector<std::uint8_t>& framebuffer, const Scene& scene, const View& view, int hovered);
// An SQ skill's frame now: the mode and when it started, as Fight::seq_view.
std::pair<int, std::uint32_t> view_seq(const Scene& scene, int cls, const View& view, std::uint32_t now_ms);

// The client (docs/design/multiplayer.md): input, panels, camera,
// drawing and sound, over a World (world.hpp) it sends commands to. The
// references below are the World's, for the code that reads them.
// The frame's light (FUN_00475800): the grid round the player at the
// level's own light or the day's, then each light stamped. Positions in
// eighths of a subtile (a cell is 40).
// A light's radius moves toward a new one 8 eighths (a subtile) a frame
// (FUN_004755a0: +0x18 toward +0x1c); a new light starts at its first
// radius (FUN_00474160; an overlay's InitRadius).
// ponytail: light quality is taken as high (2: shadows on).
Lighting frame_light(const Scene& scene, const View& view, float cam_x, float cam_y, std::span<const View::Shot> effects = {}, int ambient = -1,
                     std::span<const Unit> units = {}, const Unit* player_look = nullptr, std::uint32_t now = 0);

struct Town {
    const Scene* scene = nullptr;
    CharCreateUI& character;                      // the in-game character (save, items, stats)
    World world;                           // the game server's side (in-process: single player)
    const Level* level = nullptr;          // where the character is (the View's): the town, the Blood Moor, the Den of Evil
    d2d::rules::Rng rng{ 0x7f4a7c15u };    // the client's own rolls (which gossip, sound variations)
    Cues cues{ scene };                    // the world's sounds the View brought, due to play
    // The World's state by name, for the devctl verbs (admin and tests, on
    // the server's side); the client itself reads only `view`.
    UnitState& player = world.player;
    std::vector<UnitState>& npc_states = world.npc_states;
    std::optional<UnitState>& merc = world.merc;
    Loot& loot = world.loot;
    Fight& fight = world.fight;
    float& target_x = world.target_x;
    float& target_y = world.target_y;
    LocalTransport net;                    // the commands to the World, as their wire form (single player)
    View view;                             // what the World told the client after its last tick
    ViewEncoder view_enc;                  // the host's memory of what this client was sent
    std::unique_ptr<NetGame> net_game;     // --join: a game on a game.exe host (its other players and monsters)
    std::unordered_map<std::uint32_t, Monster> net_monsters;   // the host's monsters as d2d draws them, by unit id
    int net_attack = -1, net_attack_skill = 0;               // the host monster being attacked, with what
    int net_warp_sent = -1;                                  // the warp whose 0x13 went to the host
    int net_pick = -1;                                       // the host item being walked to, to pick up
    std::unordered_map<std::uint32_t, Loot::GroundItem> net_ground;   // the host's ground items as drawn, labels made once
    int talking_sent = -1;                 // the NPC last reported as talked to (cmd::Chat)
    bool press_on_ui = false;              // the held left button was pressed on the UI
    std::uint32_t world_ms = 0;            // the World's clock: when it last ticked
    float prev_x = 0, prev_y = 0;          // the player a tick before: the camera slides between the two
    float cam_x = 0, cam_y = 0;            // where the camera is this frame
    std::unordered_map<int, Automap> other_automaps;   // other Layers' maps, while elsewhere
    std::uint32_t level_ms = 0;            // when the player entered `level`
    bool have_world = false;
    bool  stash_open = false;
    std::optional<d2d::d2s::Item> held;   // the item on the cursor (the View's)
    int identify_with = -1;               // a right-clicked Scroll / Tome of Identify's id: the next click picks the item
    int   stat_pressed = -1;               // char panel stat button held down
    bool  tree_open = false;               // skill tree ('T')
    QuestLog quest_log;                    // the quest log ('Q'), where the character panel goes
    int   tree_tab = 1;                    // 1..3, bottom tab first (0x724bec starts at 1)
    int   skill_pressed = -1;              // skill icon held down
    std::vector<d2d::rules::MercOffer> hire_offers;   // Kashya's list while it's open (the View's)
    std::string merc_label;
    bool  belt_open = false;               // belt popup (` key or a click on the belt)
    bool  cube_open = false;               // right-click the Horadric Cube item
    NpcMenuState npc_menu;                 // open NPC menu (npc < 0: none)
    Automap automap;                       // Tab
    Store store;                           // an open vendor store (npc < 0: none): the View's stock, the client's tab and buttons
    WaypointUI waypoint;                   // the waypoint panel
    Speech speech;                         // NPC talking (npc < 0: none)
    std::vector<d2d::rules::QuestMsg> npc_quest;   // what the NPC being talked to has on quests
    // The level's random ambient sound (SoundEnviron Day / Night Event,
    // FUN_004e42e0): its sound, the gap to the next and the last one's
    // time, in sound ticks (25 Hz).
    int amb_event = 0;
    std::uint32_t amb_next = 0, amb_last = 0;
    d2d::rules::Rng sound_rng{ 0x5eed5u };
    std::vector<int> gossip_pick;          // per world NPC: chosen gossip topic, -1 = not yet
    SkillBar skillbar{ scene, character };        // the skill buttons, picker and hotkeys (skillbar.hpp)
    int   hovered_npc = -1;                // Level::npcs index under the cursor (last frame); <= -10: monster -10 - i
    std::uint32_t now_ms = 0;              // this frame's ms (devctl)   // shrines / chests used: when
    bool  player_walked = false;           // `walking` as of the last frame
    bool alt_held = false;                 // devctl `debug alt on`: "Show Items" held
    StateClock state_clock;                // when each unit's states were first seen (their cast overlays)
    std::uint32_t walk_ms = 0;             // when the player last started or stopped walking (the client's clock)
    bool  player_ran = false;
    bool  inv_open = false;   // 'I' — inventory panel
    bool  char_open = false;  // 'C' — character panel
    bool  stats_down = false, skills_down = false;   // New Stats / New Skills held (DAT_007c02e4 / DAT_007c02e8)
    bool  run_down = false;                          // the run button held (DAT_007befd8)
    GameMenu game_menu;                    // Esc's menu (UI 9)
    MiniPanel mini;                        // the mini-panel (UI 0x15) and the HUD's button for it
    std::filesystem::path cfg_file;        // where the menu's volumes are kept (d2d.cfg; empty: not kept)

    Town(const Scene* game_scene, CharCreateUI& player_character, int start_x = -1, int start_y = -1)
        : scene(game_scene), character(player_character), world(game_scene, start_x, start_y) {
        have_world = world.level && !world.level->dt1s.empty();
        view = world.view();
        level = view.level;
    }

    // What the World tells the client: its View, over as bytes too. The
    // character in it becomes the client's (the panels draw it); the store
    // keeps the client's tab and buttons.
    void publish();
    void send(const Command& command);    // to the World, and to a joined host

    // Into the game with the character the client has (a save loaded, or
    // made): the World takes it.
    void enter();

    // Saving the character: the item on the cursor goes back first, then
    // the World writes it.
    std::string save();

    // devctl: operate object i now, as the server would on arrival.
    void operate(int npc_index, std::uint32_t frame_ms, int force = -1);

    // A fresh game for the character: the Blood Moor's monsters at its
    // difficulty, no loot about.
    void net_overlay();                   // the joined host's units into `view`
    void new_game();

    // The game menu (FUN_0047e090 / FUN_0047e200): opening closes the
    // panels, the automap and the mini-panel; closing reopens those two.
    void open_game_menu();
    void close_game_menu();

    // One InGame frame: keys, panels, clicks, walking, NPCs, then the render.
    // Esc with nothing open opens the game menu; its Exit saves and goes
    // back to the roster (screen).
    void update(std::vector<std::uint8_t>& framebuffer, const Mouse& frame_mouse, const std::vector<SDL_Keycode>& keys_this_frame,
                Screen& screen, Audio& audio, std::uint32_t frame_ms, std::uint32_t last_ms);

    // The monster / ground item under the cursor (hovered_npc -10 - i / -1000 - i), or -1.
    [[nodiscard]] int hovered_monster() const;
    [[nodiscard]] int hovered_ground() const;
    // The New Stats / New Skills buttons as the panels stand.
    [[nodiscard]] LevelButtons level_buttons_now() const;
    // The quest log's inputs: this difficulty's flags, what the server told.
    [[nodiscard]] const d2d::rules::QuestBits& quest_bits() const;
    [[nodiscard]] QuestState quest_state() const { return { view.quest_log, view.game_quests, view.den_left }; }
    void toggle_quest_log();               // Q and the mini-panel (FUN_004a3fe0)
    [[nodiscard]] QuestLogButton quest_log_button_now() const;

    // The client's side of a click: what it asks the server for
    // (protocol.hpp). A held left button re-aims the walk; a press picks
    // what's under the cursor: a monster to attack, an item, an object or
    // NPC, else the ground. The right button uses the right skill there.
    [[nodiscard]] std::vector<Command> input(const Mouse& mouse, bool over_ui) const;

    // The game this frame: what the player asks for (input), the World's
    // step, then what it told the client (events).
    void walk(const Mouse& mouse, bool over_ui, std::uint32_t frame_ms, std::uint32_t last_ms);

    // What the World told the client.
    void handle(const Event& event, std::uint32_t frame_ms);
    int menu_after_speech = -1;                    // the NPC whose menu opens once its quest speech ends
    bool questdone_sound = false;                  // the quest log's done animation began (draw → update)
    bool quest_log_was_open = false;               // UI 0xf last frame: shutting it marks the animations seen (FUN_004a28d0)
    int replay_speech = 0;                         // questlast: a quest message to play again
    d2d::rules::Rain rain;                         // the weather (its state lasts the session, like game.exe's)
    std::uint32_t rain_ms = 0;
    float rain_cam_x = 0, rain_cam_y = 0;          // the camera at the last weather tick
    int rain_vol = 0;                              // the rain sound's volume 0..255
    // The Den of Evil cleared (docs/research/re/quests.md "The Den lights
    // up"): S→C 0x2d event 0 starts a count (FUN_0046b0c0); for 30 frames
    // the Den's ambient falls from 80 (FUN_0046bd50), then its rooms get
    // light beams, missile denofevillight, three to a room at free spots
    // (FUN_0046b0d0 / FUN_0046af70), and keep them (client func 23).
    int den_flash = -1;
    bool den_seen = false, den_lit = false;
    std::vector<View::Shot> den_beams;
    [[nodiscard]] int den_ambient() const;
    void den_tick(std::uint32_t frame_ms);
    // NPC `npc`'s menu, placed by its feet on screen as render_world
    // projects them.
    void open_menu(int npc);

    // The frame: the world with its units, the open panels, the tree and
    // the waypoint panel.
    void draw(std::vector<std::uint8_t>& framebuffer, const Mouse& mouse, std::uint32_t frame_ms);
};

}  // namespace d2d::client
