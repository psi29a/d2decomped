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
// Fend (9 / 13) as Zeal, a hit per enemy in reach.
// Every other skill swings a plain attack for now.
inline bool skill_built(const d2d::rules::Skill& s) {
    return (s.srvdofunc == 2 && (s.srvstfunc == 32 || s.srvstfunc == 6 || s.srvstfunc == 39 || s.srvstfunc == 35)) || (s.srvstfunc == 24 && s.srvdofunc == 42)
        || (s.srvstfunc == 23 && (s.srvdofunc == 34 || s.srvdofunc == 35)) || (s.srvstfunc == 27 && s.srvdofunc == 50)
        || (s.srvstfunc == 37 && s.srvdofunc == 13) || (s.srvstfunc == 29 && s.srvdofunc == 64) || s.srvdofunc == 150
        || (s.srvstfunc == 5 && s.srvdofunc == 7) || (s.srvstfunc == 25 && s.srvdofunc == 46) || s.srvdofunc == 9 || s.srvdofunc == 70
        || (s.srvstfunc == 9 && s.srvdofunc == 13) || (s.srvstfunc == 7 && s.srvdofunc == 2);
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
    // The skill the player attacks with (Skills.txt id; 0 Attack), the one
    // this swing uses (Attack when it's not built or can't be paid for),
    // and the strikes still to come (Dragon Talon's kicks, Zeal's hits).
    int   attack_skill = 0, swing_skill = 0, kicks_left = 0;
    std::int64_t self_hurt = 0;
    // An SQ skill's sequence while it plays (sequences.hpp, for the weapon
    // class): its frames, each seq_frame_ms long, and the hits (event 1)
    // already struck.
    std::span<const d2d::rules::SeqFrame> seq{};
    std::uint32_t seq_frame_ms = 40;
    int seq_struck = 0;            // life (256ths) the player's own skills cost (Sacrifice), taken with the monsters' hits
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
    // ponytail: FHR / FBR as rate bonuses, not the class breakpoint tables.
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
        } else if (mode == kModeGH || mode == kModeBL) {
            len = len * 100 / std::uint32_t(100 + d2d::rules::effective_speed(mode == kModeGH ? pf.fhr : pf.fbr));
        }
        prate = len ? float(a.length_ms()) / float(len) : 1.f;
        pmode_until = mode == kModeDD ? 0 : ms + len;
    }
    [[nodiscard]] bool dead() const { return pmode == kModeDT || pmode == kModeDD; }

    // The player as combat sees them: item stats summed like the char
    // panel's (worn, charms, what's socketed), the weapon and shield worn,
    // the panel's defense and resistances.
    // ponytail: set bonuses and the weapon swap aren't counted.
    [[nodiscard]] d2d::rules::Fighter player_fighter(d2d::rules::Fighter* kick = nullptr,
                                                     const d2d::rules::StatSum* states = nullptr) const {
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
        if (weapon) {                                        // its own enhanced damage (op 13), sockets included
            add(weapon_sum, weapon->props);
            for (const auto& j : weapon->socketed_items) add(weapon_sum, socket_props(*scene, *weapon, j));
        }
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
        for (const auto& st : self_states) {
            const auto* s = scene->skills.get(st.skill);
            for (std::size_t i = 0; s && i < s->aurastat.size(); ++i)
                if (const int id = s->aurastat[i]; id >= 0 && std::size_t(id) < st_sum.size())
                    st_sum[std::size_t(id)] += d2d::rules::eval_calc(scene->skills, s->aura_calc[i], env, s->id, st.level);
        }
        pf = player_fighter(&pf_kick, &st_sum);
        pf.ias += int(st_sum[68]);
        pf.frw += int(st_sum[67]);
        pf.defense += int(pf.defense * st_sum[171] / 100);
        pf.defense = std::max(int(pf.defense + pf.defense * st_sum[182] / 100), 0);
    }

    // What calcs ask of the player (skills.hpp).
    [[nodiscard]] d2d::rules::CalcEnv calc_env() {
        return { skill_base, skill_level, nullptr, int(cc.stats.get(d2d::d2s::kLevel)), &rng };
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
        const auto ticks = d2d::rules::attack_ticks(int(seq.size()), int(a1.speed ? a1.speed : 256), pf.ias, pf.wsm);
        pmode_until = ms + std::uint32_t(ticks) * 40;
        seq_frame_ms = std::max<std::uint32_t>(std::uint32_t(ticks) * 40 / std::uint32_t(seq.size()), 1);
        prate = 1.f;
    }
    // What the player shows in a sequence: its frame's mode, and a start
    // time that lands the renderer (frame = elapsed / ms_per_frame) on it.
    [[nodiscard]] std::pair<int, std::uint32_t> seq_view(std::uint32_t ms) const {
        const auto& f = seq[std::min<std::size_t>((ms - player.mode_ms) / seq_frame_ms, seq.size() - 1)];
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
            sw.knockback = s->srvstfunc != 6 && (s->result_flags & 8) != 0;
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
        if (!seq.empty()) {
            const auto at = std::min<std::size_t>((ms - player.mode_ms) / seq_frame_ms, seq.size() - 1);
            int due = 0;
            for (std::size_t i = 0; i <= at; ++i) due += seq[i].event == 1;
            while (seq_struck < due) {
                ++seq_struck;
                // Frenzy's and Double Swing's second hand looks for another
                // target (FUN_0056bd10 on the odd frame, FUN_005d8e00 /
                // FUN_005d8470).
                if (const auto* s = scene->skills.get(swing_skill); s && seq_struck % 2 == 0 && (s->srvdofunc == 9 || s->srvdofunc == 70))
                    other_target();
                hit(ms);
            }
            return;
        }
        if (pstruck || attack_mon < 0
            || ms < player.mode_ms + std::uint32_t(float(player_anim(pmode).action_ms()) / prate)) return;
        pstruck = true;
        hit(ms);
    }
    void hit(std::uint32_t ms) {
        if (attack_mon < 0) return;
        auto& m = monsters[std::size_t(attack_mon)];
        if (!m.alive() || std::hypot(m.u.x - player.x, m.u.y - player.y) > kMeleeReach + 0.5f) return;
        auto sw = swing();
        auto f = sw.kick ? pf_kick : pf;
        const auto* s = scene->skills.get(swing_skill);
        const bool charging = s && s->srvstfunc == 23, finishing = finisher(s);
        std::erase_if(charges, [&](const Charge& c) { return ms >= c.until; });
        if (finishing) add_charges(f, sw);
        if (s && s->srvstfunc != 35 && std::ranges::contains(std::array{ 2, 13, 64, 7, 46, 9, 70 }, s->srvdofunc)) skill_element(f, *s);
        const int hp_before = m.hp;
        const auto target = std::size_t(attack_mon);
        const auto b = d2d::rules::player_blow(f, m.target(*scene), int(cc.stats.get(d2d::d2s::kLevel)), rng, sw);
        land(target, b, true, ms);
        if (b.hit && charging) charge(*s, ms);
        if (b.hit && finishing) release();
        if (b.hit && s && s->srvdofunc == 50) dragon_tail(*s, target, b.phys, ms);
        if (b.hit && s && s->srvdofunc == 9) frenzy(*s, ms);
        if (b.hit && s && s->srvstfunc == 7) impale_wear(*s);
        // Sacrifice's price (FUN_005ce8e0): calc2 % of the physical dealt,
        // no more than the target had left, off the player's life.
        if (b.hit && s && s->srvdofunc == 64) {
            const int dealt = std::min(d2d::rules::resisted(b.phys, m.target(*scene).res[0]), std::max(hp_before, 0));
            const auto env = calc_env();
            self_hurt += (std::int64_t(dealt) << 8) * d2d::rules::eval_calc(scene->skills, s->calc[1], env, s->id,
                                                                            skill_level ? skill_level(s->id) : 1) / 100;
        }
        if (!monsters[target].alive()) attack_mon = -1;
    }

    // The skill's own element on its hit (FUN_0056e0c0: EMin..EMax with
    // brackets and synergy; Power Strike's lightning). Stun is elsewhere.
    void skill_element(d2d::rules::Fighter& f, const d2d::rules::Skill& s) {
        if (s.etype < 0 || s.etype >= 5) return;
        const int lvl = skill_level ? skill_level(s.id) : 1;
        const auto env = calc_env();
        auto& [lo, hi] = f.elem[std::size_t(s.etype)];
        lo += d2d::rules::elem_damage(scene->skills, s, env, lvl, false) >> 8;
        hi += d2d::rules::elem_damage(scene->skills, s, env, lvl, true) >> 8;
        if (s.etype == 2) f.cold_len = std::max(f.cold_len, d2d::rules::elem_length(scene->skills, s, env, lvl));
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
    // A finishing hit releases every charge (FUN_005d5220): each skill's
    // srvprgfunc per charge (Fists of Fire's fire bursts, Claws of
    // Thunder's novas, Blades of Ice's, Royal Strike's meteor, chain
    // lightning and ice), then the charges are gone.
    // ponytail: those srvprgfunc missiles aren't built; logged once.
    void release() {
        for (const auto& c : charges) {
            const auto* s = scene->skills.get(c.skill);
            if (!s || std::ranges::all_of(s->prgfunc, [](int p) { return p <= 0; })
                || std::ranges::find(told, -c.skill) != told.end()) continue;
            told.push_back(-c.skill);
            d2d::log::info("not implemented: {}'s release (srvprgfunc {} {} {})", s->name, s->prgfunc[0], s->prgfunc[1], s->prgfunc[2]);
        }
        charges.clear();
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
            if (hurt(*scene, m, d2d::rules::resisted(fire, m.target(*scene).res[2]), ms)) killed(i, ms);
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
                    Missile a{ &mi, u.x, u.y, dx / d * v, dy / d * v, direction32(dx, dy), ms, ms + std::uint32_t(mi.range) * 40 };
                    a.min = merc_st.dmg_min; a.max = merc_st.dmg_max; a.ar = merc_st.ar; a.level = merc_st.level; a.friendly = true;
                    missiles.push_back(a);
                } else if (m.alive() && d <= kMeleeReach + 0.3f) {
                    land(std::size_t(merc_target), d2d::rules::player_blow(merc_fighter(), m.target(*scene), merc_st.level, rng), false, ms);
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
    bool player_modes(const Mouse& mouse, std::uint32_t ms) {
        if (dead()) {
            if (pmode == kModeDT && ms >= pmode_until) set_pmode(kModeDD, ms);
            return pmode == kModeDD && mouse.press_this_frame;
        }
        if (attack_mode(pmode)) {
            strike(ms);
            if (ms >= pmode_until) {
                if (kicks_left > 0 && next_target()) {
                    --kicks_left;                            // Talon's next kick (FUN_005d5a30), Zeal's next hit (FUN_005dbc60)
                    set_pmode(swing_mode(), ms);
                    pstruck = false;
                    return false;
                }
                kicks_left = 0;
                seq = {};
                pmode = -1; player.mode_ms = ms;
                if (!mouse.down) attack_mon = -1;
            }
        } else if (pmode == kModeSC) {                       // a self cast: its state on the action frame
            if (!pstruck && ms >= player.mode_ms + player_anim(kModeSC).action_ms()) {
                pstruck = true;
                if (const auto* s = scene->skills.get(swing_skill)) {
                    const int lvl = skill_level ? skill_level(s->id) : 1;
                    const auto env = calc_env();
                    std::erase_if(self_states, [&](const SelfState& st) { return st.skill == s->id; });
                    self_states.push_back({ s->id, lvl, ms + std::uint32_t(std::max(d2d::rules::eval_calc(scene->skills, s->auralen, env, s->id, lvl), 1)) * 40 });
                }
            }
            if (ms >= pmode_until) { pmode = -1; player.mode_ms = ms; }
        } else if ((pmode == kModeGH || pmode == kModeBL) && ms >= pmode_until) {
            pmode = -1; player.mode_ms = ms;
        }
        return false;
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
            std::array<Foe, 2> foes{ Foe{ player.x, player.y, int(cc.stats.get(d2d::d2s::kLevel)), true, player.walking, pf },
                                     Foe{ merc ? merc->x : 0, merc ? merc->y : 0, merc_st.level, merc && merc_mode != "DT",
                                          merc && merc->walking, merc_fighter() } };
            for (std::size_t i = 0; i < monsters.size(); ++i) {
                auto& m = monsters[i];
                if (std::abs(m.u.x - player.x) < 30 && std::abs(m.u.y - player.y) < 30
                    && monster_update(*scene, *level, m, foes, rng, ms, dt, crowd, missiles))
                    killed(i, ms);                           // on the player's thorns
            }
            monster_dots(ms, dt);
            // The merc's arrows strike the first live monster they reach.
            missiles_update(*level, missiles, foes, rng, ms, dt, [&](const Missile& a) {
                for (std::size_t i = 0; i < monsters.size(); ++i) {
                    auto& m = monsters[i];
                    if (!m.alive() || std::hypot(m.u.x - a.x, m.u.y - a.y) > 0.5f) continue;
                    land(i, d2d::rules::player_blow(d2d::rules::simple_fighter(a.min, a.max, a.ar), m.target(*scene), a.level, rng),
                         false, ms);
                    return true;
                }
                return false;
            });
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
        if (level != &scene->moor) return;
        for (const auto& mi : missiles)
            if (mi.info->dcc) {
                Unit u{ mi.x, mi.y, nullptr, mi.dir, nullptr, mi.born, -1 };
                u.missile = mi.info;
                out.push_back(u);
            }
        for (std::size_t i = 0; i < monsters.size(); ++i) {
            const auto& m = monsters[i];
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
