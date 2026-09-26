// Fighting: the Blood Moor's monsters and missiles, the player's combat
// modes (swing, flinch, block, death) and Fighter (components/rules/
// combat.hpp), hits and kills, damage over time, the merc in a fight,
// potions and regeneration, monster sounds, the monster life bar.
#pragma once

#include "loot.hpp"

namespace {

// The hovered monster's name on its life bar, top centre: a dark red bar
// as wide as the name plus a margin, filled by its share of life left.
// ponytail: D2's own bar (game.exe draws it with the MonsterIndicators
// font and per-type colours) isn't traced; this is its look by eye.
void draw_monster_bar(std::vector<std::uint8_t>& fb, const Scene& s, const Monster& m) {
    const auto& name = m.npc.name;
    if (name.empty() || !m.alive()) return;
    const int w = std::max(s.font.measure(name) + 20, 120), h = s.font.line_height() + 4;
    const int x0 = int(kW) / 2 - w / 2, y0 = 10;
    const int filled = w * std::clamp(m.hp, 0, m.st.hp) / std::max(m.st.hp, 1);
    for (int y = y0; y < y0 + h; ++y)
        for (int x = x0; x < x0 + w; ++x) {
            auto* p = fb.data() + (std::size_t(y) * kW + std::size_t(x)) * 4;
            const bool on = x - x0 < filled;
            p[0] = on ? 0x88 : 0x20; p[1] = on ? 0x08 : 0x10; p[2] = on ? 0x08 : 0x10;
        }
    const auto& pal = s.act1_pal.entries().empty() ? s.pal : s.act1_pal;
    s.font.draw(fb, kW, kH, pal, int(kW) / 2 - s.font.measure(name) / 2, y0 + 2, name);
}

// The skills d2d uses as game.exe does so far (docs/research/re/skills.md):
// the Bash family (srvstfunc 32 builds the record, srvdofunc 2 resolves
// it: Bash, Stun, Concentrate; 6 Power Strike, 39 Berserk, 35 Vengeance
// resolve the same way), Dragon Talon (24 / 42: calc1 kicks), the
// charge-ups (23 / 34, 35: Tiger Strike .. Royal Strike) and Dragon Tail
// (27 / 50: a kick, then fire around the target), Zeal (37 / 13: calc1
// hits), Sacrifice (29 / 64: a hit that costs life) and Smite (- / 150:
// the shield); and on their sequences' hits Jab (5 / 7), Dragon Claw
// (25 / 46), Frenzy (- / 9), Double Swing (- / 70) and Impale (7 / 2);
// Fend (9 / 13) as Zeal, a hit per enemy in reach; Poison Dagger (16 /
// 32: FUN_005c30a0 builds Bash's way with its poison, FUN_005c4cd0
// resolves).
// Every other skill swings a plain attack for now.
// Skills that move the player: Whirlwind (38 / 76), Charge (31 / 67),
// Leap Attack (41 / 78).
inline bool moving_skill(const d2d::rules::Skill& s) {
    return (s.srvstfunc == 38 && s.srvdofunc == 76) || (s.srvstfunc == 31 && s.srvdofunc == 67) || (s.srvstfunc == 41 && s.srvdofunc == 78);
}
inline bool skill_built(const d2d::rules::Skill& s) {
    return (s.srvdofunc == 2 && (s.srvstfunc == 32 || s.srvstfunc == 6 || s.srvstfunc == 39 || s.srvstfunc == 35)) || (s.srvstfunc == 24 && s.srvdofunc == 42)
        || (s.srvstfunc == 23 && (s.srvdofunc == 34 || s.srvdofunc == 35)) || (s.srvstfunc == 27 && s.srvdofunc == 50)
        || (s.srvstfunc == 37 && s.srvdofunc == 13) || (s.srvstfunc == 29 && s.srvdofunc == 64) || s.srvdofunc == 150
        || (s.srvstfunc == 5 && s.srvdofunc == 7) || (s.srvstfunc == 25 && s.srvdofunc == 46) || s.srvdofunc == 9 || s.srvdofunc == 70
        || (s.srvstfunc == 9 && s.srvdofunc == 13) || (s.srvstfunc == 7 && s.srvdofunc == 2) || (s.srvstfunc == 16 && s.srvdofunc == 32)
        || moving_skill(s);
}
// Self casts (right click, no target): Holy Shield (36 / 18).
inline bool self_cast(const d2d::rules::Skill& s) { return s.srvstfunc == 36 && s.srvdofunc == 18; }
inline bool attack_mode(int m) { return m == kModeA1 || m == kModeKK || m == kModeS1; }
// A finishing move releases charges (FUN_005d5220 runs after Attack's
// srvdofunc and the finishers'): Attack, Dragon Talon, Dragon Tail, and
// each Dragon Claw hit (FUN_005d6340 releases after FUN_005d6200's).
// ponytail: Dragon Flight isn't built, so it swings as Attack and
// releases that way.
inline bool finisher(const d2d::rules::Skill* s) {
    return !s || s->id == 0 || s->srvdofunc == 42 || s->srvdofunc == 50 || s->srvdofunc == 46;
}

struct Fight {
    const Scene* scene;
    const Level* const& level;             // Town's: where the player is
    CharCreateUI& cc;
    UnitState& player;
    std::optional<UnitState>& merc;
    const Npc* const& merc_npc;
    d2d::rules::Rng& rng;
    Loot& loot;
    Cues& cues;

    std::vector<Monster> monsters;         // the Blood Moor's (Level::spawns), kept while the game runs
    std::vector<Missile> missiles;         // in flight in the Blood Moor
    std::vector<Missile> pending;          // made this frame, joining `missiles` after its update
    std::vector<std::pair<int, std::uint32_t>> strafe;   // Strafe's arrows to come: target, when
    std::vector<std::pair<int, int>> burned;             // this frame's burns: monster, skill
    int channel = -1, last_frame = -1;     // Inferno's skill while its cast lasts
    // The monster being attacked (walked up to, then struck), the player's
    // non-walking mode (A1 attack, GH get-hit, BL block, DT dying, DD dead;
    // -1 none) and when it ends.
    int   attack_mon = -1;
    int   pmode = -1;
    std::uint32_t pmode_until = 0;
    bool  pstruck = false;                 // this swing's hit is resolved
    float prate = 1.f;                     // the mode's animation rate (attack speed, FHR, FBR)
    d2d::rules::Fighter pf;                // the player in a fight, as of this frame
    d2d::rules::Fighter pf_kick;           // the same without the weapon (kicks: FUN_00646280 takes it off)
    d2d::rules::StatSum psum{};            // the player's stats (gear, passives) as of this frame
    float cast_x = 0, cast_y = 0;          // where a missile skill was sent
    int aura = 0;                          // the aura on (Town: the right skill when it's one)
    // The player's summons (skills.md "Summons"): a Monster of the skill's
    // summon row on the player's side, the skill that raised it, and the
    // monster it's after.
    // A trap (do 45) shoots instead: its skill (a monster skill whose
    // missile carries the player's), at its level, `shots` times.
    struct Pet {
        Monster m; int skill = 0; int target = -1; int shot_skill = -1, shot_level = 0, shots = 0;
        // Its sumskills d2d uses (FUN_0056deb0 at their sumsk calcs): a
        // missile it shoots (the skeletal mage's, the hydra's), an aura it
        // runs (Fire Golem's Holy Fire, the totems').
        int ranged = -1, ranged_level = 0, aura = -1, aura_level = 0;
        std::uint32_t aura_next = 0;
        // What the summoning skill's stats gave it (FUN_005c4470).
        std::array<int, 4> res{};                  // fire, lightning, cold, poison
        int thorns = 0, fire_lo = 0, fire_hi = 0, speed_pct = 0;
        const Level* where = nullptr;              // the level it's on (pets follow the player across)
        std::uint32_t until = 0;                   // it goes then (Decoy, Revive, Hydra; 0 never)
        int variant = 0;                           // a skeletal mage's element (its +0xf, FUN_005ce0b0)
        int hits = 0;                              // a Raven's attacks left (0: no count)
        bool idle = false, mirror = false;         // Decoy stands; a Shadow Warrior swings the owner's blow
    };
    std::vector<Pet> pets;
    std::uint32_t aura_next = 0;           // its next pulse
    // The skill the player attacks with (Skills.txt id; 0 Attack), the one
    // this swing uses (Attack when it's not built or can't be paid for),
    // and the strikes still to come (Dragon Talon's kicks, Zeal's hits).
    int   attack_skill = 0, swing_skill = 0, kicks_left = 0;
    std::int64_t self_hurt = 0;            // life (256ths) the player's own skills cost (Sacrifice), taken with the monsters' hits
    // An SQ skill's sequence while it plays (sequences.hpp, for the weapon
    // class): its frames, each seq_frame_ms long, and the hits (event 1)
    // already struck.
    std::span<const d2d::rules::SeqFrame> seq{};
    std::uint32_t seq_frame_ms = 40;
    int seq_struck = 0;
    bool seq_loop = false;                 // Whirlwind's plays over until it arrives
    // A skill that moves the player (Whirlwind, Charge, Leap Attack):
    // toward (tx, ty) at `speed` cells/s while `on`; `fly` ignores walls
    // (a leap); whirl_next is Whirlwind's next hit time (FUN_005d9320).
    struct SkillMove { float tx = 0, ty = 0, speed = 0; bool on = false, fly = false; };
    SkillMove smove;
    float move_x = 0, move_y = 0;          // where the click sent a moving skill
    std::uint32_t whirl_next = 0;
    std::vector<int> told;                 // skills logged as not built yet
    // A charge-up's charges (FUN_005d3320: its aurastate, the skill and
    // level in stats 0x15e / 0x15f, the count, at most 3, in aurastat1),
    // until auralencalc ticks after the last one.
    struct Charge { int skill = 0, level = 0, count = 0; std::uint32_t until = 0; };
    std::vector<Charge> charges;
    // Self states from a swing (aurastate): Concentrate's lasts while its
    // swing does (made with no length; its removal isn't traced), Berserk's
    // calc2 ticks (FUN_005d97f0, 10 when that's 0).
    struct SelfState { int skill = 0, level = 0; std::uint32_t until = 0; };
    std::vector<SelfState> self_states;
    // The player's level in a skill: points, and with item bonuses (Town
    // points these at its SkillBar).
    std::function<int(int)> skill_base, skill_level;
    // The merc in a fight: its stats (hireling.txt at its level), life,
    // mode (NU/WL follow, A1 attack, GH, DT) and the monster it's after.
    d2d::rules::MercStats merc_st;
    int   merc_life = 0;
    std::string_view merc_mode = "NU";
    std::uint32_t merc_until = 0;
    bool  merc_struck = false;
    int   merc_target = -1;
    // Potions working: life / mana (8.8 fixed) a millisecond, until when.
    struct Regen { double life = 0, mana = 0; std::uint32_t until = 0; bool poison = false; };
    std::vector<Regen> regen;

    // A fresh game at `difficulty`: its monsters, nothing in flight.
    void new_game(int difficulty) {
        monsters = spawn_monsters(*scene, scene->moor, rng, difficulty);
        missiles.clear();
        regen.clear();
        charges.clear();
        self_states.clear();
        pets.clear();
        attack_mon = -1;
        pmode = -1;
    }
    // The save's merc joins: its fighting stats at its experience.
    void merc_joins() {
        const auto& h = cc.header;
        merc_st = d2d::rules::merc_stats(scene->rules, h.merc_type, h.merc_exp);
        merc_life = merc_st.life;
        merc_mode = "NU"; merc_target = -1;
    }
    // Back in camp after dying: full life, no potions or poison working.
    void revive(std::uint32_t ms) {
        using namespace d2d::d2s;
        cc.stats.v[kLife] = cc.stats.v[kMaxLife];
        regen.clear();
        charges.clear();
        self_states.clear();
        pmode = -1; player.mode_ms = ms; attack_mon = -1;
        if (merc) { merc->x = player.x + 1; merc->y = player.y + 1; merc->path.clear(); merc_mode = "NU"; merc_target = -1; }
    }

    // Keys 1-4 drink the belt's bottom-row potion in that column: healing
    // and mana potions restore their amount over their length, a
    // rejuvenation its percentages at once.
    // ponytail: no class potion bonus (CharStats HealthPotionPercent).
    void drink(int col, std::uint32_t ms) {
        using namespace d2d::d2s;
        if (dead()) return;
        const auto code = d2d::rules::drink_belt(scene->rules, cc.items, col);
        if (code.empty()) return;
        const auto& p = scene->rules.potions.at(code);
        if (p.percent) {
            cc.stats.v[kLife] = std::min(cc.stats.v[kMaxLife], cc.stats.v[kLife] + cc.stats.v[kMaxLife] * p.life / 100);
            cc.stats.v[kMana] = std::min(cc.stats.v[kMaxMana], cc.stats.v[kMana] + cc.stats.v[kMaxMana] * p.mana / 100);
        } else {
            const double len = std::max(p.ticks, 1) * 40.0;
            regen.push_back({ p.life * 256.0 / len, p.mana * 256.0 / len, ms + std::uint32_t(len) });
        }
        cues.cue("item_potion_drink", ms, player.x, player.y);
    }
    // Potions and poison, then the steady regeneration: replenish life
    // (hpregen, N/256 a tick) and mana (all of it in 120 s, faster by the
    // manarecoverybonus %). Poison is negative hpregen, and the player's
    // regen tick (FUN_00580610) never takes life below 1: poison can't kill
    // a player (docs/research/re/combat.md).
    // ponytail: the 120 s mana base is the commonly given rule, not traced.
    double regen_acc_life = 0, regen_acc_mana = 0;
    void apply_regen(std::uint32_t ms, std::uint32_t last_ms) {
        using namespace d2d::d2s;
        double life = 0, mana = 0;
        for (const auto& r : regen) {
            const double t = double(std::min(ms, r.until) - std::min(last_ms, r.until));
            life += r.life * t; mana += r.mana * t;
        }
        std::erase_if(regen, [&](const Regen& r) { return ms >= r.until; });
        const double dt = double(ms - last_ms);
        life += pf.life_regen * 256.0 / 256.0 * dt / 40.0;
        mana += double(cc.stats.v[kMaxMana]) / 120000.0 * (100 + pf.mana_regen) / 100.0 * dt;
        regen_acc_life += life; regen_acc_mana += mana;
        const auto dl = std::int64_t(regen_acc_life), dm = std::int64_t(regen_acc_mana);
        regen_acc_life -= double(dl); regen_acc_mana -= double(dm);
        auto& L = cc.stats.v[kLife];
        if (dl < 0) L = std::max<std::int64_t>(std::min<std::int64_t>(L, 256), L + dl);
        else L = std::max(L, std::min(cc.stats.v[kMaxLife], L + dl));
        cc.stats.v[kMana] = std::max(cc.stats.v[kMana], std::min(cc.stats.v[kMaxMana], cc.stats.v[kMana] + dm));
    }
    // A monster's new mode sounds off (MonSounds.txt): an attack's cry (at
    // its chance) and weapon, get-hit, death, each after its delay in ticks.
    void monster_sounds(Monster& m, std::uint32_t ms) {
        if (m.mode == m.last_mode) return;
        m.last_mode = m.mode;
        const auto it = scene->mon_sounds.find(scene->monsters.types[std::size_t(m.type)].sound);
        if (it == scene->mon_sounds.end()) return;
        const auto& S = it->second;
        if (m.mode == "A1" || m.mode == "A2") {
            const std::size_t k = m.mode == "A2";
            if (rng(100) < S.att_prb[k]) cues.cue(S.attack[k], ms + std::uint32_t(S.att_del[k]) * 40, m.u.x, m.u.y);
            cues.cue(S.weapon[k], ms + std::uint32_t(S.wea_del[k]) * 40, m.u.x, m.u.y);
        } else if (m.mode == "GH") {
            cues.cue(S.hit, ms + std::uint32_t(S.hit_del) * 40, m.u.x, m.u.y);
        } else if (m.mode == "DT") {
            cues.cue(S.death, ms + std::uint32_t(S.death_del) * 40, m.u.x, m.u.y);
        }
    }
    // What the character wears (the save's appearance, else the class's starting gear).
    [[nodiscard]] const Scene::Appearance& gfx() const {
        return cc.appearance ? *cc.appearance : scene->starting_gear[std::size_t(kUiToSaveClass[std::max(cc.selected, 0)])];
    }
    [[nodiscard]] const Scene::PlayerAnim& player_anim(int mode) const {
        return scene->composite(kUiToSaveClass[std::max(cc.selected, 0)], mode, gfx());
    }
    // A swing takes attack_ticks for the item attack speed and weapon
    // speed; get-hit and block recover faster with FHR / FBR (their
    // effective % on the animation rate).
    // ponytail: FHR / FBR / FCR as rate bonuses, not the class breakpoint
    // tables.
    void set_pmode(int mode, std::uint32_t ms) {
        pmode = mode;
        player.mode_ms = ms;
        player.walking = false;
        player.path.clear();
        const auto& a = player_anim(mode);
        std::uint32_t len = a.length_ms();
        if (attack_mode(mode) && a.frames) {
            const auto ticks = d2d::rules::attack_ticks(int(a.frames), int(a.speed ? a.speed : 256), pf.ias, pf.wsm);
            len = std::uint32_t(ticks) * 40;
        } else if (mode == kModeGH || mode == kModeBL || mode == kModeSC) {
            const int v = mode == kModeGH ? pf.fhr : mode == kModeBL ? pf.fbr : pf.fcr;
            len = len * 100 / std::uint32_t(100 + d2d::rules::effective_speed(v));
        }
        prate = len ? float(a.length_ms()) / float(len) : 1.f;
        pmode_until = mode == kModeDD ? 0 : ms + len;
    }
    [[nodiscard]] bool dead() const { return pmode == kModeDT || pmode == kModeDD; }

    // The player as combat sees them: item stats summed like the char
    // panel's (worn, charms, what's socketed), the weapon and shield worn,
    // the panel's defense and resistances; the passives' stats (`passives`):
    // those with no passiveitype join the sum, a mastery (342..344) only
    // when the weapon is its type, the best one (FUN_00645830), Weapon Block
    // (348) with two claws when either hand is (FUN_0057dca0, class HT2).
    // ponytail: set bonuses and the weapon swap aren't counted; the throw
    // masteries (345..347) wait for thrown weapons.
    [[nodiscard]] d2d::rules::Fighter player_fighter(d2d::rules::Fighter* kick = nullptr,
                                                     const d2d::rules::StatSum* states = nullptr,
                                                     const std::vector<d2d::rules::PassiveStat>* passives = nullptr,
                                                     d2d::rules::StatSum* sum_out = nullptr) const {
        d2d::rules::StatSum sum = states ? *states : d2d::rules::StatSum{}, weapon_sum{};
        const d2d::d2s::Item *weapon = nullptr, *shield = nullptr, *boots = nullptr;
        auto add = [](d2d::rules::StatSum& into, const std::vector<d2d::d2s::ItemProp>& props) {
            for (const auto& p : props) if (p.stat >= 0 && std::size_t(p.stat) < into.size()) into[std::size_t(p.stat)] += p.value;
        };
        for (const auto& it : cc.items) {
            const bool worn = it.location == 1 && it.slot >= 1 && it.slot <= 10;
            const bool charm = it.location == 0 && it.panel == 1 && (it.code == "cm1" || it.code == "cm2" || it.code == "cm3");
            if (!worn && !charm) continue;
            add(sum, it.props);
            for (const auto& j : it.socketed_items) add(sum, socket_props(*scene, it, j));
            if (worn && it.slot == 9) boots = &it;
            if (!worn || (it.slot != 4 && it.slot != 5)) continue;
            const auto b = scene->rules.item_base.find(it.code);
            const auto info = scene->rules.item_info.find(it.code);
            if (b == scene->rules.item_base.end() || info == scene->rules.item_info.end()) continue;
            if (info->second.kind == 2 && (!weapon || it.slot == 4)) weapon = &it;
            if (info->second.kind == 1 && b->second.block > 0) shield = &it;
        }
        if (passives) {
            const auto type_of = [&](const d2d::d2s::Item& it) {
                const auto i = scene->rules.item_info.find(it.code);
                return i == scene->rules.item_info.end() ? std::string{} : i->second.type;
            };
            std::vector<std::string> hands;                  // the types in slots 4 / 5
            for (const auto& it : cc.items)
                if (it.location == 1 && (it.slot == 4 || it.slot == 5)) hands.push_back(type_of(it));
            const auto is = [&](const std::string& t, const std::string& want) { return d2d::rules::type_is(scene->rules, t, want); };
            const bool claws2 = hands.size() == 2 && is(hands[0], "h2h") && is(hands[1], "h2h");
            std::array<std::int64_t, 4> best{};              // 342, 343, 344, 348
            for (const auto& p : *passives) {
                if (p.stat < 0 || std::size_t(p.stat) >= sum.size()) continue;
                if (p.itype.empty()) { sum[std::size_t(p.stat)] += p.value; continue; }
                const std::size_t k = p.stat >= 342 && p.stat <= 344 ? std::size_t(p.stat - 342) : p.stat == 348 ? 3 : 4;
                if (k == 4) continue;
                const bool fits = k < 3 ? weapon && is(type_of(*weapon), p.itype)
                                        : claws2 && std::ranges::any_of(hands, [&](const auto& t) { return is(t, p.itype); });
                if (fits) best[k] = std::max<std::int64_t>(best[k], p.value);
            }
            for (std::size_t k = 0; k < 3; ++k) sum[342 + k] += best[k];
            sum[348] += best[3];
        }
        if (weapon) {                                        // its own enhanced damage (op 13), sockets included
            add(weapon_sum, weapon->props);
            for (const auto& j : weapon->socketed_items) add(weapon_sum, socket_props(*scene, *weapon, j));
        }
        if (sum_out) *sum_out = sum;
        const auto& r = cc.panel.res;                        // panel: fire, cold, lightning, poison
        const auto& gains = scene->class_gains[std::size_t(kUiToSaveClass[std::max(cc.selected, 0)])];
        const std::array<int, 4> res{ int(r[0]), int(r[2]), int(r[1]), int(r[3]) };
        auto f = d2d::rules::make_fighter(scene->rules, weapon, shield, sum, weapon_sum, cc.stats, gains,
                                          int(cc.panel.defense), res, boots);
        if (kick) {                                          // the weapon's stats off, its attack rating kept
            auto bare = sum;
            for (std::size_t i = 0; i < bare.size(); ++i) bare[i] -= weapon_sum[i];
            *kick = d2d::rules::make_fighter(scene->rules, nullptr, shield, bare, {}, cc.stats, gains,
                                             int(cc.panel.defense), res, boots);
            kick->ar_base = f.ar_base; kick->ar_pct = f.ar_pct; kick->ar = f.ar;
            kick->kick_pct = f.kick_pct;
        }
        return f;
    }
    // The player's fighter this frame, with the self states' aurastats
    // (FUN_005c6cc0): skill_armor_percent and armor_override_percent as %
    // of the panel defense (the override after), damageresist to DR %.
    // ponytail: other aurastats aren't read.
    // A friendly aura (srvdofunc 65, FUN_005cf010) puts its aurastate and
    // aurastats on the player (and allies within aurarangecalc subtiles)
    // the same way; its hitpoints (Prayer) heal on each pulse instead.
    // ponytail: resist auras cap at 95, not the panel's max resist; the
    // merc doesn't get them.
    // The self states' aurastats are stats on the player (FUN_005c6cc0):
    // they join the item stats, so toblock (20), damageresist (36) and
    // the rest go through make_fighter; defense % (171 skill_armor_percent,
    // 182 armor_override_percent after it) applies to the panel defense.
    // ponytail: attackrate (68) is taken as IAS and velocitypercent (67) as
    // FRW; other stats make_fighter doesn't read do nothing.
    void update_fighters(std::uint32_t ms) {
        std::erase_if(self_states, [&](const SelfState& st) { return ms >= st.until; });
        d2d::rules::StatSum st_sum{};
        const auto env = calc_env();
        const auto passives = d2d::rules::passive_stats(scene->skills, env);
        cc.panel = panel_stats(*scene, cc.header, cc.items, cc.stats, &passives);
        auto states = self_states;
        if (const auto* a = scene->skills.get(aura); a && a->srvdofunc == 65) states.push_back({ aura, skill_level ? skill_level(aura) : 1, ~0u });
        // A totem's aura (Oak Sage, Heart of Wolverine, Spirit of Barbs: do
        // 65) while the player is within its aurarange of it.
        for (const auto& p : pets)
            if (const auto* a = scene->skills.get(p.aura); a && a->srvdofunc == 65 && p.m.alive() && p.where == level
                && std::hypot(p.m.u.x - player.x, p.m.u.y - player.y) * 5 <= float(calc(*a, a->aurarange, p.aura_level)))
                states.push_back({ p.aura, p.aura_level, ~0u });
        for (const auto& st : states) {
            const auto* s = scene->skills.get(st.skill);
            for (std::size_t i = 0; s && i < s->aurastat.size(); ++i)
                if (const int id = s->aurastat[i]; id >= 0 && id != 6 && std::size_t(id) < st_sum.size())
                    st_sum[std::size_t(id)] += d2d::rules::eval_calc(scene->skills, s->aura_calc[i], env, s->id, st.level);
        }
        pf = player_fighter(&pf_kick, &st_sum, &passives, &psum);
        constexpr int kRes[4] = { 39, 41, 43, 45 };          // Fighter::res order: fire, lightning, cold, poison
        for (std::size_t k = 0; k < 4; ++k)                  // a resist aura's on top of the panel's (Salvation, Resist Fire, ...)
            if (const auto v = st_sum[std::size_t(kRes[k])]; v != 0) pf.res[k] = int(std::min<std::int64_t>(pf.res[k] + v, 95));
        pf.ias += int(st_sum[68]);
        pf.frw += int(st_sum[67]);
        for (const auto& p : passives) if (p.stat == 67 && p.itype.empty()) pf.frw += p.value;   // Increased Speed
        pf.defense += int(pf.defense * st_sum[171] / 100);
        pf.defense = std::max(int(pf.defense + pf.defense * st_sum[182] / 100), 0);
    }

    // What calcs ask of the player (skills.hpp).
    [[nodiscard]] d2d::rules::CalcEnv calc_env() {
        return { skill_base, skill_level, [this](int id) { return id >= 0 && std::size_t(id) < psum.size() ? int(psum[std::size_t(id)]) : 0; },
                 int(cc.stats.get(d2d::d2s::kLevel)), &rng };
    }
    // A swing starts: the skill in use when it's built, paid for from mana
    // (FUN_0056c160's cost); short of mana, a skill with AttackNoMana swings
    // a plain attack instead, any other doesn't swing. Its animation (A1,
    // KK for the kicks) at the swing's speed; Dragon Talon's kick count is
    // calc1 (FUN_005d5970).
    // ponytail: SQ sequence skills (Jab, Fists of Fire) aren't built; no
    // "not enough mana" voice.
    bool start_swing(std::uint32_t ms) {
        using namespace d2d::d2s;
        swing_skill = 0;
        kicks_left = 0;
        const auto* s = scene->skills.get(attack_skill);
        if (s && attack_skill != 0) {
            if (!skill_built(*s)) {
                if (std::ranges::find(told, attack_skill) == told.end()) {
                    told.push_back(attack_skill);
                    d2d::log::info("not implemented: skill {} (srvstfunc {}, srvdofunc {}) - a plain attack for now",
                                   s->name, s->srvstfunc, s->srvdofunc);
                }
            } else {
                const int lvl = skill_level ? skill_level(attack_skill) : 0;
                const int cost = d2d::rules::mana_cost(*s, lvl);
                if (lvl > 0 && cc.stats.v[kMana] >= cost) {
                    cc.stats.v[kMana] -= cost;
                    swing_skill = attack_skill;
                    if (s->srvstfunc == 24 || s->srvstfunc == 37 || s->srvstfunc == 9) {   // Talon's kicks (FUN_005d5970), Zeal's hits (FUN_005daf40)
                        const auto env = calc_env();
                        int n = d2d::rules::eval_calc(scene->skills, s->calc[0], env, s->id, lvl);
                        if (s->srvstfunc == 9) n = std::min(n, int(in_reach().size()));   // Fend (FUN_005dae30): one per enemy in reach (FUN_0056bc80), at most calc1
                        kicks_left = std::max(n, 1) - 1;
                    }
                } else if (!s->attack_no_mana) {
                    attack_mon = -1;                         // can't pay, won't swing
                    return false;
                }
            }
        }
        const auto* used = scene->skills.get(swing_skill);
        set_pmode(swing_mode(), ms);
        start_sequence(ms);
        if (used && swing_skill != 0 && used->srvdofunc == 2 && used->aurastat[0] >= 0) self_state(*used, ms);
        pstruck = false;
        return true;
    }
    // An SQ skill plays its sequence (FUN_00663310: seqnum's frames for
    // the weapon class) at the attack's speed; with no frames for the
    // weapon it swings once.
    // ponytail: the sequence's rate taken as the class's A1 (animdata)
    // through attack_ticks; seqinput / seqtrans aren't read.
    void start_sequence(std::uint32_t ms) {
        seq = {};
        seq_struck = 0;
        const auto* s = scene->skills.get(swing_skill);
        if (!s || swing_skill == 0 || s->anim != "SQ" || s->seqnum <= 0) return;
        const auto& a1 = player_anim(kModeA1);
        std::string wc = a1.name.size() >= 7 ? a1.name.substr(4) : "hth";
        for (auto& c : wc) c = char(std::tolower((unsigned char)c));
        seq = d2d::rules::sequence(s->seqnum, wc);
        if (seq.empty()) return;
        start_move(*s, ms);
        const auto ticks = d2d::rules::attack_ticks(int(seq.size()), int(a1.speed ? a1.speed : 256), pf.ias, pf.wsm);
        pmode_until = ms + std::uint32_t(ticks) * 40;
        seq_frame_ms = std::max<std::uint32_t>(std::uint32_t(ticks) * 40 / std::uint32_t(seq.size()), 1);
        prate = 1.f;
    }
    // The movement a moving skill starts with (its srvstfunc):
    // Whirlwind (FUN_005d8f50) to the target at the class's walk velocity
    // (FUN_0056e5b0: CharStats +0x40), its sequence over and over;
    // Charge (FUN_005cf6b0) at the monster at run velocity × (max(velocity
    // percent, 50) + par1) / 100; Leap Attack (FUN_005da540) waits for its
    // takeoff event.
    // ponytail: velocitypercent taken as its base 100; Whirlwind's path is
    // a straight line (FUN_0064ea90's pathing isn't traced).
    void start_move(const d2d::rules::Skill& s, std::uint32_t ms) {
        smove = {};
        seq_loop = false;
        const auto cls = std::size_t(kUiToSaveClass[std::max(cc.selected, 0)]);
        if (s.srvdofunc == 76) {
            smove = { move_x, move_y, cells_per_sec(float(scene->walk_velocity[cls])), true, false };
            seq_loop = true;
            pmode_until = ~0u;
            whirl_next = ms;
        } else if (s.srvdofunc == 67) {
            smove = { move_x, move_y, cells_per_sec(float(scene->run_velocity[cls])) * float(std::max(100, 50) + s.par[0]) / 100.f, true, false };
            seq_loop = true;                                 // the run starts over until it reaches (FUN_005cf900)
            pmode_until = ~0u;
        }
    }
    // Where the frames stand: the frame index (looping or held on the
    // last) and how many event-1 frames have passed since the start.
    [[nodiscard]] std::size_t seq_at(std::uint32_t ms) const {
        const auto i = std::size_t((ms - player.mode_ms) / seq_frame_ms);
        return seq_loop ? i % seq.size() : std::min(i, seq.size() - 1);
    }
    [[nodiscard]] int seq_events(std::uint32_t ms) const {
        const auto i = std::size_t((ms - player.mode_ms) / seq_frame_ms);
        const auto per = std::ranges::count(seq, 1, &d2d::rules::SeqFrame::event);
        const auto last = seq_loop ? i : std::min(i, seq.size() - 1);
        int n = seq_loop ? int(last / seq.size() * std::size_t(per)) : 0;
        for (std::size_t k = 0; k <= last % (seq_loop ? seq.size() : ~std::size_t{ 0 }) && k < seq.size(); ++k) n += seq[k].event == 1;
        return n;
    }
    // What the player shows in a sequence: its frame's mode, and a start
    // time that lands the renderer (frame = elapsed / ms_per_frame) on it.
    [[nodiscard]] std::pair<int, std::uint32_t> seq_view(std::uint32_t ms) const {
        const auto& f = seq[seq_at(ms)];
        const auto mpf = player_anim(f.mode).ms_per_frame();
        return { f.mode, ms - mpf * f.frame - mpf / 2 };
    }
    // The swing's animation: KK for kicks, S1 for Smite, else A1.
    // ponytail: SQ sequences play A1.
    [[nodiscard]] int swing_mode() const {
        const auto* s = scene->skills.get(swing_skill);
        if (!s || swing_skill == 0) return kModeA1;
        return s->anim == "KK" ? kModeKK : s->anim == "S1" ? kModeS1 : kModeA1;
    }
    // What the swing's skill adds to the blow: the Bash family's toht,
    // calc1 damage %, calc2 damage after, SrcDam, ResultFlags' knockback;
    // a Dragon Talon kick: toht, ln12 damage % (FUN_005d5880), the skill's
    // physical damage, the last kick knocking back (100% on normal
    // monsters, FUN_005d5a30).
    // ponytail: the skills' states (Stun's stun, Concentrate's defense) and
    // calc4's element conversion aren't applied; stat 325's to-hit on kicks
    // isn't added.
    [[nodiscard]] d2d::rules::Swing swing() {
        d2d::rules::Swing sw;
        const auto* s = scene->skills.get(swing_skill);
        if (!s || swing_skill == 0) return sw;
        const int lvl = skill_level ? skill_level(swing_skill) : 1;
        const auto env = calc_env();
        const auto& T = scene->skills;
        sw.ar_pct = d2d::rules::skill_tohit(T, *s, env, lvl);
        if (s->srvstfunc == 23) return sw;                   // a charge-up: a plain hit at its to-hit (FUN_005d3490)
        if (s->srvstfunc == 27) {                            // Dragon Tail: a kick (FUN_005d7090 -> FUN_005d54b0)
            sw.kick = true;
            return sw;
        }
        if (s->srvstfunc == 24) {
            sw.kick = true;
            sw.ed_pct = d2d::rules::calc_ln(s->par[0], s->par[1], lvl);
            sw.skill_lo = d2d::rules::skill_phys(T, *s, env, lvl, false);
            sw.skill_hi = d2d::rules::skill_phys(T, *s, env, lvl, true);
            sw.knockback = kicks_left == 0;
        } else if (s->srvdofunc == 150) {                    // Smite (FUN_005ce9f0): calc1 ED, calc2 stun
            sw.ar_pct = 0;
            sw.smite = true;
            // With Holy Shield up (state 0x65), its damage (MinDam..MaxDam
            // by level) joins the shield's.
            for (const auto& st : self_states)
                if (const auto* h = T.get(st.skill); h && self_cast(*h)) {
                    sw.skill_lo = d2d::rules::skill_phys(T, *h, env, st.level, false);
                    sw.skill_hi = d2d::rules::skill_phys(T, *h, env, st.level, true);
                }
            sw.ed_pct = d2d::rules::eval_calc(T, s->calc[0], env, s->id, lvl);
            sw.stun_ticks = d2d::rules::eval_calc(T, s->calc[1], env, s->id, lvl);
            sw.knockback = (s->result_flags & 8) != 0;
        } else if (s->srvdofunc == 13) {                     // Zeal's hit (FUN_005dbc60): calc2 ED, no ResultFlags
            sw.ed_pct = d2d::rules::eval_calc(T, s->calc[1], env, s->id, lvl);
            if (s->etype >= 0 && s->etype < 5)
                if (const int c = d2d::rules::eval_calc(T, s->calc[3], env, s->id, lvl); c > 0) { sw.conv_type = s->etype; sw.conv_pct = c; }
        } else if (s->srvstfunc == 29) {                     // Sacrifice (FUN_005ce790): calc1 ED on the weapon's physical
            sw.ed_pct = d2d::rules::eval_calc(T, s->calc[0], env, s->id, lvl);
            sw.srcdam = s->srcdam;
            sw.knockback = (s->result_flags & 8) != 0;
        } else if (s->srvstfunc == 35) {                     // Vengeance (FUN_005cfe10)
            sw.fire_pct = d2d::rules::eval_calc(T, s->calc[0], env, s->id, lvl);
            sw.cold_pct = d2d::rules::eval_calc(T, s->calc[1], env, s->id, lvl);
            sw.ltng_pct = d2d::rules::eval_calc(T, s->calc[2], env, s->id, lvl);
            sw.cold_len = d2d::rules::elem_length(T, *s, env, lvl);
        } else {                                             // Bash's family (32), Power Strike (6), Berserk (39)
            // Jab (FUN_005db2d0), Dragon Claw (FUN_005d6200), Frenzy
            // (FUN_005d8b10) and Double Swing (Bash's FUN_005d7ea0) build
            // the same way on each hit.
            // Power Strike's start (FUN_005da940) passes no to-hit bonus and
            // no ResultFlags; only Bash's adds calc2 after the damage
            // (Berserk's calc2 is its state's length).
            if (s->srvstfunc == 6) sw.ar_pct = 0;
            sw.ed_pct = d2d::rules::eval_calc(T, s->calc[0], env, s->id, lvl);
            if (s->srvstfunc == 32 || s->srvdofunc == 70) sw.flat = d2d::rules::eval_calc(T, s->calc[1], env, s->id, lvl);
            sw.srcdam = s->srcdam;
            sw.knockback = (s->srvstfunc != 6 && (s->result_flags & 8) != 0) || s->srvdofunc == 67 || s->srvdofunc == 78;
            // FUN_005d7ea0 on a hit: EType stun stands the target for the
            // skill's elemental length (FUN_0056e0c0 -> FUN_0056c8e0 case
            // 9); another element with calc4 > 0 takes calc4 % of the
            // physical (Concentrate: Berserk's level to magic).
            if (s->etype == 5) sw.stun_ticks = d2d::rules::elem_length(T, *s, env, lvl);
            else if (s->etype >= 0)
                if (const int c = d2d::rules::eval_calc(T, s->calc[3], env, s->id, lvl); c > 0) { sw.conv_type = s->etype; sw.conv_pct = c; }
        }
        return sw;
    }

    // A hit on monster i (the player's or the merc's): blocked, it blocks;
    // otherwise the damage, life/mana leech (the player's), poison and
    // bleeding over time, a chill, a knockback. A kill counts.
    void land(std::size_t i, const d2d::rules::Blow& b, bool by_player, std::uint32_t ms) {
        using namespace d2d::d2s;
        auto& m = monsters[i];
        if (b.blocked) { block_anim(*scene, m, ms); return; }
        if (!b.hit) return;
        if (by_player) {
            cc.stats.v[kLife] = std::min(cc.stats.v[kMaxLife], cc.stats.v[kLife] + (std::int64_t(b.life) << 8));
            cc.stats.v[kMana] = std::min(cc.stats.v[kMaxMana], cc.stats.v[kMana] + (std::int64_t(b.mana) << 8));
        }
        // One poison at a time, the stronger wins (FUN_0057ac50); at 0 life
        // the monster dies to it, the kill the poisoner's (FUN_005a6920).
        if (const double rate = double(b.poison) / (std::max(b.poison_ticks, 1) * 40.0);
            b.poison > 0 && (ms >= m.poison_until || rate >= m.poison_rate)) {
            m.poison_rate = rate;
            m.poison_until = ms + std::uint32_t(b.poison_ticks) * 40;
        }
        if (b.chill_ticks > 0) m.chill_until = ms + std::uint32_t(b.chill_ticks) * 40;
        // Stunned (state 21, FUN_0057aae0): it stands until the stun ends, a
        // new stun resetting the length.
        // ponytail: its guards aren't applied: special monsters' 90 % to
        // shrug it off (FUN_005a0180), the MonStats flag immunity and the
        // act bosses' 13-frame cap; the item stun length (stat 66) isn't added.
        if (b.stun_ticks > 0) m.stun_until = ms + std::uint32_t(b.stun_ticks) * 40;
        if (b.bleed) {
            m.bleed_rate = d2d::rules::open_wounds_per_sec(int(cc.stats.get(kLevel))) / 1000.0;
            m.bleed_until = ms + 8000;
        }
        if (hurt(*scene, m, b.damage, ms)) { killed(i, ms); return; }
        if (b.knockback) {                                   // a step straight back, if there's room
            const float dx = m.u.x - player.x, dy = m.u.y - player.y, d = std::max(std::hypot(dx, dy), 0.01f);
            const float nx = m.u.x + dx / d * 0.6f, ny = m.u.y + dy / d * 0.6f;
            if (!level->unit_blocked(nx, ny)) { m.u.x = nx; m.u.y = ny; }
        }
    }

    // Poison and bleeding tick on the monsters near the player.
    void monster_dots(std::uint32_t ms, float dt) {
        for (std::size_t i = 0; i < monsters.size(); ++i) {
            auto& m = monsters[i];
            if (!m.alive() || (ms >= m.poison_until && ms >= m.bleed_until)) continue;
            m.dot_acc += ((ms < m.poison_until ? m.poison_rate : 0) + (ms < m.bleed_until ? m.bleed_rate : 0)) * dt * 1000;
            const int whole = int(m.dot_acc);
            if (whole <= 0) continue;
            m.dot_acc -= whole;
            m.hp -= whole;
            if (!m.alive()) { set_mode(*scene, m, "DT", ms); killed(i, ms); }
        }
    }

    // The player's swing: the hit lands on the attack's event frame (at the
    // swing's own speed) — player_blow: hit chance, the monster's block,
    // deadly strike, resistances, elemental damage, crushing blow, leech.
    // The swing's hits: a sequence's on each of its event-1 frames, else
    // one on the attack's event frame.
    void strike(std::uint32_t ms) {
        if (const auto* s = scene->skills.get(swing_skill); s && missile_skill(*s)) {   // Magic Arrow: the bow's action frame
            if (!pstruck && ms >= player.mode_ms + std::uint32_t(float(player_anim(pmode).action_ms()) / prate)) { pstruck = true; fire(*s, ms); }
            return;
        }
        if (!seq.empty()) {
            const int due = seq_events(ms);
            while (seq_struck < due) {
                ++seq_struck;
                // Frenzy's and Double Swing's second hand looks for another
                // target (FUN_0056bd10 on the odd frame, FUN_005d8e00 /
                // FUN_005d8470).
                if (const auto* s = scene->skills.get(swing_skill); s && seq_struck % 2 == 0 && (s->srvdofunc == 9 || s->srvdofunc == 70))
                    other_target();
                if (const auto* s = scene->skills.get(swing_skill); s && moving_skill(*s)) move_event(*s, ms);
                else hit(ms);
            }
            return;
        }
        if (pstruck || attack_mon < 0
            || ms < player.mode_ms + std::uint32_t(float(player_anim(pmode).action_ms()) / prate)) return;
        pstruck = true;
        hit(ms);
    }
    void hit(std::uint32_t ms, float reach = kMeleeReach + 0.5f) {
        if (attack_mon < 0) return;
        auto& m = monsters[std::size_t(attack_mon)];
        if (!m.alive() || std::hypot(m.u.x - player.x, m.u.y - player.y) > reach) return;
        auto sw = swing();
        auto f = sw.kick ? pf_kick : pf;
        const auto* s = scene->skills.get(swing_skill);
        const bool charging = s && s->srvstfunc == 23, finishing = finisher(s);
        std::erase_if(charges, [&](const Charge& c) { return ms >= c.until; });
        if (finishing) add_charges(f, sw);
        if (s && s->srvstfunc != 35 && std::ranges::contains(std::array{ 2, 13, 64, 7, 46, 9, 70, 76, 67, 78, 32 }, s->srvdofunc)) skill_element(f, *s);
        const int hp_before = m.hp;
        const auto target = std::size_t(attack_mon);
        const auto b = d2d::rules::player_blow(f, target_of(target), int(cc.stats.get(d2d::d2s::kLevel)), rng, sw);
        land(target, b, true, ms);
        if (b.hit && charging) charge(*s, ms);
        if (b.hit && finishing) release(target, ms);
        if (b.hit && s && s->srvdofunc == 50) dragon_tail(*s, target, b.phys, ms);
        if (b.hit && s && s->srvdofunc == 9) frenzy(*s, ms);
        if (b.hit && s && s->srvstfunc == 7) impale_wear(*s);
        // Sacrifice's price (FUN_005ce8e0): calc2 % of the physical dealt,
        // no more than the target had left, off the player's life.
        if (b.hit && s && s->srvdofunc == 64) {
            const int dealt = std::min(d2d::rules::resisted(b.phys, target_of(target).res[0]), std::max(hp_before, 0));
            const auto env = calc_env();
            self_hurt += (std::int64_t(dealt) << 8) * d2d::rules::eval_calc(scene->skills, s->calc[1], env, s->id,
                                                                            skill_level ? skill_level(s->id) : 1) / 100;
        }
        if (!monsters[target].alive()) attack_mon = -1;
    }

    // The skill's own element on its hit (FUN_0056e0c0: EMin..EMax with
    // brackets, synergy and the element's mastery (flag 1); Power Strike's
    // lightning, Poison Dagger's poison). Stun is elsewhere.
    void skill_element(d2d::rules::Fighter& f, const d2d::rules::Skill& s) {
        if (s.etype < 0 || s.etype >= 5) return;
        const int lvl = skill_level ? skill_level(s.id) : 1;
        const auto env = calc_env();
        auto& [lo, hi] = f.elem[std::size_t(s.etype)];
        const int len = d2d::rules::elem_length(scene->skills, s, env, lvl);
        const std::int64_t elo = d2d::rules::elem_damage(scene->skills, s, env, lvl, false, 0, true),
                           ehi = d2d::rules::elem_damage(scene->skills, s, env, lvl, true, 0, true);
        if (s.etype == 3) {                                  // poison: 256ths a tick over its length
            lo += int(elo * std::max(len, 1) >> 8); hi += int(ehi * std::max(len, 1) >> 8);
            f.poison_len = std::max(f.poison_len, len);
            return;
        }
        lo += int(elo >> 8); hi += int(ehi >> 8);
        if (s.etype == 2) f.cold_len = std::max(f.cold_len, len);
    }
    void self_state(const d2d::rules::Skill& s, std::uint32_t ms) {
        const int lvl = skill_level ? skill_level(s.id) : 1;
        std::uint32_t until = pmode_until;
        if (s.srvstfunc == 39) {
            const auto env = calc_env();
            const int ticks = d2d::rules::eval_calc(scene->skills, s.calc[1], env, s.id, lvl);
            until = ms + std::uint32_t(ticks > 0 ? ticks : 10) * 40;
        }
        std::erase_if(self_states, [&](const SelfState& st) { return st.skill == s.id; });
        self_states.push_back({ s.id, lvl, until });
    }

    // A charge-up's hit lands: one more charge (up to 3), for auralencalc
    // ticks more (FUN_005d3320).
    void charge(const d2d::rules::Skill& s, std::uint32_t ms) {
        const int lvl = skill_level ? skill_level(s.id) : 1;
        auto it = std::ranges::find(charges, s.id, &Charge::skill);
        if (it == charges.end()) it = charges.insert(charges.end(), Charge{ s.id });
        it->level = std::max(it->level, lvl);
        it->count = std::min(it->count + 1, 3);
        const auto env = calc_env();
        it->until = ms + std::uint32_t(std::max(d2d::rules::eval_calc(scene->skills, s.auralen, env, s.id, lvl), 1)) * 40;
    }
    // What the charges add to a finishing blow (FUN_005d3ba0 / FUN_005d3ac0,
    // at the higher of the stored level and today's).
    // ponytail: aurastat2's progressive_tohit (par4) isn't given.
    void add_charges(d2d::rules::Fighter& f, d2d::rules::Swing& sw) {
        const auto env = calc_env();
        for (const auto& c : charges) {
            const auto* s = scene->skills.get(c.skill);
            if (!s) continue;
            const int lvl = std::max(c.level, skill_level ? skill_level(c.skill) : 0);
            const auto cb = d2d::rules::charge_bonus(scene->skills, *s, env, lvl, c.count);
            sw.ed_pct += cb.ed_pct;
            f.life_steal += cb.life_steal;
            f.mana_steal += cb.mana_steal;
            if (cb.etype >= 0 && cb.etype < 5) {
                auto& [lo, hi] = f.elem[std::size_t(cb.etype)];
                lo += cb.elem_lo; hi += cb.elem_hi;
                if (cb.etype == 2) f.cold_len = std::max(f.cold_len, cb.elem_len);
            }
        }
    }
    // A row with no Skill: its own element at level lvl, the weapon at its
    // SrcDamage.
    [[nodiscard]] static d2d::rules::MissileDamage row_damage(const Scene::MissileInfo& mi, int lvl) {
        auto md = d2d::rules::row_damage(mi.etype, mi.emin, mi.emax, mi.emin_lev, mi.emax_lev, mi.hitshift, mi.elen, mi.elen_lev, lvl);
        md.srcdam = mi.src_damage;
        return md;
    }
    // A finishing hit on monster `on` releases every charge (FUN_005d5220):
    // with n charges (1..3) the skill's srvprgfunc n runs — prgstack skills
    // (Fists of Fire, Claws of Thunder, Blades of Ice) run 1..n, each with
    // the count set to its own — at the charges' level or the skill's now,
    // whichever is higher; then the charges are gone. d2d runs:
    //   38 (FUN_005d3e80): the skill's physical and element (FUN_0056e170,
    //      FUN_0056e0c0) on every monster within prgcalc n subtiles of it;
    //   36 (FUN_005d4db0): a nova of the count's missile round it
    //      (FUN_0056d400), its range + calc1;
    //   39 (FUN_005d3f90): prgcalc n squared tries at points within that
    //      many subtiles, each a missile of the count's (fire, ice cubes).
    // The count's missile (FUN_005d3cf0): 1 srvmissilea, 2 b, 3 c.
    // ponytail: 37 (Claws of Thunder's bolts, FUN_005d4150), 40 / 41 / 143
    // (Royal Strike, Fists of Fire's first) are logged once; 39's points
    // come from d2d's rng.
    void release(std::size_t on, std::uint32_t ms) {
        const float tx = monsters[on].u.x, ty = monsters[on].u.y;
        const auto held = charges;
        charges.clear();
        for (const auto& c : held) {
            const auto* s = scene->skills.get(c.skill);
            if (!s) continue;
            const int lvl = std::max(c.level, skill_level ? skill_level(s->id) : 0);
            const int n = std::clamp(c.count, 1, 3);
            for (int k = s->prgstack ? 1 : n; k <= n; ++k) prg(*s, s->prgfunc[std::size_t(k - 1)], k, lvl, tx, ty, ms);
        }
    }
    void prg(const d2d::rules::Skill& s, int func, int count, int lvl, float tx, float ty, std::uint32_t ms) {
        if (func <= 0) return;
        const auto env = calc_env();
        const auto& mname = count <= 1 ? s.srvmissilea : count == 2 ? s.srvmissileb : s.srvmissilec;
        const auto mit = scene->missiles.find(mname);
        const Scene::MissileInfo* mi = mit == scene->missiles.end() ? nullptr : &mit->second;
        int r = d2d::rules::eval_calc(scene->skills, s.prgcalc[std::size_t(count - 1)], env, s.id, lvl);
        if (r == 0) r = d2d::rules::eval_calc(scene->skills, s.prgcalc[0], env, s.id, lvl);
        if (func == 38) {
            d2d::rules::MissileDamage md = d2d::rules::missile_damage(scene->skills, s, env, lvl);
            md.srcdam = 0;
            const std::array<int, 4> pierce{ int(psum[333]), int(psum[334]), int(psum[335]), int(psum[336]) };
            for (std::size_t i = 0; i < monsters.size(); ++i)
                if (monsters[i].alive() && std::hypot(monsters[i].u.x - tx, monsters[i].u.y - ty) * 5 <= float(std::max(r, 1)))
                    land(i, d2d::rules::missile_blow(md, target_of(i), pierce, rng), true, ms);
            return;
        }
        if ((func == 36 || func == 39) && mi) {
            auto put = [&](float x, float y, float vx, float vy, int range) {
                Missile a{ mi, x, y, vx, vy, direction32(vx, vy), ms, ms + std::uint32_t(std::max(range, 1)) * 40, {} };
                a.friendly = true; a.skill = s.id; a.level = lvl;
                missiles.push_back(a);
                return missiles.size() - 1;
            };
            if (func == 36) {
                const auto shared = std::make_shared<std::vector<int>>();
                const float v = cells_per_sec(float(mi->vel));
                const int range = mi->range + mi->lev_range * lvl + d2d::rules::eval_calc(scene->skills, s.calc[0], env, s.id, lvl);
                for (int k = 0; k < 64; ++k) {
                    const float a = float(k) * 2 * 3.14159265f / 64;
                    missiles[put(tx, ty, std::cos(a) * v, std::sin(a) * v, range)].struck = shared;
                }
            } else {
                for (int k = 0; k < r * r; ++k) {
                    const int ox = r - int(rng(2 * r + 1)), oy = r - int(rng(2 * r + 1));
                    if (ox * ox + oy * oy > r * r) continue;
                    const float x = tx + float(ox) / 5, y = ty + float(oy) / 5;
                    if (!level->blocked(x, y, 0x04)) put(x, y, 0, 0, mi->range + mi->lev_range * lvl);
                }
            }
            return;
        }
        if (std::ranges::find(told, -1000 - func) != told.end()) return;
        told.push_back(-1000 - func);
        d2d::log::info("not implemented: {}'s release srvprgfunc {} ({} charges)", s.name, func, count);
    }
    // Dragon Tail's kick hit: fire, (calc1 + fire mastery) % of the kick's
    // physical damage, on every monster within aurarangecalc subtiles of
    // the target, less fire resistance (FUN_005d7180 -> FUN_0056bad0).
    // ponytail: fire mastery (stat 329) isn't summed into the fighter; the
    // target is taken to be in the blast.
    void dragon_tail(const d2d::rules::Skill& s, std::size_t target, int phys, std::uint32_t ms) {
        const int lvl = skill_level ? skill_level(s.id) : 1;
        const auto env = calc_env();
        const int fire = phys * d2d::rules::eval_calc(scene->skills, s.calc[0], env, s.id, lvl) / 100;
        const float r = float(d2d::rules::eval_calc(scene->skills, s.aurarange, env, s.id, lvl));
        const float cx = monsters[target].u.x, cy = monsters[target].u.y;
        for (std::size_t i = 0; i < monsters.size() && fire > 0; ++i) {
            auto& m = monsters[i];
            if (!m.alive() || std::hypot(m.u.x - cx, m.u.y - cy) > r) continue;
            if (hurt(*scene, m, d2d::rules::resisted(fire, target_of(i).res[2]), ms)) killed(i, ms);
        }
    }

    // Monster i died (the player's or the merc's doing): the player gets the
    // experience, its pack may scatter, it drops its loot.
    // ponytail: the merc's own experience share isn't kept.
    void killed(std::size_t i, std::uint32_t ms) {
        const auto& m = monsters[i];
        const auto sc = std::size_t(kUiToSaveClass[std::max(cc.selected, 0)]);
        const auto exp = d2d::rules::kill_exp(m.st.exp, int(cc.stats.get(d2d::d2s::kLevel)), m.st.level);
        const int up = d2d::rules::gain_exp(cc.stats, exp, scene->exp_next, scene->class_gains[sc]);
        d2d::log::info("killed {} (+{} exp){}", m.npc.name, exp, up ? std::format(", level {}", cc.stats.get(d2d::d2s::kLevel)) : "");
        if (up) cc.panel = panel_stats(*scene, cc.header, cc.items, cc.stats);
        fallen_scatter(*scene, monsters, i, rng, ms);
        loot.drop(m, ms);
    }

    // The merc as a fighter: its hireling damage, attack rating, defense.
    [[nodiscard]] d2d::rules::Fighter merc_fighter() const {
        return d2d::rules::simple_fighter(merc_st.dmg_min, merc_st.dmg_max, merc_st.ar, merc_st.def);
    }

    // The merc's turn: it goes for the nearest monster within 6 cells of the
    // player that has noticed them (or is within 3 of the merc), strikes in
    // melee — an Act 1 rogue shoots arrows (Missiles.txt arrow) from up to
    // 6 cells — and otherwise follows. Hits use its attack rating against
    // the monster's defense and its damage. Killed, it plays its death and
    // is gone (the save's merc is dead until resurrected).
    // ponytail: mercs' skills and the Hireable AI aren't traced; the rogue's
    // bow is assumed, other mercs fight in melee.
    void merc_turn(std::uint32_t ms, float dt, const Crowd& crowd) {
        auto& u = *merc;
        const auto set = [&](std::string_view mode) {
            merc_mode = mode; u.mode_ms = ms; u.walking = mode == "WL";
            u.path.clear();
            merc_until = mode == "NU" || mode == "WL" ? 0 : ms + scene->npc_anim(*merc_npc, mode).length_ms();
        };
        if (merc_mode == "DT") {
            if (ms >= merc_until) { merc.reset(); cc.header.merc_dead = true; d2d::log::info("the merc died"); }
            return;
        }
        if (merc_mode == "GH") { if (ms < merc_until) return; set("NU"); }
        const bool archer = merc_npc && merc_npc->id == "roguehire";
        const float reach = archer ? 6.f : kMeleeReach;
        if (merc_mode == "A1") {
            if (!merc_struck && merc_target >= 0 && ms >= u.mode_ms + scene->npc_anim(*merc_npc, "A1").action_ms()) {
                merc_struck = true;
                auto& m = monsters[std::size_t(merc_target)];
                const float dx = m.u.x - u.x, dy = m.u.y - u.y, d = std::max(std::hypot(dx, dy), 0.01f);
                if (archer && scene->missiles.contains("arrow")) {
                    const auto& mi = scene->missiles.at("arrow");
                    const float v = cells_per_sec(float(mi.vel));
                    Missile a{ &mi, u.x, u.y, dx / d * v, dy / d * v, direction32(dx, dy), ms, ms + std::uint32_t(mi.range) * 40, {} };
                    a.min = merc_st.dmg_min; a.max = merc_st.dmg_max; a.ar = merc_st.ar; a.level = merc_st.level; a.friendly = true;
                    missiles.push_back(a);
                } else if (m.alive() && d <= kMeleeReach + 0.3f) {
                    land(std::size_t(merc_target), d2d::rules::player_blow(merc_fighter(), target_of(std::size_t(merc_target)), merc_st.level, rng), false, ms);
                }
            }
            if (ms < merc_until) return;
            set("NU");
        }
        // Pick a target.
        if (merc_target >= 0 && !monsters[std::size_t(merc_target)].alive()) merc_target = -1;
        if (merc_target < 0) {
            float best = 1e9f;
            for (std::size_t i = 0; i < monsters.size(); ++i) {
                const auto& m = monsters[i];
                if (!m.alive()) continue;
                const float dp = std::hypot(m.u.x - player.x, m.u.y - player.y), dm = std::hypot(m.u.x - u.x, m.u.y - u.y);
                if (((m.aware && dp < 6) || dm < 3) && dm < best) { best = dm; merc_target = int(i); }
            }
        }
        const auto sc = std::size_t(kUiToSaveClass[std::max(cc.selected, 0)]);
        const float speed = cells_per_sec(float(scene->run_velocity[sc])) * 1.1f;
        if (merc_target >= 0 && std::hypot(u.x - player.x, u.y - player.y) < 10) {
            const auto& m = monsters[std::size_t(merc_target)];
            const float dx = m.u.x - u.x, dy = m.u.y - u.y, d = std::hypot(dx, dy);
            if (d <= reach) {
                u.dir = direction16(dx, dy);
                set("A1");
                merc_struck = false;
                return;
            }
            if (merc_mode != "WL") set("WL");
            if (u.path.empty() || std::hypot(u.goal_x - m.u.x, u.goal_y - m.u.y) > 1.f) {
                u.path = walk_path(*level, u.x, u.y, m.u.x, m.u.y, crowd, &u);
                u.goal_x = m.u.x; u.goal_y = m.u.y;
            }
            if (!follow_path(*level, u, speed * dt, crowd)) merc_target = -1;
            return;
        }
        merc_target = -1;
        merc_follow(*level, u, player.x, player.y, speed, ms, dt, crowd);
        merc_mode = u.walking ? "WL" : "NU";
    }

    // The player's combat modes this frame: dead, the death plays out (a
    // click respawns: true asks Town for it); a swing strikes, and ends —
    // held on the monster, the next follows; a flinch or block ends.
    bool player_modes(const Mouse& mouse, std::uint32_t ms, float dt) {
        if (dead()) {
            if (pmode == kModeDT && ms >= pmode_until) set_pmode(kModeDD, ms);
            return pmode == kModeDD && mouse.press_this_frame;
        }
        if (attack_mode(pmode)) {
            strike(ms);
            if (smove.on) skill_step(ms, dt);
            if (ms >= pmode_until) {
                if (kicks_left > 0 && next_target()) {
                    --kicks_left;                            // Talon's next kick (FUN_005d5a30), Zeal's next hit (FUN_005dbc60)
                    set_pmode(swing_mode(), ms);
                    pstruck = false;
                    return false;
                }
                kicks_left = 0;
                seq = {};
                smove = {};
                seq_loop = false;
                pmode = -1; player.mode_ms = ms;
                if (!mouse.down) attack_mon = -1;
            }
        } else if (pmode == kModeSC) {                       // a self cast: its state on the action frame
            if (!pstruck && ms >= player.mode_ms + player_anim(kModeSC).action_ms()) {
                pstruck = true;
                if (const auto* s = scene->skills.get(swing_skill); s && missile_skill(*s)) {
                    fire(*s, ms);
                } else if (s && summon_skill(*s)) {
                    summon(*s, ms);
                } else if (s) {
                    const int lvl = skill_level ? skill_level(s->id) : 1;
                    const auto env = calc_env();
                    std::erase_if(self_states, [&](const SelfState& st) { return st.skill == s->id; });
                    self_states.push_back({ s->id, lvl, ms + std::uint32_t(std::max(d2d::rules::eval_calc(scene->skills, s->auralen, env, s->id, lvl), 1)) * 40 });
                }
            }
            if (ms >= pmode_until) { pmode = -1; player.mode_ms = ms; if (!mouse.down) attack_mon = -1; }
        } else if ((pmode == kModeGH || pmode == kModeBL) && ms >= pmode_until) {
            pmode = -1; player.mode_ms = ms;
        }
        return false;
    }
    // A step of a moving skill. Whirlwind ends where it was sent (or at a
    // wall); Charge follows its monster; a leap flies over whatever's
    // below and lands on its landing event.
    void skill_step(std::uint32_t ms, float dt) {
        const auto* s = scene->skills.get(swing_skill);
        if (s && s->srvdofunc == 67 && attack_mon >= 0) {
            smove.tx = monsters[std::size_t(attack_mon)].u.x; smove.ty = monsters[std::size_t(attack_mon)].u.y;
        }
        const float dx = smove.tx - player.x, dy = smove.ty - player.y, d = std::hypot(dx, dy);
        const bool whirl = s && s->srvdofunc == 76;
        if (d < 0.05f || (!whirl && !smove.fly && d <= kMeleeReach)) { if (whirl) pmode_until = ms; return; }
        player.dir = direction16(dx, dy);
        const float step = std::min(smove.speed * dt, d), nx = player.x + dx / d * step, ny = player.y + dy / d * step;
        if (!smove.fly && level->unit_blocked(nx, ny)) { if (whirl) pmode_until = ms; return; }
        player.x = nx; player.y = ny;
    }
    // A moving skill's event frame (its srvdofunc):
    // Whirlwind (FUN_005d9580): when its hit timer allows (FUN_005d9320:
    // every 4..16 frames by the attack's speed), one hit (two with two
    // weapons) on the next enemy within 5 (round by id);
    // Charge (FUN_005cf900): running, the monster in reach jumps the
    // sequence to its attack; the attack's event hits (knockback);
    // Leap Attack (FUN_005da7e0): takeoff, landing beside the monster, the
    // strike (FUN_005da660: knockback, a stun on it ends).
    // ponytail: FUN_0062a710's attack frames taken as attack_ticks of the
    // class's A1; FUN_005d92d0 (two weapons) as a weapon in each hand.
    void move_event(const d2d::rules::Skill& s, std::uint32_t ms) {
        if (s.srvdofunc == 76) {
            if (ms < whirl_next) return;
            const auto& a1 = player_anim(kModeA1);
            const int f = d2d::rules::attack_ticks(int(a1.frames ? a1.frames : 16), int(a1.speed ? a1.speed : 256), pf.ias, pf.wsm);
            whirl_next = ms + std::uint32_t(d2d::rules::whirlwind_gap(f)) * 40;
            const int hands = std::ranges::count_if(cc.items, [&](const d2d::d2s::Item& it) {
                return it.location == 1 && (it.slot == 4 || it.slot == 5) && scene->rules.item_info.contains(it.code)
                    && scene->rules.item_info.at(it.code).kind == 2; });
            for (int h = 0; h < (hands >= 2 ? 2 : 1); ++h) {
                std::vector<int> near;
                for (std::size_t i = 0; i < monsters.size(); ++i)
                    if (monsters[i].alive() && std::hypot(monsters[i].u.x - player.x, monsters[i].u.y - player.y) <= 5.f) near.push_back(int(i));
                if (near.empty()) return;
                const auto nx = std::ranges::upper_bound(near, attack_mon);
                attack_mon = nx != near.end() ? *nx : near.front();
                hit(ms, 5.f);
            }
            return;
        }
        if (s.srvdofunc == 67) {
            const auto i = seq_at(ms);
            const auto run_end = std::size_t(std::ranges::find_if(seq, [](const auto& f) { return f.mode != kModeRN; }) - seq.begin());
            if (i < run_end) {                               // running: in reach, on to the attack frames
                if (attack_mon >= 0 && std::hypot(monsters[std::size_t(attack_mon)].u.x - player.x,
                                                  monsters[std::size_t(attack_mon)].u.y - player.y) <= kMeleeReach + 0.3f) {
                    seq_loop = false;
                    smove.on = false;
                    player.mode_ms = ms - std::uint32_t(run_end) * seq_frame_ms;
                    seq_struck = seq_events(ms);
                    pmode_until = ms + std::uint32_t(seq.size() - run_end) * seq_frame_ms;
                }
                return;
            }
            hit(ms);
            return;
        }
        // Leap Attack: the first event takes off, the second lands, the third strikes.
        if (seq_struck == 1 && attack_mon >= 0) {
            const auto& m = monsters[std::size_t(attack_mon)].u;
            const float dx = m.x - player.x, dy = m.y - player.y, d = std::max(std::hypot(dx, dy), 0.01f);
            const float land = std::max(d - kMeleeReach * 0.8f, 0.f);
            std::uint32_t air = seq_frame_ms;
            for (std::size_t k = seq_at(ms) + 1; k < seq.size() && seq[k].event != 1; ++k) air += seq_frame_ms;
            smove = { player.x + dx / d * land, player.y + dy / d * land, land / (float(air) / 1000.f), land > 0.f, true };
            player.dir = direction16(dx, dy);
        } else if (seq_struck == 2) {
            if (smove.on) { player.x = smove.tx; player.y = smove.ty; }
            smove = {};
        } else if (seq_struck >= 3) {
            hit(ms);
        }
    }
    // FUN_0056bd10 handed the last target: the enemy in reach with the next
    // higher unit id, else the lowest (round the ring; the same one when
    // it's alone).
    void other_target() {
        const auto r = in_reach();
        if (r.empty()) { attack_mon = -1; return; }
        const auto next = std::ranges::upper_bound(r, attack_mon);
        attack_mon = next != r.end() ? *next : r.front();
        player.dir = direction16(monsters[std::size_t(attack_mon)].u.x - player.x, monsters[std::size_t(attack_mon)].u.y - player.y);
    }
    // Impale's price (FUN_005daa40): calc2 % of the time the weapon loses
    // calc3 durability (stat 72); a throwing weapon (FUN_006289f0) one of
    // its quantity (stat 70, FUN_0056c3f0) instead.
    // ponytail: at 0 durability it should break (FUN_0055f850); it just
    // stays at 0.
    void impale_wear(const d2d::rules::Skill& s) {
        const int lvl = skill_level ? skill_level(s.id) : 1;
        const auto env = calc_env();
        if (int(rng(100)) >= d2d::rules::eval_calc(scene->skills, s.calc[1], env, s.id, lvl)) return;
        for (auto& it : cc.items)
            if (it.location == 1 && (it.slot == 4 || it.slot == 5) && scene->rules.item_info.contains(it.code)
                && scene->rules.item_info.at(it.code).kind == 2) {
                const auto b = scene->rules.item_base.find(it.code);
                if (b != scene->rules.item_base.end() && b->second.stackable) it.quantity = std::max(int(it.quantity) - 1, 0);
                else if (it.max_durability > 0 && !d2d::rules::indestructible(it))
                    it.durability = std::max(int(it.durability) - d2d::rules::eval_calc(scene->skills, s.calc[2], env, s.id, lvl), 0);
                return;
            }
    }
    // The missile a skill fires, when d2d builds it: its srvmissile after
    // its do (FUN_0056f7f0 with no srvdofunc: FUN_0056ecb0 / FUN_0056ee90
    // -> FUN_0059fa30, from the caster toward the target; srvstfunc 4,
    // FUN_005da8b0, only checks the ammo), or its srvmissilea from its do:
    // 8 (a fan), 17 (Charged Bolt), 22 (a nova), 10 (Guided Arrow, Bone
    // Spirit), 12 (Strafe), 26 (Chain Lightning), 28 (Meteor, Blizzard,
    // Eruption), 24 (Fire Wall), 19 (Inferno, Arctic Blast). The row carries
    // the skill's damage (Skill) or none (the weapon's at SrcDamage:
    // Multiple Shot, Strafe), and hits with no function, 1 (the explosion),
    // 10 (guided), 12 (chain) or 14 (Meteor's landing).
    // `any_owner`: a trap's shot, whose row names the player's skill.
    [[nodiscard]] const Scene::MissileInfo* skill_missile(const d2d::rules::Skill& s, bool any_owner = false) const {
        static constexpr int kDo[] = { 8, 17, 22, 10, 12, 26, 28, 24, 19 };
        static constexpr int kSt[] = { 0, 4, 8, 11 };
        const bool plain = (s.srvstfunc == 0 || s.srvstfunc == 4) && s.srvdofunc == 0;
        const bool multi = std::ranges::contains(kSt, s.srvstfunc) && std::ranges::contains(kDo, s.srvdofunc);
        const auto& name = plain ? s.srvmissile : s.srvmissilea;
        if ((!plain && !multi) || name.empty()) return nullptr;
        const auto m = scene->missiles.find(name);
        static constexpr int kHit[] = { 0, 1, 10, 12, 14 };
        if (m == scene->missiles.end() || !std::ranges::contains(kHit, m->second.hit_func)) return nullptr;
        return any_owner || m->second.skill == s.name || m->second.skill.empty() ? &m->second : nullptr;
    }
    [[nodiscard]] bool missile_skill(const d2d::rules::Skill& s) const { return skill_missile(s) != nullptr; }
    // Casting one at (tx, ty): its mana, then its animation (A1 at the
    // attack's speed for the bow skills, else SC with FCR); the missiles
    // leave on the action frame (fire).
    // ponytail: the sequence skills (Lightning's SQ) cast as SC; the delay
    // (+400) isn't kept; FCR as a rate bonus, not the breakpoint table; no
    // ammo is used up.
    bool cast_missile(int skill, float tx, float ty, std::uint32_t ms) {
        using namespace d2d::d2s;
        const auto* s = scene->skills.get(skill);
        if (!s || dead() || pmode >= 0 || !missile_skill(*s)) return false;
        const int lvl = skill_level ? skill_level(skill) : 0;
        const int cost = d2d::rules::mana_cost(*s, lvl);
        if (lvl <= 0 || cc.stats.v[kMana] < cost) { attack_mon = -1; return false; }
        cc.stats.v[kMana] -= cost;
        swing_skill = skill;
        cast_x = tx; cast_y = ty;
        player.dir = direction16(tx - player.x, ty - player.y);
        set_pmode(s->anim == "A1" || s->anim == "TH" ? kModeA1 : kModeSC, ms);
        pstruck = false;
        return true;
    }
    // A skill missile of row `mi` from (x, y) toward (x + dx, y + dy) for
    // `range` ticks (0 velocity: it stays put).
    Missile& launch(const Scene::MissileInfo& mi, const d2d::rules::Skill& s, int lvl, float x, float y, float dx, float dy,
                    int range, std::uint32_t ms) {
        const float v = cells_per_sec(float(mi.vel)), d = std::max(std::hypot(dx, dy), 0.01f);
        Missile a{ &mi, x, y, dx / d * v, dy / d * v, direction32(dx, dy), ms, ms + std::uint32_t(std::max(range, 1)) * 40, {} };
        a.friendly = true; a.skill = s.id; a.level = lvl;
        pending.push_back(a);
        return pending.back();
    }
    [[nodiscard]] int calc(const d2d::rules::Skill& s, const d2d::rules::Calc& c, int lvl) {
        return d2d::rules::eval_calc(scene->skills, c, calc_env(), s.id, lvl);
    }
    // The missiles leave the caster at their velocity for Range + LevRange x
    // level ticks: one toward the cast point; do 8 (FUN_005db410) calc1 of
    // them at points a step apart across the line to it (the step: the
    // line's direction turned a right angle and halved to at most ~1.7
    // subtiles, FUN_0056d3b0), centred on it; do 17 (FUN_005c9300) calc1
    // toward it, each wandering (FUN_005c9290); do 22 (FUN_005c9b50 ->
    // FUN_0056d400) 64 all round, for Range + calc1 ticks.
    // Do 10 (FUN_005db6d0: Guided Arrow, Bone Spirit): one at the monster
    // clicked, which it seeks (missile do 7); calc1 is its damage %
    // (stat 25 through the callback 0x5db6a0). Do 12 (FUN_005dba40,
    // Strafe): st 8 (FUN_005dacd0) counts the monsters within aurarange
    // and clamps that between min(calc3, calc1) and calc1; each do then
    // shoots one at the next of them (FUN_0056bd10), calc2 its damage %.
    // Do 26 (FUN_005ca1b0, Chain Lightning): one carrying calc1 hops. Do 28
    // (FUN_005ca3e0 -> FUN_0056ede0: Meteor, Blizzard, Eruption): the row
    // at the cast point. Do 24 (FUN_005c9ea0, Fire Wall): from the target
    // two makers out across the line from the caster, and srvmissileb on
    // it. Do 19 (FUN_005c8ca0, Inferno): a flame of calc1 ticks per frame
    // while the cast lasts.
    // ponytail: Charged Bolt's wander is a random spread of ±40 degrees, not
    // FUN_005c9290's path; the nova's 64 directions are even angles, not
    // the tables at 0x6e1288 / 0x6e1388; Strafe's arrows go out on a timer
    // (3 ticks apart), not a repeated attack animation; Inferno's channel
    // is its cast's animation (no held button, no mana per frame).
    void fire(const d2d::rules::Skill& s, std::uint32_t ms) {
        const auto& mi = *skill_missile(s);
        const int lvl = skill_level ? skill_level(s.id) : 1;
        const int calc1 = calc(s, s.calc[0], lvl);
        const auto shared = std::make_shared<std::vector<int>>();
        const float dx = cast_x - player.x, dy = cast_y - player.y;
        const int range = mi.range + mi.lev_range * lvl;
        const int target = attack_mon >= 0 && monsters[std::size_t(attack_mon)].alive() ? attack_mon : -1;
        auto go = [&](float ddx, float ddy, int r) -> Missile& { return launch(mi, s, lvl, player.x, player.y, ddx, ddy, r, ms); };
        if (s.srvdofunc == 8) {
            const int n = std::max(calc1, 1);
            float px = dy * 5, py = -dx * 5;                 // subtiles, turned a right angle
            while (px * px + py * py > 3) { px = std::trunc(px / 2); py = std::trunc(py / 2); }
            if (px == 0 && py == 0) px = 1;
            for (int k = 0; k < n; ++k) {
                const float o = float(k) - float(n) / 2;
                go(dx + o * px / 5, dy + o * py / 5, range);
            }
        } else if (s.srvdofunc == 17) {
            for (int k = 0; k < std::max(calc1, 1); ++k) {
                const float a = (float(rng(81)) - 40) * 3.14159265f / 180;
                go(dx * std::cos(a) - dy * std::sin(a), dx * std::sin(a) + dy * std::cos(a), range);
            }
        } else if (s.srvdofunc == 22) {
            for (int k = 0; k < 64; ++k) {
                const float a = float(k) * 2 * 3.14159265f / 64;
                go(std::cos(a), std::sin(a), mi.range + calc1).struck = shared;
            }
        } else if (s.srvdofunc == 10) {
            auto& a = go(dx, dy, range);
            a.target = target; a.ed_pct = calc1;
        } else if (s.srvdofunc == 12) {
            const int r = calc(s, s.aurarange, lvl);
            std::vector<int> near;
            if (target >= 0) near.push_back(target);
            for (std::size_t i = 0; i < monsters.size(); ++i)
                if (int(i) != target && monsters[i].alive()
                    && std::hypot(monsters[i].u.x - player.x, monsters[i].u.y - player.y) * 5 <= float(r)) near.push_back(int(i));
            const int hi = calc1, lo = std::min(calc(s, s.calc[2], lvl), hi);
            const int n = std::clamp(int(near.size()), lo, hi);
            for (int k = 0; k < n; ++k)
                strafe.push_back({ near.empty() ? -1 : near[std::size_t(k) % near.size()], ms + std::uint32_t(k) * 120 });
        } else if (s.srvdofunc == 26) {
            go(dx, dy, range).hops = calc1;
        } else if (s.srvdofunc == 28) {
            launch(mi, s, lvl, cast_x, cast_y, 0, 0, range, ms);
        } else if (s.srvdofunc == 24) {
            const float ox = target >= 0 ? monsters[std::size_t(target)].u.x : cast_x;
            const float oy = target >= 0 ? monsters[std::size_t(target)].u.y : cast_y;
            const float wx = oy - player.y, wy = player.x - ox;
            launch(mi, s, lvl, ox, oy, wx, wy, range, ms);
            launch(mi, s, lvl, ox, oy, -wx, -wy, range, ms);
            if (const auto c = scene->missiles.find(s.srvmissileb); c != scene->missiles.end())
                launch(c->second, s, lvl, ox, oy, 0, 0, c->second.range + c->second.lev_range * lvl, ms);
        } else if (s.srvdofunc == 19) {
            channel = s.id;
        } else {
            go(dx, dy, range);
        }
    }
    // Once a frame for the player's skill missiles (their pSrvDoFunc):
    // 7 (FUN_005ae780) turns a guided one to its target every Param1
    // frames; 6 (FUN_005ae680, Fire Wall's maker) leaves its SubMissile1
    // where it is; 10 / 25 (FUN_005aea60 / FUN_005af880 -> FUN_005a9820:
    // Blizzard, Eruption) every calc2 frames drop SubMissile1 at a random
    // point within calc1 subtiles; 5 (FUN_005ae520: the flames of Fire
    // Wall, Meteor, Blaze) burns whoever stands in it every frame, CollideKill
    // 0. Hit function 14 (FUN_005aabb0, Meteor) goes off where the row's
    // Range ends: the skill's damage on everything within sHitPar1 (else
    // aurarange) subtiles, then HitSubMissile1 at the 18 offsets of
    // 0x6e2550 / 0x6e2598 (every sHitPar2th; FUN_005aaa90), each burning
    // Param3 + Param4 x (level - 1) 256ths a frame (FUN_004cc7c0).
    // Also Strafe's queued arrows and Inferno's flames.
    // ponytail: a burner hits a monster once a frame however many overlap
    // (by skill); the maker leaves one a frame (not per subtile); a Bone
    // Spirit cast at nothing seeks the monster nearest the cast point.
    void missile_tick(std::uint32_t ms) {
        const int frame = int(ms / 40);
        if (frame == last_frame) return;
        last_frame = frame;
        burned.clear();
        std::erase_if(strafe, [&](const std::pair<int, std::uint32_t>& q) {
            if (ms < q.second) return false;
            const auto* s = scene->skills.get(swing_skill);
            if (!s || s->srvdofunc != 12) return true;
            const auto& mi = *skill_missile(*s);
            const int lvl = skill_level ? skill_level(s->id) : 1;
            float tx = cast_x, ty = cast_y;
            if (q.first >= 0 && monsters[std::size_t(q.first)].alive()) { tx = monsters[std::size_t(q.first)].u.x; ty = monsters[std::size_t(q.first)].u.y; }
            launch(mi, *s, lvl, player.x, player.y, tx - player.x, ty - player.y, mi.range + mi.lev_range * lvl, ms).ed_pct
                = calc(*s, s->calc[1], lvl);
            return true;
        });
        if (channel >= 0) {
            const auto* s = scene->skills.get(channel);
            if (!s || pmode != kModeSC || swing_skill != channel) channel = -1;
            else {
                const int lvl = skill_level ? skill_level(s->id) : 1;
                launch(*skill_missile(*s), *s, lvl, player.x, player.y, cast_x - player.x, cast_y - player.y,
                       std::max(calc(*s, s->calc[0], lvl), 1), ms);
            }
        }
        for (std::size_t k = 0; k < missiles.size(); ++k) {
            auto& a = missiles[k];
            if (a.skill < 0 || !a.info) continue;
            const auto* s = scene->skills.get(a.skill);
            if (!s) continue;
            const int age = int(ms - a.born) / 40;
            const auto& mi = *a.info;
            if (mi.srv_do == 7 && age % std::max(mi.param1, 1) == 0) {
                if (a.target < 0 && s->srvstfunc == 0) {
                    float best = 1e9f;
                    for (std::size_t i = 0; i < monsters.size(); ++i)
                        if (const float d = std::hypot(monsters[i].u.x - cast_x, monsters[i].u.y - cast_y); monsters[i].alive() && d < best) {
                            best = d; a.target = int(i);
                        }
                }
                if (a.target >= 0 && monsters[std::size_t(a.target)].alive()) {
                    const auto& m = monsters[std::size_t(a.target)];
                    const float v = std::hypot(a.vx, a.vy), dx = m.u.x - a.x, dy = m.u.y - a.y, d = std::max(std::hypot(dx, dy), 0.01f);
                    a.vx = dx / d * v; a.vy = dy / d * v; a.dir = direction32(dx, dy);
                }
            }
            const auto sub = mi.sub.empty() ? scene->missiles.end() : scene->missiles.find(mi.sub);
            if (mi.srv_do == 6 && sub != scene->missiles.end())
                launch(sub->second, *s, a.level, a.x, a.y, 0, 0, sub->second.range + sub->second.lev_range * a.level, ms);
            if ((mi.srv_do == 10 || mi.srv_do == 25) && sub != scene->missiles.end()) {
                const int every = std::max(calc(*s, s->calc[1], a.level), 1), r = std::max(calc(*s, s->calc[0], a.level), 1);
                if (age % every == 0) {
                    const float ox = float(int(rng(2 * r - 1)) - (r - 1)) / 5, oy = float(int(rng(2 * r - 1)) - (r - 1)) / 5;
                    launch(sub->second, *s, a.level, a.x + ox, a.y + oy, 0, 0, sub->second.range, ms);
                }
            }
            if (mi.srv_do == 5) {
                const auto md = a.fixed >= 0 ? d2d::rules::MissileDamage{ .etype = 0, .elo = a.fixed, .ehi = a.fixed }
                                             : d2d::rules::missile_damage(scene->skills, *s, calc_env(), a.level);
                for (std::size_t i = 0; i < monsters.size(); ++i)
                    if (monsters[i].alive() && std::hypot(monsters[i].u.x - a.x, monsters[i].u.y - a.y) <= 0.5f
                        && !std::ranges::contains(burned, std::pair{ int(i), a.skill })) {
                        burned.emplace_back(int(i), a.skill);
                        burn(i, md, ms);
                    }
            }
            if (mi.hit_func == 14 && ms + 40 >= a.dies && a.frame < 0) {
                a.frame = frame;
                const int r = mi.hit_par1 > 0 ? mi.hit_par1 : std::max(calc(*s, s->aurarange, a.level), 1);
                for (std::size_t i = 0; i < monsters.size(); ++i)
                    if (monsters[i].alive() && std::hypot(monsters[i].u.x - a.x, monsters[i].u.y - a.y) * 5 <= float(r))
                        land(i, d2d::rules::missile_blow(d2d::rules::missile_damage(scene->skills, *s, calc_env(), a.level), target_of(i),
                                                         pierce(), rng), true, ms);
                if (const auto h = scene->missiles.find(mi.hit_sub); h != scene->missiles.end()) {
                    static constexpr int kX[18] = { 2, -2, 0, 0, -3, 0, 3, -1, 1, -1, 2, -4, -3, -1, 0, 1, 3, 4 };
                    static constexpr int kY[18] = { -2, -2, 2, 5, 3, 3, 3, 2, 1, -1, -1, -2, -2, -3, -4, -3, -3, -2 };
                    const int burn256 = s->par[2] + (a.level - 1) * s->par[3];
                    for (std::size_t j = 0; j < 18; j += std::size_t(std::max(mi.hit_par2, 1))) {
                        launch(h->second, *s, a.level, a.x + float(kX[j]) / 5, a.y + float(kY[j]) / 5, 0, 0, h->second.range, ms).fixed
                            = burn256 > 0 ? burn256 : -1;
                    }
                }
            }
        }
    }
    // An element in 256ths a frame on monster i (a burner's): less its
    // resistance, gathered until whole points come off (no hit recovery).
    void burn(std::size_t i, const d2d::rules::MissileDamage& md, std::uint32_t ms) {
        static constexpr int kRes[5] = { 2, 3, 4, 5, 1 };
        if (md.etype < 0 || md.etype > 4 || md.ehi <= 0) return;
        auto& m = monsters[i];
        const auto t = target_of(i);
        int res = t.res[std::size_t(kRes[md.etype])];
        if (md.etype < 4 && res < 100) res = std::max(res - pierce()[std::size_t(md.etype)], -100);
        m.dot_acc += double(d2d::rules::resisted(rng.range(md.elo, md.ehi), res)) / 256;
        const int whole = int(m.dot_acc);
        if (whole <= 0) return;
        m.dot_acc -= whole;
        m.hp -= whole;
        m.aware = true;
        if (!m.alive()) { set_mode(*scene, m, "DT", ms); killed(i, ms); }
    }
    [[nodiscard]] std::array<int, 4> pierce() const { return { int(psum[333]), int(psum[334]), int(psum[335]), int(psum[336]) }; }
    // A skill's missile reaching monster i (FUN_0064b860's record): with a
    // weapon share (the skill's SrcDam, or a row with no Skill its
    // SrcDamage) the weapon's blow at that share, attack rating rolled;
    // else a sure hit (ToHit missiles roll the attack rating); then the
    // skill's damage (missile_blow). True: it's spent — a CollideKill
    // missile is, unless it may pierce (Missiles.txt Pierce) and stat 328's
    // chance lets it fly on.
    // Hit function 1 (FUN_005a9a70) explodes instead: every monster within
    // sHitPar1 subtiles (else the skill's calc1) of the missile takes the
    // skill's damage (FUN_0056bad0 -> FUN_0056b7e0: squared subtile
    // distance against the radius squared).
    // ponytail: a wall or the end of its range doesn't set it off; a row
    // with no Skill uses no physical columns (MinDamage..) or synergy.
    bool skill_missile_hits(Missile& a, std::size_t i, std::uint32_t ms) {
        const auto* s = scene->skills.get(a.skill);
        if (!s) return true;
        if (a.info->srv_do == 5) return false;               // a burner: missile_tick's
        // Hit function 10 (FUN_005aa650): a guided one passes by all but its target.
        if (a.info->hit_func == 10 && a.target >= 0 && a.target != int(i) && monsters[std::size_t(a.target)].alive()) return false;
        auto& struck = *a.struck;
        if (a.info->hit_func == 1 && !std::ranges::contains(struck, -1)) {
            struck.push_back(-1);                            // exploding: the hits below are its
            const int r = a.info->hit_par1 > 0 ? a.info->hit_par1
                        : d2d::rules::eval_calc(scene->skills, s->calc[0], calc_env(), s->id, a.level);
            for (std::size_t j = 0; j < monsters.size(); ++j)
                if (monsters[j].alive() && std::hypot(monsters[j].u.x - a.x, monsters[j].u.y - a.y) * 5 <= float(std::max(r, 1)))
                    skill_missile_hits(a, j, ms);
            if (!std::ranges::contains(struck, int(i))) skill_missile_hits(a, i, ms);
            return true;
        }
        const auto target = target_of(i);
        const int clvl = int(cc.stats.get(d2d::d2s::kLevel));
        auto md = a.info->skill.empty() ? row_damage(*a.info, a.level)
                                        : d2d::rules::missile_damage(scene->skills, *s, calc_env(), a.level);
        d2d::rules::Blow b{ .hit = true };
        if (md.srcdam > 0) {
            d2d::rules::Swing sw;
            sw.srcdam = md.srcdam;
            sw.ed_pct = a.ed_pct;
            b = d2d::rules::player_blow(pf, target, clvl, rng, sw);
        } else if (a.info->to_hit) {
            b.hit = int(rng(100)) < d2d::rules::hit_chance(pf.ar, target.ac, clvl, target.level);
        }
        if (b.hit) {
            b = d2d::rules::missile_blow(md, target, pierce(), rng, b);
        }
        land(i, b, true, ms);
        struck.push_back(int(i));
        // Hit function 12 (FUN_005aa730, Chain Lightning): with hops left, on
        // from here at another monster within sHitPar1 (else aurarange)
        // subtiles (FUN_0056bd10), a hop fewer.
        // ponytail: the nearest one, not FUN_0056bd10's pick.
        if (a.info->hit_func == 12 && a.hops > 1) {
            const int r = a.info->hit_par1 > 0 ? a.info->hit_par1 : calc(*s, s->aurarange, a.level);
            int best = -1; float bd = 1e9f;
            for (std::size_t j = 0; j < monsters.size(); ++j)
                if (const float d = std::hypot(monsters[j].u.x - a.x, monsters[j].u.y - a.y);
                    j != i && monsters[j].alive() && d * 5 <= float(r) && d < bd) { bd = d; best = int(j); }
            if (best >= 0) {
                const auto& m = monsters[std::size_t(best)];
                launch(*a.info, *s, a.level, a.x, a.y, m.u.x - a.x, m.u.y - a.y, a.info->range, ms).hops = a.hops - 1;
            }
        }
        return a.info->collide_kill && !(a.info->pierce && int(rng(100)) < int(psum[328]));
    }
    // The aura's pulse, every perdelay ticks while it's on: a friendly one's
    // hitpoints heal the player (Prayer: edns, 256ths); an enemy one
    // (srvdofunc 66, FUN_005cf3a0; 81, FUN_005d0920) strikes each monster
    // within aurarangecalc subtiles (FUN_0056b7e0 with aurafilter) with the
    // skill's element (FUN_0056e0c0, the mastery in; Holy Fire, Holy Shock,
    // Holy Freeze's cold) — its aurastats ride the target state (+0x82)
    // and target_of applies them while the monster is in range.
    // ponytail: perdelay taken as ticks (its reader isn't traced); the
    // aurafilter bits aren't read (every monster counts); Sanctuary and
    // Redemption's own callbacks aren't built; no mana is drawn
    // (FUN_00644b10).
    void aura_pulse(std::uint32_t ms) {
        const auto* s = scene->skills.get(aura);
        if (!s || !s->aura || ms < aura_next || dead()) return;
        const int lvl = skill_level ? skill_level(aura) : 1;
        if (lvl < 1) return;
        const auto env = calc_env();
        aura_next = ms + std::uint32_t(std::max(s->perdelay, 25)) * 40;
        if (s->srvdofunc == 65) {
            for (std::size_t i = 0; i < s->aurastat.size(); ++i)
                if (s->aurastat[i] == 6) {
                    using namespace d2d::d2s;
                    const auto heal = d2d::rules::eval_calc(scene->skills, s->aura_calc[i], env, s->id, lvl);
                    cc.stats.v[kLife] = std::min(cc.stats.v[kMaxLife], cc.stats.v[kLife] + heal);
                }
            return;
        }
        if ((s->srvdofunc != 66 && s->srvdofunc != 81) || s->etype < 0 || s->etype > 4) return;
        const auto md = d2d::rules::MissileDamage{ .etype = s->etype,
                                                   .elo = d2d::rules::elem_damage(scene->skills, *s, env, lvl, false, 0, true),
                                                   .ehi = d2d::rules::elem_damage(scene->skills, *s, env, lvl, true, 0, true),
                                                   .elen = d2d::rules::elem_length(scene->skills, *s, env, lvl) };
        const std::array<int, 4> pierce{ int(psum[333]), int(psum[334]), int(psum[335]), int(psum[336]) };
        for (std::size_t i = 0; i < monsters.size(); ++i)
            if (monsters[i].alive() && in_aura(monsters[i]))
                land(i, d2d::rules::missile_blow(md, target_of(i), pierce, rng), true, ms);
    }
    [[nodiscard]] bool in_aura(const Monster& m) {
        const auto* s = scene->skills.get(aura);
        if (!s || !s->aura) return false;
        const int r = d2d::rules::eval_calc(scene->skills, s->aurarange, calc_env(), s->id, skill_level ? skill_level(aura) : 1);
        return std::hypot(m.u.x - player.x, m.u.y - player.y) * 5 <= float(r);
    }
    // Monster i as the player's blows see it: an enemy aura's aurastats
    // while it's in range (Conviction: resistances and defense down;
    // resistances 36 physical, 37 magic, 39 fire, 41 lightning, 43 cold,
    // 45 poison, 171 defense %).
    // ponytail: the published rule for immune monsters (a fifth of the
    // cut) isn't applied, nor the other target stats (Holy Freeze's
    // slow: its cold chills instead).
    [[nodiscard]] d2d::rules::Target target_of(std::size_t i) {
        auto t = monsters[i].target(*scene);
        const auto* s = scene->skills.get(aura);
        if (!s || (s->srvdofunc != 66 && s->srvdofunc != 81) || !in_aura(monsters[i])) return t;
        const auto env = calc_env();
        const int lvl = skill_level ? skill_level(aura) : 1;
        for (std::size_t k = 0; k < s->aurastat.size(); ++k) {
            const int v = d2d::rules::eval_calc(scene->skills, s->aura_calc[k], env, s->id, lvl);
            switch (s->aurastat[k]) {
                case 36: t.res[0] += v; break;  case 37: t.res[1] += v; break;
                case 39: t.res[2] += v; break;  case 41: t.res[3] += v; break;
                case 43: t.res[4] += v; break;  case 45: t.res[5] += v; break;
                case 171: t.ac += t.ac * v / 100; break;
                default: break;
            }
        }
        return t;
    }
    // A summoning skill d2d builds (the do spawns the `summon` row through
    // FUN_0056d940): Clay / Blood / Iron / Fire Golem (56, 57), Raise
    // Skeleton and Skeletal Mage (31, from a corpse), Valkyrie (16), the
    // Druid's Raven (114), Spirit Wolf, Fenris and Grizzly (119).
    [[nodiscard]] bool summon_skill(const d2d::rules::Skill& s) const {
        if (s.srvdofunc == 58) return true;                  // Revive: the corpse's own monster
        return !s.summon.empty() && scene->monsters.row(s.summon) >= 0
            && (s.srvdofunc == 56 || s.srvdofunc == 57 || s.srvdofunc == 31 || s.srvdofunc == 16 || s.srvdofunc == 114
                || s.srvdofunc == 144 || s.srvdofunc == 15 || s.srvdofunc == 49
                || (s.srvdofunc == 119 && (s.pettype == "spiritwolf" || s.pettype == "fenris" || s.pettype == "grizzly"))
                || (s.srvdofunc == 45 && trap_shot(s) >= 0));
    }
    // A trap's shooting skill: the first of its sumskills that fires a
    // missile d2d builds (the monster skills 'sentry lightning',
    // 'BoltSentry', 'death sentry ltng'); -1 for the others (Wake of Fire's
    // and Inferno's do 125 / 95, Death Sentry's corpse blast do 55).
    [[nodiscard]] int trap_shot(const d2d::rules::Skill& s) const {
        for (const auto& n : s.sumskill)
            if (const auto it = scene->skills.by_name.find(n); !n.empty() && it != scene->skills.by_name.end())
                if (const auto* k = scene->skills.get(it->second); k && skill_missile(*k, true)) return k->id;
        return -1;
    }
    // Casting one at (tx, ty) (the corpse there for Raise Skeleton): its
    // mana and SC, the summon on the action frame.
    bool cast_summon(int skill, float tx, float ty, std::uint32_t ms) {
        using namespace d2d::d2s;
        const auto* s = scene->skills.get(skill);
        if (!s || dead() || pmode >= 0 || !summon_skill(*s)) return false;
        if (s->target_corpse && corpse_near(tx, ty) < 0) return false;
        const int lvl = skill_level ? skill_level(skill) : 0;
        const int cost = d2d::rules::mana_cost(*s, lvl);
        if (lvl <= 0 || cc.stats.v[kMana] < cost) return false;
        cc.stats.v[kMana] -= cost;
        swing_skill = skill;
        cast_x = tx; cast_y = ty;
        attack_mon = -1;
        player.dir = direction16(tx - player.x, ty - player.y);
        set_pmode(kModeSC, ms);
        pstruck = false;
        return true;
    }
    // The unraised corpse within 2 cells of (x, y) nearest it, or -1.
    [[nodiscard]] int corpse_near(float x, float y) const {
        int best = -1; float bd = 2.f;
        for (std::size_t i = 0; i < monsters.size(); ++i)
            if (const auto& m = monsters[i]; !m.alive() && !m.corpse_used && m.mode == "DD")
                if (const float d = std::hypot(m.u.x - x, m.u.y - y); d < bd) { bd = d; best = int(i); }
        return best;
    }
    // The pet arrives (FUN_0056d940, FUN_005c49e0, FUN_005c4470): spawned
    // as its MonStats row at the row's level, then set to level min(clvl,
    // clvl x 3 / 4 + bonus) — bonus calc2 for the Druid's (do 114 / 119),
    // else 0 — with MonLvl's own defense and attack rating at it (stats 31
    // / 19), life + calc1 %.
    // Past petmax of its pettype the oldest goes.
    // ponytail: the skill's aurastats / passive stats / sumskills / sumumod
    // on the pet (FUN_005c4470), Skeleton and Golem Mastery (FUN_005d6b60),
    // the skeletal mage's missile and the golems' specials aren't built;
    // pets stay in the Blood Moor.
    // A skill by its name as the tables write it (Fire Golem's sumskill
    // says 'holy fire').
    [[nodiscard]] const d2d::rules::Skill* skill_named(std::string_view n) const {
        if (n.empty()) return nullptr;
        if (const auto it = scene->skills.by_name.find(std::string(n)); it != scene->skills.by_name.end()) return scene->skills.get(it->second);
        for (const auto& [k, id] : scene->skills.by_name)
            if (std::ranges::equal(k, n, [](char a, char b) { return std::tolower(std::uint8_t(a)) == std::tolower(std::uint8_t(b)); }))
                return scene->skills.get(id);
        return nullptr;
    }
    // The missile row a pet's skill shoots: its srvmissile, else
    // srvmissilea — NecromageMissile's do 149 (FUN_005ce0b0) adds the mage's
    // variant (+0xf of its monster data) to it: necromage1..4.
    // ponytail: the variant is rolled at the raise; where game.exe sets it
    // isn't traced.
    [[nodiscard]] const Scene::MissileInfo* pet_missile(const d2d::rules::Skill& k, int variant = 0) const {
        std::string n = k.srvmissile.empty() ? k.srvmissilea : k.srvmissile;
        if (k.srvdofunc == 149 && !n.empty() && n.back() == '1') n.back() = char('1' + std::clamp(variant, 0, 3));
        const auto it = scene->missiles.find(n);
        return n.empty() || it == scene->missiles.end() ? nullptr : &it->second;
    }
    void summon(const d2d::rules::Skill& s, std::uint32_t ms) {
        int type = scene->monsters.row(s.summon);
        const auto env = calc_env();
        const int lvl = skill_level ? skill_level(s.id) : 1;
        float x = cast_x, y = cast_y;
        if (s.target_corpse) {
            const int c = corpse_near(cast_x, cast_y);
            if (c < 0) return;
            monsters[std::size_t(c)].corpse_used = true;
            x = monsters[std::size_t(c)].u.x; y = monsters[std::size_t(c)].u.y;
            if (s.srvdofunc == 58) type = monsters[std::size_t(c)].type;
        }
        if (type < 0) return;
        // Hydra (do 144, FUN_005ca910): three at once.
        for (int k = s.srvdofunc == 144 ? 3 : 1; k > 0; --k) summon_one(s, type, lvl, env, x + float(k - 1) * 0.6f, y, ms);
    }
    void summon_one(const d2d::rules::Skill& s, int type, int lvl, const d2d::rules::CalcEnv& env, float x, float y, std::uint32_t ms) {
        const int max = std::max(d2d::rules::eval_calc(scene->skills, s.petmax, env, s.id, lvl), 1);
        auto same = [&](const Pet& p) { return scene->skills.get(p.skill) && scene->skills.get(p.skill)->pettype == s.pettype; };
        while (std::ranges::count_if(pets, same) >= max) pets.erase(std::ranges::find_if(pets, same));
        const int clvl = int(cc.stats.get(d2d::d2s::kLevel));
        const int bonus = s.srvdofunc == 114 || s.srvdofunc == 119 ? d2d::rules::eval_calc(scene->skills, s.calc[1], env, s.id, lvl) : 0;
        const int plvl = std::max(std::min(clvl, clvl * 3 / 4 + bonus), 1);
        Pet p;
        auto& m = p.m;
        m.type = type;
        m.npc = scene->mon_npc[std::size_t(type)];
        const auto& t = scene->monsters.types[std::size_t(type)];
        for (std::size_t l = 0; l < 16; ++l)
            if (!t.parts[l].empty()) m.npc.comp[l] = t.parts[l].front();
        m.difficulty = std::clamp(cc.header.active_difficulty(), 0, 2);
        m.st = d2d::rules::monster_stats(scene->monsters, type, m.difficulty, rng);   // spawned at its own level (FUN_005b2f20)
        // Its life and damage straight from MonStats' columns (a Raise
        // Skeleton skeleton: 21 life, 1-2 damage in Normal; 30 / 42 in
        // Nightmare / Hell), not scaled by MonLvl.
        // ponytail: FUN_005b2f20's pet branch isn't traced; the published
        // pet values are the raw columns.
        // Revive (do 58, FUN_005c56c0) raises the monster as it was: its own
        // stats at its level, life and level brought down to the owner's
        // when it's above it.
        if (s.srvdofunc != 58) {
            const auto& d = scene->monsters.types[std::size_t(type)].diff[std::size_t(m.difficulty)];
            m.st.hp = std::max(rng.range(d.min_hp, std::max(d.max_hp, d.min_hp)), 1);
            m.st.a1_min = d.a1_min; m.st.a1_max = std::max(d.a1_max, d.a1_min);
            m.st.a2_min = d.a2_min; m.st.a2_max = std::max(d.a2_max, d.a2_min);
        }
        // The Druid's and Raven's hit with the skill's physical damage (their
        // MonStats rows have none).
        // ponytail: where game.exe hands the skill's damage to the pet isn't
        // traced.
        if (m.st.a1_max == 0 && s.maxdam > 0) {
            const auto md = d2d::rules::missile_damage(scene->skills, s, env, lvl);
            m.st.a1_min = md.phys_lo >> 8; m.st.a1_max = std::max(md.phys_hi >> 8, m.st.a1_min);
        }
        m.st.level = s.srvdofunc == 58 ? std::min(m.st.level, clvl) : s.srvdofunc == 49 || s.srvdofunc == 15 ? clvl : plvl;
        if (s.srvdofunc != 58 && !scene->monsters.lvl.empty()) {
            const auto& L = scene->monsters.lvl[std::min<std::size_t>(std::size_t(plvl), scene->monsters.lvl.size() - 1)];
            m.st.ac = L.ac[std::size_t(m.difficulty)];
            m.st.th = L.th[std::size_t(m.difficulty)];
        }
        // FUN_005c4470: the skill's passive stats (base) and aurastats (its
        // aurastate) onto the pet, at the skill's level through the owner's
        // calcs — Skeleton Mastery, Golem Mastery and the golems' synergies
        // come in that way — then life (with maxhp) + calc1 %, then its
        // sumskills at their sumsk calcs.
        std::unordered_map<int, int> ps;
        for (std::size_t k = 0; k < 5; ++k)
            if (s.passive_stat[k] >= 0) ps[s.passive_stat[k]] += calc(s, s.passive_calc[k], lvl);
        for (std::size_t k = 0; k < 6; ++k)
            if (s.aurastat[k] >= 0) ps[s.aurastat[k]] += calc(s, s.aura_calc[k], lvl);
        auto st = [&](int id) { const auto it = ps.find(id); return it == ps.end() ? 0 : it->second; };
        m.st.hp += st(7) >> 8;                                               // maxhp, 256ths
        m.st.hp += int(std::int64_t(m.st.hp) * calc(s, s.calc[0], lvl) / 100);
        m.hp = m.st.hp = std::max(m.st.hp, 1);
        for (int* d : { &m.st.a1_min, &m.st.a1_max }) { *d += st(111); *d += *d * st(25) / 100; }   // item_normaldamage, damagepercent
        m.st.th += st(19); m.st.th += m.st.th * st(119) / 100;              // tohit, item_tohit_percent
        m.st.ac += st(31); m.st.ac += m.st.ac * (st(16) + st(171)) / 100;   // armorclass, item / skill_armor_percent
        const auto& base_res = t.diff[std::size_t(m.difficulty)].res;
        p.res = { base_res[2] + st(39), base_res[3] + st(41), base_res[4] + st(43), base_res[5] + st(45) };
        p.thorns = st(131); p.fire_lo = st(48); p.fire_hi = std::max(st(49), p.fire_lo); p.speed_pct = st(67);
        for (std::size_t k = 0; k < 5; ++k) {
            const auto* k_s = skill_named(s.sumskill[k]);
            const int kl = k_s ? calc(s, s.sumsk_calc[k], lvl) : 0;
            if (!k_s || kl < 1) continue;
            if (k_s->aura) { p.aura = k_s->id; p.aura_level = kl; }
            else if (pet_missile(*k_s)) { p.ranged = k_s->id; p.ranged_level = kl; }
        }
        m.u.x = m.home_x = x; m.u.y = m.home_y = y;
        m.u.dir = player.dir;
        p.skill = s.id;
        p.where = level;
        p.variant = int(rng(4));
        switch (s.srvdofunc) {
            case 58: p.until = ms + std::uint32_t(std::max(calc(s, s.calc[1], lvl), 1)) * 40; break;   // Revive: calc2 frames
            case 144:                                                                                   // Hydra: Param1 frames, it stands
                p.until = ms + std::uint32_t(std::max(s.par[0], 1)) * 40;
                if (const auto* hm = skill_named("HydraMissile")) { p.ranged = hm->id; p.ranged_level = lvl; }
                break;
            case 15:                                  // Decoy (FUN_005dc000): owner's life x calc3 %, calc2 frames, it stands
                m.hp = m.st.hp = std::max(int(cc.stats.fixed(d2d::d2s::kMaxLife) * calc(s, s.calc[2], lvl) / 100), 1);
                p.until = ms + std::uint32_t(std::max(calc(s, s.calc[1], lvl), 1)) * 40;
                p.idle = true;
                break;
            case 49: p.mirror = true; break;          // Shadow Warrior / Master (FUN_005d6e70): the owner's level and gear
            case 114: p.hits = std::max(s.par[4], 1); break;   // Raven: its attacks
            default: break;
        }
        if (s.srvdofunc == 45) {                             // a trap (FUN_005d6170 -> FUN_005d5e10)
            p.shot_skill = trap_shot(s);
            for (std::size_t k = 0; k < 5; ++k)
                if (scene->skills.by_name.contains(s.sumskill[k]) && scene->skills.by_name.at(s.sumskill[k]) == p.shot_skill)
                    p.shot_level = std::max(d2d::rules::eval_calc(scene->skills, s.sumsk_calc[k], env, s.id, lvl), 1);
            const int c4 = d2d::rules::eval_calc(scene->skills, s.calc[3], env, s.id, lvl);
            p.shots = c4 > 0 ? c4 : std::max(s.par[0], 1);
        }
        set_mode(*scene, m, "NU", ms);
        pets.push_back(std::move(p));
    }
    // A trap's think (MonStats AI AssassinSentry / DeathSentry): every
    // aidel ticks, with a monster within aip4 subtiles, it shoots its skill
    // at the nearest — the skill's missile, whose row names the player's
    // skill for the damage (sentrylightningbolt: Lightning Sentry) at the
    // trap's sumskill level; spent after its shots (the skill's Param1,
    // Charged Bolt Sentry's calc4), it dies.
    // ponytail: the sentry AI function isn't traced (aip4 read as the
    // range, the shots from the published counts); it shoots from its
    // spot, in no animation.
    void trap_turn(Pet& p, std::uint32_t ms) {
        auto& m = p.m;
        if (ms < m.next_act) return;
        const auto& d = scene->monsters.types[std::size_t(m.type)].diff[std::size_t(m.difficulty)];
        m.next_act = ms + std::uint32_t(std::max(d.aidel, 1)) * 40;
        const float range = float(d.aip[3] > 0 ? d.aip[3] : 25) / 5;
        int best = -1; float bd = range;
        for (std::size_t i = 0; i < monsters.size(); ++i)
            if (const float dd = std::hypot(monsters[i].u.x - m.u.x, monsters[i].u.y - m.u.y); monsters[i].alive() && dd <= bd) { bd = dd; best = int(i); }
        if (best < 0) return;
        const auto* k = scene->skills.get(p.shot_skill);
        if (!k) return;
        const auto& mi = *skill_missile(*k, true);
        const auto owner = scene->skills.by_name.find(mi.skill);
        const float dx = monsters[std::size_t(best)].u.x - m.u.x, dy = monsters[std::size_t(best)].u.y - m.u.y, dist = std::max(std::hypot(dx, dy), 0.01f);
        const float v = cells_per_sec(float(mi.vel));
        const int n = k->srvdofunc == 17 ? std::max(d2d::rules::eval_calc(scene->skills, k->calc[0], calc_env(), k->id, p.shot_level), 1) : 1;
        for (int j = 0; j < n; ++j) {
            const float a = n > 1 ? (float(rng(81)) - 40) * 3.14159265f / 180 : 0.f;
            const float ex = dx * std::cos(a) - dy * std::sin(a), ey = dx * std::sin(a) + dy * std::cos(a);
            Missile x{ &mi, m.u.x, m.u.y, ex / dist * v, ey / dist * v, direction32(ex, ey), ms,
                       ms + std::uint32_t(std::max(mi.range, 1)) * 40, {} };
            x.friendly = true; x.level = p.shot_level;
            x.skill = owner != scene->skills.by_name.end() ? owner->second : k->id;
            missiles.push_back(x);
        }
        if (--p.shots <= 0) { m.hp = 0; set_mode(*scene, m, "DT", ms); }
    }
    // A pet as the monsters see it: its defense, its MonStats resistances
    // (traps aren't there to hit).
    [[nodiscard]] Foe pet_foe(const Pet& p) const {
        auto f = d2d::rules::simple_fighter(p.m.st.a1_min, p.m.st.a1_max, p.m.st.th, p.m.st.ac);
        for (std::size_t k = 0; k < 4; ++k) f.res[k] = std::min(p.res[k], 95);
        f.thorns_pct = p.thorns;                             // Iron Golem's thorns
        const auto& t = scene->monsters.types[std::size_t(p.m.type)];
        const bool still = t.velocity == 0 && t.run == 0;     // a Hydra, like a trap, isn't there to hit
        return Foe{ p.m.u.x, p.m.u.y, p.m.st.level, p.m.alive() && p.m.mode != "DT" && p.shot_skill < 0 && !still && p.where == level,
                    p.m.u.walking, f };
    }
    void pet_hurt(Pet& p, int damage, std::uint32_t ms) {
        if (damage > 0 && p.m.mode != "DT") hurt(*scene, p.m, damage, ms);
    }
    // The player crossed from `from` to `to` (shifted by dx, dy): the pets
    // with them come along, traps stay where they were set.
    void pets_cross(const Level* from, const Level* to, float dx, float dy) {
        for (auto& p : pets)
            if (p.where == from && p.shot_skill < 0 && p.m.alive()) {
                p.where = to; p.target = -1;
                p.m.u.x -= dx; p.m.u.y -= dy; p.m.u.goal_x -= dx; p.m.u.goal_y -= dy;
                for (auto& [px, py] : p.m.u.path) { px -= dx; py -= dy; }
                p.m.home_x -= dx; p.m.home_y -= dy;
            }
    }
    // A pet's enemy aura (Fire Golem's Holy Fire: srvdofunc 66) pulses
    // round the pet, as the player's does round the player (aura_pulse).
    void pet_aura(Pet& p, std::uint32_t ms) {
        const auto* s = scene->skills.get(p.aura);
        if (!s || s->srvdofunc != 66 || ms < p.aura_next || s->etype < 0 || s->etype > 4) return;
        p.aura_next = ms + std::uint32_t(std::max(s->perdelay, 25)) * 40;
        const int r = calc(*s, s->aurarange, p.aura_level);
        const auto md = d2d::rules::MissileDamage{ .etype = s->etype,
                                                   .elo = d2d::rules::elem_damage(scene->skills, *s, calc_env(), p.aura_level, false),
                                                   .ehi = d2d::rules::elem_damage(scene->skills, *s, calc_env(), p.aura_level, true) };
        for (std::size_t i = 0; i < monsters.size(); ++i)
            if (monsters[i].alive() && std::hypot(monsters[i].u.x - p.m.u.x, monsters[i].u.y - p.m.u.y) * 5 <= float(r))
                land(i, d2d::rules::missile_blow(md, target_of(i), {}, rng), false, ms);
    }
    // The pets' turn, the merc's way: each goes for the nearest monster
    // within 8 of the player that has noticed them (or within 4 of the
    // pet), strikes it in reach on its A1's action frame (its damage and
    // attack rating against the monster's defense), and otherwise follows
    // the player. A dead one plays its death and is gone.
    // ponytail: every pet fights in melee with A1; the Hireable-style
    // think isn't game.exe's pet AI (not traced).
    void pets_turn(std::uint32_t ms, float dt, const Crowd& crowd) {
        std::erase_if(pets, [&](const Pet& p) { return !p.m.alive() && p.m.mode == "DT" && ms >= p.m.mode_until; });
        const auto sc = std::size_t(kUiToSaveClass[std::max(cc.selected, 0)]);
        for (auto& p : pets) {
            auto& m = p.m;
            auto& u = m.u;
            if (!m.alive() || p.where != level) continue;
            if (p.until && ms >= p.until) { m.hp = 0; set_mode(*scene, m, "DT", ms); continue; }
            if (p.shot_skill >= 0) { trap_turn(p, ms); continue; }
            if (p.aura >= 0) pet_aura(p, ms);
            if (p.idle) continue;                            // Decoy stands where it was cast
            if (m.mode == "A1") {
                if (!m.struck && p.target >= 0 && ms >= u.mode_ms + scene->npc_anim(m.npc, "A1").action_ms()) {
                    m.struck = true;
                    const auto i = std::size_t(p.target);
                    if (const auto* mi = p.ranged >= 0 ? pet_missile(*scene->skills.get(p.ranged), p.variant) : nullptr; mi && monsters[i].alive()) {
                        Missile x{ mi, u.x, u.y, 0, 0, 0, ms, ms + std::uint32_t(std::max(mi->range, 1)) * 40, {} };
                        const float dx = monsters[i].u.x - u.x, dy = monsters[i].u.y - u.y, d = std::max(std::hypot(dx, dy), 0.01f);
                        x.vx = dx / d * cells_per_sec(float(mi->vel)); x.vy = dy / d * cells_per_sec(float(mi->vel)); x.dir = direction32(dx, dy);
                        x.friendly = true; x.level = p.ranged_level; x.skill = p.ranged;
                        if (const auto* o = skill_named(mi->skill)) x.skill = o->id;   // 'hydra' carries the Hydra skill's damage
                        pending.push_back(x);
                    } else if (monsters[i].alive() && std::hypot(monsters[i].u.x - u.x, monsters[i].u.y - u.y) <= kMeleeReach + 0.3f) {
                        auto t = target_of(i);
                        // A Shadow Warrior swings the owner's blow (its copied gear and level).
                        // ponytail: its own skills (Fists of Fire, Blade Fury, ...) and
                        // FUN_005d6cf0's skill copy aren't built.
                        auto b = d2d::rules::player_blow(p.mirror ? pf : d2d::rules::simple_fighter(m.st.a1_min, m.st.a1_max, m.st.th),
                                                         t, m.st.level, rng);
                        if (p.hits > 0 && --p.hits == 0) { m.hp = 0; set_mode(*scene, m, "DT", ms); }   // a Raven's last
                        if (b.hit && p.fire_hi > 0) b.damage += d2d::rules::resisted(rng.range(p.fire_lo, p.fire_hi), t.res[2]);   // Fire Golem
                        land(i, b, false, ms);
                    }
                }
                if (ms < m.mode_until) continue;
                set_mode(*scene, m, "NU", ms);
            }
            if (m.mode == "GH" && ms < m.mode_until) continue;
            if (p.target >= 0 && !monsters[std::size_t(p.target)].alive()) p.target = -1;
            if (p.target < 0) {
                float best = 1e9f;
                for (std::size_t i = 0; i < monsters.size(); ++i) {
                    const auto& o = monsters[i];
                    if (!o.alive()) continue;
                    const float dp = std::hypot(o.u.x - player.x, o.u.y - player.y), dm = std::hypot(o.u.x - u.x, o.u.y - u.y);
                    if (((o.aware && dp < 8) || dm < 4) && dm < best) { best = dm; p.target = int(i); }
                }
            }
            const auto& t = scene->monsters.types[std::size_t(m.type)];
            const float speed = cells_per_sec(float(std::max(t.run, t.velocity))) * float(100 + p.speed_pct) / 100;
            if (speed <= 0) {                                // a Hydra: it stands and shoots what comes in range
                if (p.target >= 0 && std::hypot(monsters[std::size_t(p.target)].u.x - u.x, monsters[std::size_t(p.target)].u.y - u.y) > 6.f) p.target = -1;
                for (std::size_t i = 0; p.target < 0 && i < monsters.size(); ++i)
                    if (monsters[i].alive() && std::hypot(monsters[i].u.x - u.x, monsters[i].u.y - u.y) <= 6.f) p.target = int(i);
                if (p.target >= 0 && ms >= m.next_act) {
                    const auto& o = monsters[std::size_t(p.target)];
                    u.dir = direction16(o.u.x - u.x, o.u.y - u.y);
                    set_mode(*scene, m, scene->npc_anim(m.npc, "A1").cof.directions() ? "A1" : "NU", ms);
                    m.struck = false;
                    m.next_act = ms + std::uint32_t(std::max(scene->monsters.types[std::size_t(m.type)].diff[std::size_t(m.difficulty)].aidel, 15)) * 40;
                }
                continue;
            }
            if (p.target >= 0) {
                const auto& o = monsters[std::size_t(p.target)];
                const float dx = o.u.x - u.x, dy = o.u.y - u.y;
                if (std::hypot(dx, dy) <= (p.ranged >= 0 ? 6.f : kMeleeReach)) {
                    u.dir = direction16(dx, dy);
                    u.path.clear(); u.walking = false;
                    set_mode(*scene, m, "A1", ms);
                    m.struck = false;
                    continue;
                }
                if (u.path.empty() || std::hypot(u.goal_x - o.u.x, u.goal_y - o.u.y) > 1.f) {
                    u.path = walk_path(*level, u.x, u.y, o.u.x, o.u.y, crowd, &u);
                    u.goal_x = o.u.x; u.goal_y = o.u.y;
                }
                if (!follow_path(*level, u, speed * dt, crowd)) p.target = -1;
            } else {
                merc_follow(*level, u, player.x, player.y, cells_per_sec(float(scene->run_velocity[sc])) * 1.1f, ms, dt, crowd);
            }
            const std::string_view want = u.walking ? "WL" : "NU";
            if (m.mode != want) set_mode(*scene, m, want, ms);
        }
    }
    // Holy Shield (FUN_005c9480): the holyshield state for auralencalc
    // ticks, its aurastats (toblock dm56) on the player.
    // ponytail: the aura events (+0x84) and the passive part
    // (FUN_005c6dc0) aren't read; cast rate (FCR) isn't applied; a shield
    // is assumed (itypea1 shie isn't checked).
    bool cast(int skill, std::uint32_t ms) {
        using namespace d2d::d2s;
        const auto* s = scene->skills.get(skill);
        if (!s || dead() || pmode >= 0 || !self_cast(*s)) return false;
        const int lvl = skill_level ? skill_level(skill) : 0;
        const int cost = d2d::rules::mana_cost(*s, lvl);
        if (lvl <= 0 || cc.stats.v[kMana] < cost) return false;
        cc.stats.v[kMana] -= cost;
        swing_skill = skill;
        attack_mon = -1;
        set_pmode(kModeSC, ms);
        pstruck = false;
        return true;
    }
    // Frenzy's state (FUN_005d8c70): each hit that lands raises it a level,
    // up to the skill's, for auralencalc ticks; its aurastats (velocitypercent
    // dm34, attackrate dm56) are taken at that level.
    void frenzy(const d2d::rules::Skill& s, std::uint32_t ms) {
        const int lvl = skill_level ? skill_level(s.id) : 1;
        const auto env = calc_env();
        const auto it = std::ranges::find(self_states, s.id, &SelfState::skill);
        const int n = std::min(it == self_states.end() ? 1 : it->level + 1, lvl);
        const auto until = ms + std::uint32_t(std::max(d2d::rules::eval_calc(scene->skills, s.auralen, env, s.id, lvl), 1)) * 40;
        if (it == self_states.end()) self_states.push_back({ s.id, n, until });
        else *it = { s.id, n, until };
    }
    // Who the next strike of a chain goes for: the same monster while it
    // lives; Zeal, else the nearest one in reach (FUN_0056bd10's search).
    // ponytail: FUN_0056bd10's pick (it's handed the last target's id) isn't
    // traced: nearest is assumed; Zeal doesn't change targets while one lives.
    // Zeal's and Fend's next hit (do 13) goes round the enemies in reach;
    // Talon's next kick stays on its monster while it lives.
    bool next_target() {
        const auto* s = scene->skills.get(swing_skill);
        if (s && s->srvdofunc == 13) { other_target(); return attack_mon >= 0; }
        return attack_mon >= 0 && monsters[std::size_t(attack_mon)].alive();
    }
    // The monsters in reach, alive, by unit id (d2d's index), as
    // FUN_0056b7e0 walks the rooms around the player.
    // ponytail: the reach taken as melee reach (FUN_0056e510's range isn't read).
    [[nodiscard]] std::vector<int> in_reach() const {
        std::vector<int> out;
        for (std::size_t i = 0; i < monsters.size(); ++i)
            if (monsters[i].alive() && std::hypot(monsters[i].u.x - player.x, monsters[i].u.y - player.y) <= kMeleeReach + 0.5f)
                out.push_back(int(i));
        return out;
    }
    // Closing in on the monster being attacked: in reach, swing; else the
    // point to walk to (nullopt: nothing to do).
    std::optional<std::pair<float, float>> engage(std::uint32_t ms) {
        if (attack_mon < 0 || pmode >= 0) return std::nullopt;
        const auto& m = monsters[std::size_t(attack_mon)];
        if (!m.alive()) { attack_mon = -1; return std::nullopt; }
        const auto* s = scene->skills.get(attack_skill);
        if (s && missile_skill(*s)) {                        // from here, at it
            cast_missile(attack_skill, m.u.x, m.u.y, ms);
            return std::nullopt;
        }
        if (s && moving_skill(*s) && (s->srvdofunc != 67 || std::hypot(m.u.x - player.x, m.u.y - player.y) > kMeleeReach)) {
            player.dir = direction16(m.u.x - player.x, m.u.y - player.y);
            move_x = m.u.x; move_y = m.u.y;                  // Whirlwind to it, Charge and Leap Attack at it
            start_swing(ms);
            return std::nullopt;
        }
        if (std::hypot(m.u.x - player.x, m.u.y - player.y) <= kMeleeReach) {
            player.dir = direction16(m.u.x - player.x, m.u.y - player.y);
            start_swing(ms);
            return std::nullopt;
        }
        return std::pair{ m.u.x, m.u.y };
    }
    // The monsters around the player, in the crowd that blocks walkers.
    void crowd(Crowd& c) {
        for (auto& m : monsters)
            if (m.alive() && std::abs(m.u.x - player.x) < 12 && std::abs(m.u.y - player.y) < 12) c.units.push_back(&m.u);
    }
    // One frame of the fight in the Blood Moor (`in_moor`), and the merc's
    // turn (following, outside it).
    void world(bool in_moor, std::uint32_t ms, float dt, const Crowd& crowd) {
        // Monsters think while the player is near (D2 runs the rooms
        // around each player); what they hit comes off the player's life,
        // and a hit of a twelfth of max life or more makes them flinch (GH).
        if (in_moor) {
            std::vector<Foe> foes{ Foe{ player.x, player.y, int(cc.stats.get(d2d::d2s::kLevel)), true, player.walking, pf },
                                   Foe{ merc ? merc->x : 0, merc ? merc->y : 0, merc_st.level, merc && merc_mode != "DT",
                                        merc && merc->walking, merc_fighter() } };
            for (const auto& p : pets) foes.push_back(pet_foe(p));
            for (std::size_t i = 0; i < monsters.size(); ++i) {
                auto& m = monsters[i];
                if (std::abs(m.u.x - player.x) < 30 && std::abs(m.u.y - player.y) < 30
                    && monster_update(*scene, *level, m, foes, rng, ms, dt, crowd, missiles))
                    killed(i, ms);                           // on the player's thorns
            }
            monster_dots(ms, dt);
            aura_pulse(ms);
            for (std::size_t k = 0; k < pets.size(); ++k) pet_hurt(pets[k], foes[2 + k].damage, ms);
            pets_turn(ms, dt, crowd);
            missile_tick(ms);
            // The merc's arrows strike the first live monster they reach.
            missiles_update(*level, missiles, foes, rng, ms, dt, [&](Missile& a) {
                for (std::size_t i = 0; i < monsters.size(); ++i) {
                    auto& m = monsters[i];
                    if (!m.alive() || std::hypot(m.u.x - a.x, m.u.y - a.y) > 0.5f) continue;
                    if (a.skill >= 0) {
                        if (std::ranges::contains(*a.struck, int(i))) continue;
                        if (skill_missile_hits(a, i, ms)) return true;
                        continue;
                    }
                    land(i, d2d::rules::player_blow(d2d::rules::simple_fighter(a.min, a.max, a.ar), target_of(i), a.level, rng),
                         false, ms);
                    return true;
                }
                return false;
            });
            std::ranges::move(pending, std::back_inserter(missiles));
            pending.clear();
            if (merc && foes[1].damage > 0 && merc_mode != "DT") {
                merc_life -= foes[1].damage;
                auto& u = *merc;
                const auto mode = merc_life <= 0 ? std::string_view("DT") : foes[1].damage * 12 >= merc_st.life ? std::string_view("GH") : merc_mode;
                if (mode != merc_mode) {
                    merc_mode = mode; u.mode_ms = ms; u.walking = false; u.path.clear();
                    merc_until = ms + scene->npc_anim(*merc_npc, mode).length_ms();
                }
            }
            for (auto& m : monsters)
                if (std::abs(m.u.x - player.x) < 30 && std::abs(m.u.y - player.y) < 30) monster_sounds(m, ms);
            auto& foe = foes[0];
            // Poison works on the player over its ticks (a negative potion).
            // One poison at a time: a new one at least as strong replaces it
            // (and its length), a weaker one is ignored (FUN_0057ac50).
            if (foe.poison > 0) {
                const Regen p{ -foe.poison * 256.0 / (foe.poison_ticks * 40.0), 0, ms + std::uint32_t(foe.poison_ticks) * 40, true };
                const auto old = std::ranges::find_if(regen, [](const Regen& r) { return r.poison; });
                if (old == regen.end()) regen.push_back(p);
                else if (p.life <= old->life) *old = p;
            }
            if (foe.blocked && pmode < 0) set_pmode(kModeBL, ms);   // a block plays out (FBR)
            foe.damage += int(self_hurt >> 8);
            self_hurt &= 255;
            if (foe.damage > 0) {
                using namespace d2d::d2s;
                cc.stats.v[kLife] -= std::int64_t(foe.damage) << 8;
                if (cc.stats.v[kLife] <= 0) {
                    cc.stats.v[kLife] = 0;
                    set_pmode(kModeDT, ms);
                    attack_mon = -1;
                    d2d::log::info("the player died");
                } else if (std::int64_t(foe.damage) * 12 >= cc.stats.fixed(kMaxLife) && !attack_mode(pmode) && pmode != kModeBL) {
                    set_pmode(kModeGH, ms);
                }
            }
        }
        if (!in_moor) {                                      // pets follow the player about camp
            const auto sc = std::size_t(kUiToSaveClass[std::max(cc.selected, 0)]);
            for (auto& p : pets)
                if (p.where == level && p.m.alive()) {
                    merc_follow(*level, p.m.u, player.x, player.y, cells_per_sec(float(scene->run_velocity[sc])) * 1.1f, ms, dt, crowd);
                    if (const std::string_view want = p.m.u.walking ? "WL" : "NU"; p.m.mode != want) set_mode(*scene, p.m, want, ms);
                }
        }
        if (merc && merc_npc) {
            if (in_moor) merc_turn(ms, dt, crowd);
            else {
                const auto sc = std::size_t(kUiToSaveClass[std::max(cc.selected, 0)]);
                merc_follow(*level, *merc, player.x, player.y, cells_per_sec(float(scene->run_velocity[sc])) * 1.1f, ms, dt, crowd);
                merc_mode = merc->walking ? "WL" : "NU";
            }
        }
    }
    // Monsters, missiles and the merc as units the world draws by depth.
    void units(const std::string* merc_label, std::vector<Unit>& out) const {
        if (merc && merc_npc)                          // npc -2: the merc, hoverable, no NPC menu
            out.push_back({ merc->x, merc->y, &scene->npc_anim(*merc_npc, merc_mode), merc->dir,
                            merc_mode == "DT" ? nullptr : merc_label, merc->mode_ms, -2 });
        for (const auto& p : pets)
            if (p.where == level)
                out.push_back({ p.m.u.x, p.m.u.y, &scene->npc_anim(p.m.npc, p.m.mode), p.m.u.dir, nullptr, p.m.u.mode_ms, -3 });
        if (level != &scene->moor) return;
        for (const auto& mi : missiles)
            if (mi.info->dcc) {
                Unit u{ mi.x, mi.y, nullptr, mi.dir, nullptr, mi.born, -1 };
                u.missile = mi.info;
                out.push_back(u);
            }
        for (std::size_t i = 0; i < monsters.size(); ++i) {
            const auto& m = monsters[i];
            if (m.corpse_used) continue;
            if (std::abs(m.u.x - player.x) >= 14 || std::abs(m.u.y - player.y) >= 14) continue;
            out.push_back({ m.u.x, m.u.y, &scene->npc_anim(m.npc, m.mode), m.u.dir,
                            m.alive() ? &m.npc.name : nullptr, m.u.mode_ms, -10 - int(i) });
        }
    }
    // Over the world: the hovered (else attacked) monster's life bar, the
    // death message.
    void overlays(std::vector<std::uint8_t>& fb, int hovered) const {
        if (hovered >= 0) draw_monster_bar(fb, *scene, monsters[std::size_t(hovered)]);
        else if (attack_mon >= 0) draw_monster_bar(fb, *scene, monsters[std::size_t(attack_mon)]);
        if (pmode == kModeDD) {                    // ponytail: D2's death screen text isn't traced
            const std::string msg = "You have died.  Click or press Esc to continue.";
            const auto& pal = scene->act1_pal.entries().empty() ? scene->pal : scene->act1_pal;
            scene->font.draw_tinted(fb, kW, kH, pal, int(kW) / 2 - scene->font.measure(msg) / 2, int(kH) / 2 - 60, msg, 220, 60, 60);
        }
    }
};

}  // namespace
