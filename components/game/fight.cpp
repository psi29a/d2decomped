// Definitions for fight.hpp: Fight, the combat subsystem the World owns.
#include "fight.hpp"
#include "ai.hpp"
#include "character.hpp"
#include "gamedata.hpp"
#include "log.hpp"

namespace d2d::game {

auto Fight::monster_index(int id) const -> int {
        if (id < 0) return -1;
        const auto it = std::ranges::find(monsters, id, &Monster::id);
        return it == monsters.end() ? -1 : int(it - monsters.begin());
    }

auto Fight::add_monster(Monster m) -> void { m.id = next_id++; monsters.push_back(std::move(m)); }

auto Fight::own_stats() const -> d2d::d2s::Stats {
        using namespace d2d::d2s;
        auto st = cc.stats;
        st.v[kMaxLife] -= bo_life + item_max[0]; st.v[kMaxMana] -= bo_mana + item_max[1]; st.v[kMaxStamina] -= item_max[2];
        return st;
    }

auto Fight::new_game(int difficulty) -> void {
        game_difficulty = difficulty;
        monsters.clear();
        mon_level = nullptr;
        amplified = {};
        kept.clear();
        next_id = 1;
        missiles.clear();
        regen.clear();
        charges.clear();
        self_states.clear();
        boost = {};
        pets.clear();
        attack_mon = -1;
        pmode = -1;
    }

auto Fight::merc_joins() -> void {
        const auto& h = cc.header;
        merc_st = d2d::rules::merc_stats(scene->rules, h.merc_type, h.merc_exp);
        merc_life = merc_st.life;
        merc_mode = "NU"; merc_target = -1;
    }

auto Fight::revive(std::uint32_t ms) -> void {
        using namespace d2d::d2s;
        cc.stats.v[kLife] = cc.stats.v[kMaxLife];
        regen.clear();
        charges.clear();
        self_states.clear();
        pmode = -1; player.mode_ms = ms; attack_mon = -1;
        if (merc) { merc->x = player.x + 1; merc->y = player.y + 1; merc->path.clear(); merc_mode = "NU"; merc_target = -1; }
    }

auto Fight::drink(int col, std::uint32_t ms) -> void {
        if (!dead()) potion(d2d::rules::drink_belt(scene->rules, cc.items, col), ms);
    }

auto Fight::drink_item(int id, std::uint32_t ms) -> void {
        if (!dead()) potion(d2d::rules::drink_item(scene->rules, cc.items, id), ms);
    }

auto Fight::potion(const std::string& code, std::uint32_t ms) -> void {
        using namespace d2d::d2s;
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

auto Fight::apply_regen(std::uint32_t ms, std::uint32_t last_ms) -> void {
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

auto Fight::monster_sounds(Monster& m, std::uint32_t ms) -> void {
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

auto Fight::gfx() const -> const GameData::Appearance& {
        return cc.appearance ? *cc.appearance : scene->starting_gear[std::size_t(kUiToSaveClass[std::max(cc.selected, 0)])];
    }

auto Fight::player_anim(int mode) const -> const GameData::AnimTiming& {
        return scene->composite_timing(kUiToSaveClass[std::max(cc.selected, 0)], mode, gfx());
    }

auto Fight::set_pmode(int mode, std::uint32_t ms) -> void {
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

auto Fight::dead() const -> bool { return pmode == kModeDT || pmode == kModeDD; }

auto Fight::player_fighter(d2d::rules::Fighter* kick ,
                                                     const d2d::rules::StatSum* states ,
                                                     const std::vector<d2d::rules::PassiveStat>* passives ,
                                                     d2d::rules::StatSum* sum_out ) const -> d2d::rules::Fighter {
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

auto Fight::update_fighters(std::uint32_t ms) -> void {
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
        if (ms < boost.until)
            for (const auto& [id, v] : boost.stats) if (std::size_t(id) < st_sum.size()) st_sum[std::size_t(id)] += v;
        // Battle Orders (item_maxhp_percent 76, item_maxmana_percent 77): the
        // maxima up while it lasts, life and mana with them.
        {
            using namespace d2d::d2s;
            const std::int64_t base_life = cc.stats.v[kMaxLife] - bo_life, base_mana = cc.stats.v[kMaxMana] - bo_mana;
            const std::int64_t want_life = base_life * st_sum[76] / 100, want_mana = base_mana * st_sum[77] / 100;
            cc.stats.v[kMaxLife] += want_life - bo_life; cc.stats.v[kLife] = std::min(cc.stats.v[kLife] + std::max<std::int64_t>(want_life - bo_life, 0), cc.stats.v[kMaxLife]);
            cc.stats.v[kMaxMana] += want_mana - bo_mana; cc.stats.v[kMana] = std::min(cc.stats.v[kMana] + std::max<std::int64_t>(want_mana - bo_mana, 0), cc.stats.v[kMaxMana]);
            bo_life = want_life; bo_mana = want_mana;
            // Items' +life / mana / stamina (and their attributes' share):
            // the maxima follow what's worn; life and mana don't rise with
            // them, only stay under them.
            constexpr std::array<std::pair<int, int>, 3> kMax{ { { kMaxLife, kLife }, { kMaxMana, kMana }, { kMaxStamina, kStamina } } };
            constexpr std::array<std::size_t, 3> kBonus{ 7, 9, 11 };
            for (std::size_t k = 0; k < 3; ++k) {
                const std::int64_t want = cc.panel.bonus[kBonus[k]] * 256;
                auto& mx = cc.stats.v[std::size_t(kMax[k].first)];
                mx += want - item_max[k];
                cc.stats.v[std::size_t(kMax[k].second)] = std::min(cc.stats.v[std::size_t(kMax[k].second)], mx);
                item_max[k] = want;
            }
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

auto Fight::calc_env() -> d2d::rules::CalcEnv {
        return { skill_base, skill_level, [this](int id) { return id >= 0 && std::size_t(id) < psum.size() ? int(psum[std::size_t(id)]) : 0; },
                 int(cc.stats.get(d2d::d2s::kLevel)), &rng };
    }

auto Fight::start_swing(std::uint32_t ms) -> bool {
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

auto Fight::start_sequence(std::uint32_t ms) -> void {
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

auto Fight::start_move(const d2d::rules::Skill& s, std::uint32_t ms) -> void {
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

auto Fight::seq_at(std::uint32_t ms) const -> std::size_t {
        const auto i = std::size_t((ms - player.mode_ms) / seq_frame_ms);
        return seq_loop ? i % seq.size() : std::min(i, seq.size() - 1);
    }

auto Fight::seq_events(std::uint32_t ms) const -> int {
        const auto i = std::size_t((ms - player.mode_ms) / seq_frame_ms);
        const auto per = std::ranges::count(seq, 1, &d2d::rules::SeqFrame::event);
        const auto last = seq_loop ? i : std::min(i, seq.size() - 1);
        int n = seq_loop ? int(last / seq.size() * std::size_t(per)) : 0;
        for (std::size_t k = 0; k <= last % (seq_loop ? seq.size() : ~std::size_t{ 0 }) && k < seq.size(); ++k) n += seq[k].event == 1;
        return n;
    }

auto Fight::seq_view(std::uint32_t ms) const -> std::pair<int, std::uint32_t> {
        const auto& f = seq[seq_at(ms)];
        const auto mpf = player_anim(f.mode).ms_per_frame();
        return { f.mode, ms - mpf * f.frame - mpf / 2 };
    }

auto Fight::swing_mode() const -> int {
        const auto* s = scene->skills.get(swing_skill);
        if (!s || swing_skill == 0) return kModeA1;
        return s->anim == "KK" ? kModeKK : s->anim == "S1" ? kModeS1 : kModeA1;
    }

auto Fight::swing() -> d2d::rules::Swing {
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

auto Fight::land(std::size_t i, const d2d::rules::Blow& b, bool by_player, std::uint32_t ms) -> void {
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
        // Life Tap (damagedinmelee / damagedbymissile, func 5): calc1 % of the
        // damage heals whoever dealt it.
        if (const auto* lt = m.curse.skill >= 0 ? scene->skills.get(m.curse.skill) : nullptr; by_player && lt && lt->auratarget == "lifetap")
            cc.stats.v[kLife] = std::min(cc.stats.v[kMaxLife], cc.stats.v[kLife]
                                         + ((std::int64_t(b.damage) * calc(*lt, lt->calc[0], m.curse.level) / 100) << 8));
        if (hurt(*scene, m, b.damage, ms)) { killed(i, ms); return; }
        if (b.knockback) {                                   // a step straight back, if there's room
            const float dx = m.u.x - player.x, dy = m.u.y - player.y, d = std::max(std::hypot(dx, dy), 0.01f);
            const float nx = m.u.x + dx / d * 0.6f, ny = m.u.y + dy / d * 0.6f;
            if (!level->unit_blocked(nx, ny)) { m.u.x = nx; m.u.y = ny; }
        }
    }

auto Fight::monster_dots(std::uint32_t ms, float dt) -> void {
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

auto Fight::strike(std::uint32_t ms) -> void {
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

auto Fight::hit(std::uint32_t ms, float reach ) -> void {
        if (attack_mon < 0) return;
        auto& m = monsters[std::size_t(attack_mon)];
        if (!m.alive() || std::hypot(m.u.x - player.x, m.u.y - player.y) > reach) return;
        auto sw = swing();
        auto f = sw.kick ? pf_kick : pf;
        const auto* s = scene->skills.get(swing_skill);
        const bool charging = s && s->srvstfunc == 23, finishing = finisher(s);
        std::erase_if(charges, [&](const Charge& c) { return ms >= c.until; });
        if (finishing) add_charges(f, sw);
        if (s && s->srvstfunc != 35 && std::ranges::contains(std::array{ 2, 13, 64, 7, 46, 9, 70, 76, 67, 78, 32, 11, 14, 121 }, s->srvdofunc)) skill_element(f, *s);
        // Hunger (do 122, FUN_005c7f10): calc1 damage %, calc2 / calc3 life /
        // mana stolen.
        if (s && s->srvdofunc == 122) {
            const int lvl = skill_level ? skill_level(s->id) : 1;
            sw.ed_pct += calc(*s, s->calc[0], lvl);
            f.life_steal += calc(*s, s->calc[1], lvl); f.mana_steal += calc(*s, s->calc[2], lvl);
        }
        const int hp_before = m.hp;
        const auto target = std::size_t(attack_mon);
        const auto b = d2d::rules::player_blow(f, target_of(target), int(cc.stats.get(d2d::d2s::kLevel)), rng, sw);
        land(target, b, true, ms);
        if (b.hit && charging) charge(*s, ms);
        if (b.hit && finishing) release(target, ms);
        if (b.hit && s && s->srvdofunc == 50) dragon_tail(*s, target, b.phys, ms);
        if (b.hit && s && (s->srvdofunc == 9 || s->srvdofunc == 120)) frenzy(*s, ms);   // Feral Rage / Maul stack as Frenzy
        if (b.hit && s && s->srvstfunc == 7) impale_wear(*s);
        if (b.hit && s && (s->srvdofunc == 11 || s->srvdofunc == 14)) strike_bolts(*s, target, ms);
        // Conversion (do 79): at calc1 % the monster turns for auralen frames.
        // ponytail: it doesn't fight for the player; it stops seeing the
        // player's side (as Confuse here).
        if (b.hit && s && s->srvdofunc == 79 && monsters[target].alive()) {
            const int lvl = skill_level ? skill_level(s->id) : 1;
            if (int(rng(100)) < calc(*s, s->calc[0], lvl))
                monsters[target].cry = { s->id, lvl, ms + std::uint32_t(std::max(calc(*s, s->auralen, lvl), 25)) * 40 };
        }
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

auto Fight::strike_bolts(const d2d::rules::Skill& s, std::size_t i, std::uint32_t ms) -> void {
        const auto m = scene->missiles.find(s.srvmissilea);
        if (m == scene->missiles.end()) return;
        const int lvl = skill_level ? skill_level(s.id) : 1;
        const float x = monsters[i].u.x, y = monsters[i].u.y, dx = x - player.x, dy = y - player.y;
        if (s.srvdofunc == 11) {
            for (int k = 0; k < std::max(calc(s, s.calc[0], lvl), 1); ++k) {
                const float a = (float(rng(81)) - 40) * 3.14159265f / 180;
                launch(m->second, s, lvl, x, y, dx * std::cos(a) - dy * std::sin(a), dx * std::sin(a) + dy * std::cos(a),
                       m->second.range + m->second.lev_range * lvl, ms).struck->push_back(int(i));
            }
            return;
        }
        const int r = calc(s, s.calc[0], lvl);
        int best = -1; float bd = 1e9f;
        for (std::size_t j = 0; j < monsters.size(); ++j)
            if (const float d = std::hypot(monsters[j].u.x - x, monsters[j].u.y - y); j != i && monsters[j].alive() && d * 5 <= float(r) && d < bd) {
                bd = d; best = int(j);
            }
        if (best < 0) return;
        const auto& o = monsters[std::size_t(best)];
        launch(m->second, s, lvl, x, y, o.u.x - x, o.u.y - y, m->second.range, ms).hops = calc(s, s.calc[1], lvl);
    }

auto Fight::corpse_blast(const d2d::rules::Skill& s, int lvl, std::size_t c, std::uint32_t ms) -> void {
        auto& corpse = monsters[c];
        corpse.corpse_used = true;
        const int lo = calc(s, s.calc[0], lvl), hi = std::max(calc(s, s.calc[1], lvl), lo);
        const int dmg = int(std::int64_t(corpse.st.hp) * rng.range(lo, hi) / 100);
        const int r = std::max(calc(s, s.aurarange, lvl) / 2, 1);
        for (std::size_t j = 0; j < monsters.size(); ++j)
            if (monsters[j].alive() && std::hypot(monsters[j].u.x - corpse.u.x, monsters[j].u.y - corpse.u.y) * 5 <= float(r)) {
                const auto t = target_of(j);
                d2d::rules::Blow b{ .hit = true };
                b.damage = d2d::rules::resisted(dmg / 2, t.res[0]) + d2d::rules::resisted(dmg - dmg / 2, t.res[2]);
                land(j, b, true, ms);
            }
    }

auto Fight::spot_skill(const d2d::rules::Skill& s) -> bool {
        return std::ranges::contains(std::array{ 33, 51, 20, 55, 63, 27, 30, 6, 61, 59, 71, 69, 72, 77, 21, 52, 74 }, s.srvdofunc);
    }

auto Fight::spot(const d2d::rules::Skill& s, std::uint32_t ms) -> void {
        const int lvl = skill_level ? skill_level(s.id) : 1;
        const auto env = calc_env();
        const auto md = d2d::rules::missile_damage(scene->skills, s, env, lvl);
        auto within = [&](float x, float y, int r, auto&& f) {
            for (std::size_t j = 0; j < monsters.size(); ++j)
                if (monsters[j].alive() && std::hypot(monsters[j].u.x - x, monsters[j].u.y - y) * 5 <= float(r)) f(j);
        };
        switch (s.srvdofunc) {
            case 33:
                if (attack_mon >= 0 && monsters[std::size_t(attack_mon)].alive()) {
                    const auto i = std::size_t(attack_mon);
                    auto b = d2d::rules::missile_blow(md, target_of(i), pierce(), rng);
                    b.knockback = int(rng(100)) < calc(s, s.calc[0], lvl);
                    land(i, b, true, ms);
                }
                break;
            case 51:
                within(cast_x, cast_y, calc(s, s.aurarange, lvl), [&](std::size_t j) { land(j, d2d::rules::missile_blow(md, target_of(j), pierce(), rng), true, ms); });
                break;
            case 20: {
                const int pct = calc(s, s.calc[0], lvl);
                within(player.x, player.y, calc(s, s.aurarange, lvl), [&](std::size_t j) {
                    d2d::rules::Blow b{ .hit = true };
                    b.damage = d2d::rules::resisted(monsters[j].hp * pct / 100, target_of(j).res[3]);
                    land(j, b, true, ms);
                });
                break;
            }
            case 55: case 63: {
                const int c = corpse_near(cast_x, cast_y);
                if (c < 0) break;
                auto& corpse = monsters[std::size_t(c)];
                corpse.corpse_used = true;
                if (s.srvdofunc == 63) {
                    if (const auto m = scene->missiles.find(s.srvmissilea); m != scene->missiles.end())
                        launch(m->second, s, lvl, corpse.u.x, corpse.u.y, 0, 0, m->second.range, ms);
                    break;
                }
                corpse_blast(s, lvl, std::size_t(c), ms);
                break;
            }
            case 27: case 77: case 52: {
                // Teleport; Leap (FUN_... do 77: the jump, then knockback
                // round the landing within aurarange); Dragon Flight (do 52:
                // next to the monster, then a kick).
                // ponytail: Leap and Dragon Flight arrive at once (no jump,
                // no flight); Leap's range (calc1) isn't checked.
                float tx = cast_x, ty = cast_y;
                const int tgt = attack_mon >= 0 && monsters[std::size_t(attack_mon)].alive() ? attack_mon : -1;
                if (s.srvdofunc == 52) {
                    if (tgt < 0) break;
                    const auto& m = monsters[std::size_t(tgt)];
                    const float dx = player.x - m.u.x, dy = player.y - m.u.y, d = std::max(std::hypot(dx, dy), 0.01f);
                    tx = m.u.x + dx / d * 0.8f; ty = m.u.y + dy / d * 0.8f;
                }
                if (level == &scene->town || level->unit_blocked(tx, ty)) break;
                player.x = tx; player.y = ty;
                player.path.clear(); player.walking = false;
                player.goal_x = tx; player.goal_y = ty;
                if (s.srvdofunc == 77)
                    within(tx, ty, calc(s, s.aurarange, lvl), [&](std::size_t j) {
                        d2d::rules::Blow b{ .hit = true };
                        b.knockback = true;
                        land(j, b, true, ms);
                    });
                if (s.srvdofunc == 52) {
                    d2d::rules::Swing sw;
                    sw.kick = true;
                    land(std::size_t(tgt), d2d::rules::player_blow(pf_kick, target_of(std::size_t(tgt)), int(cc.stats.get(d2d::d2s::kLevel)), rng, sw),
                         true, ms);
                }
                break;
            }
            case 21:                                         // Telekinesis (do 21): its damage and a knockback on the monster
                if (attack_mon >= 0 && monsters[std::size_t(attack_mon)].alive()) {
                    auto b = d2d::rules::missile_blow(md, target_of(std::size_t(attack_mon)), pierce(), rng);
                    b.knockback = true;
                    land(std::size_t(attack_mon), b, true, ms);
                }
                break;
            // Curses (do 30, FUN_005c37c0; 61 Confuse, FUN_005c3f20): the
            // auratargetstate on every monster within aurarange of the point
            // for auralen frames (FUN_0056e780), one curse at a time. Inner
            // Sight / Slow Missiles (do 6, FUN_005db1c0) round the caster;
            // Attract (59, FUN_005c3b90) and Taunt (71, FUN_005d8570) on the
            // monster.
            case 30: case 61: case 6: case 59: case 71: {
                const std::uint32_t until = ms + std::uint32_t(std::max(calc(s, s.auralen, lvl), 25)) * 40;
                const bool curse = s.srvdofunc == 30 || s.srvdofunc == 61 || s.srvdofunc == 59;
                auto put = [&](std::size_t j) { (curse ? monsters[j].curse : monsters[j].cry) = { s.id, lvl, until }; };
                if (s.srvdofunc == 59 || s.srvdofunc == 71) {
                    if (attack_mon >= 0 && monsters[std::size_t(attack_mon)].alive()) put(std::size_t(attack_mon));
                } else {
                    within(s.srvdofunc == 6 ? player.x : cast_x, s.srvdofunc == 6 ? player.y : cast_y, calc(s, s.aurarange, lvl), put);
                }
                break;
            }
            // Double Throw (do 74, FUN_005d88b0): each hand's throwing weapon
            // flies at the target as its missile (weapons.txt missiletype),
            // with calc1 damage % (stat 25).
            // ponytail: toht (stat 19) isn't added; no ammo is spent.
            case 74:
                for (const auto& it : cc.items)
                    if (it.location == 1 && (it.slot == 4 || it.slot == 5))
                        if (const auto t = scene->thrown.find(it.code); t != scene->thrown.end())
                            if (const auto m = scene->missiles.find(t->second); m != scene->missiles.end())
                                launch(m->second, s, lvl, player.x, player.y, cast_x - player.x, cast_y - player.y,
                                       m->second.range + m->second.lev_range * lvl, ms).ed_pct = calc(s, s.calc[0], lvl);
                break;
            // Find Potion (do 69, FUN_005d81c0) / Find Item (72, FUN_005d8780):
            // a corpse not yet searched (state 0x76), at calc1 %: a potion
            // (FUN_005d8100), or the monster's treasure again (FUN_005a8000,
            // a tier by Param1..4).
            // ponytail: the potion by the character's level (FUN_005d8100's
            // table isn't traced); Find Item rolls the monster's own class.
            case 69: case 72: {
                const int c = corpse_near(cast_x, cast_y);
                if (c < 0) break;
                auto& corpse = monsters[std::size_t(c)];
                corpse.corpse_used = true;
                if (int(rng(100)) >= calc(s, s.calc[0], lvl)) break;
                if (s.srvdofunc == 72) { loot.drop(corpse, ms); break; }
                const int tier = std::clamp(1 + int(cc.stats.get(d2d::d2s::kLevel)) / 12, 1, 5);
                const std::string code = rng(20) == 0 ? "rvs" : (rng(2) ? "hp" : "mp") + std::to_string(tier);
                loot.put({ code, 2, 0 }, corpse.u.x, corpse.u.y, corpse.st.level, ms);
                break;
            }
            default: break;
        }
    }

auto Fight::skill_element(d2d::rules::Fighter& f, const d2d::rules::Skill& s) -> void {
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

auto Fight::self_state(const d2d::rules::Skill& s, std::uint32_t ms) -> void {
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

auto Fight::start_state(const d2d::rules::Skill& s, int lvl, std::uint32_t ms) -> void {
        const int len = calc(s, s.auralen, lvl);
        const std::uint32_t until = len > 0 ? ms + std::uint32_t(len) * 40 : ~0u;
        std::erase_if(self_states, [&](const SelfState& st) { return st.skill == s.id; });
        self_states.push_back({ s.id, lvl, until });
        for (std::size_t k = 0; k < s.aurastat.size(); ++k)
            if (s.aurastat[k] == kBoneArmor) absorb_pool = calc(s, s.aura_calc[k], lvl) >> 8, absorb_skill = s.id;
        if (s.srvdofunc == 47)
            for (auto& m : monsters)
                if (m.alive() && std::hypot(m.u.x - player.x, m.u.y - player.y) * 5 <= float(calc(s, s.aurarange, lvl)))
                    m.cry = { s.id, lvl, until };
    }

auto Fight::state_of(std::string_view name) const -> const SelfState* {
        for (const auto& st : self_states)
            if (const auto* k = scene->skills.get(st.skill); k && k->aurastate == name) return &st;
        return nullptr;
    }

auto Fight::absorb(int damage) -> int {
        using namespace d2d::d2s;
        if (damage <= 0) return 0;
        if (absorb_pool > 0 && std::ranges::contains(self_states, absorb_skill, &SelfState::skill)) {
            const int a = std::min(absorb_pool, damage);
            absorb_pool -= a; damage -= a;
            if (absorb_pool <= 0) std::erase_if(self_states, [&](const SelfState& st) { return st.skill == absorb_skill; });
        }
        if (const auto* es = state_of("energyshield"); es && damage > 0) {
            const auto* k = scene->skills.get(es->skill);
            const int part = damage * std::clamp(calc(*k, k->calc[0], es->level), 0, 95) / 100;
            const int per16 = std::max(calc(*k, k->calc[1], es->level), 1);
            const int can = int(std::min<std::int64_t>(part, (cc.stats.v[kMana] >> 8) * 16 / per16));
            cc.stats.v[kMana] -= (std::int64_t(can) * per16 / 16) << 8;
            damage -= can;
        }
        return damage;
    }

auto Fight::buff_events(Foe& me, std::uint32_t ms) -> void {
        for (const auto* m : me.melee_by) {
            const auto i = std::size_t(m - monsters.data());
            if (i >= monsters.size() || !monsters[i].alive()) continue;
            if (const auto* fa = state_of("frozenarmor")) {
                const auto* k = scene->skills.get(fa->skill);
                const int frames = std::max(calc(*k, k->calc[0], fa->level), 1);
                monsters[i].stun_until = std::max(monsters[i].stun_until, ms + std::uint32_t(frames) * 40);
                monsters[i].chill_until = std::max(monsters[i].chill_until, ms + std::uint32_t(frames) * 40);
            }
            if (const auto* sa = state_of("shiverarmor")) {
                const auto* k = scene->skills.get(sa->skill);
                land(i, d2d::rules::missile_blow(d2d::rules::missile_damage(scene->skills, *k, calc_env(), sa->level), target_of(i), pierce(), rng),
                     true, ms);
            }
        }
        if (const auto* ca = state_of("chillingarmor"); ca && me.missile_hits > 0) {
            const auto* k = scene->skills.get(ca->skill);
            const auto mi = scene->missiles.find(k->srvmissilea);
            int best = -1; float bd = 12.f;
            for (std::size_t j = 0; j < monsters.size(); ++j)
                if (const float d = std::hypot(monsters[j].u.x - player.x, monsters[j].u.y - player.y); monsters[j].alive() && d < bd) { bd = d; best = int(j); }
            if (best >= 0 && mi != scene->missiles.end()) {
                const auto& o = monsters[std::size_t(best)];
                launch(mi->second, *k, ca->level, player.x, player.y, o.u.x - player.x, o.u.y - player.y, mi->second.range, ms);
            }
        }
    }

auto Fight::buff_tick(std::uint32_t ms) -> void {
        if (const auto* bz = state_of("blaze"); bz && player.walking && ms / 40 != blaze_frame) {
            blaze_frame = ms / 40;
            const auto* k = scene->skills.get(bz->skill);
            if (const auto mi = scene->missiles.find(k->srvmissilea); mi != scene->missiles.end())
                launch(mi->second, *k, bz->level, player.x, player.y, 0, 0, mi->second.range + mi->second.lev_range * bz->level, ms);
        }
        // Armageddon / Hurricane (do 124, FUN_005c8190: the state and its
        // timer event, FUN_005417d0(5, ...)): Armageddon drops its control
        // row (hit 56) on a spot within aurarange every 5 frames; Hurricane
        // strikes what's within aurarange with its cold once a second. Blade
        // Shield (do 54, FUN_005d7e10 -> FUN_005d7ce0) strikes one monster
        // within aurarange every 10 frames.
        // ponytail: the timer's period (FUN_004efc80) and the events aren't
        // traced: the paces are by eye.
        if (const auto* ar = state_of("armageddon"); ar && (ms / 40) % 5 == 0 && ms / 40 != storm_frame) {
            const auto* k = scene->skills.get(ar->skill);
            if (const auto mi = scene->missiles.find(k->srvmissilea); mi != scene->missiles.end()) {
                const int r = std::max(calc(*k, k->aurarange, ar->level), 1);
                launch(mi->second, *k, ar->level, player.x + float(int(rng(2 * r + 1)) - r) / 5, player.y + float(int(rng(2 * r + 1)) - r) / 5,
                       0, 0, mi->second.range, ms);
            }
        }
        for (const char* name : { "hurricane", "bladeshield" })
            if (const auto* st = state_of(name); st && ms / 40 != storm_frame && (ms / 40) % (name[0] == 'h' ? 25 : 10) == 0) {
                const auto* k = scene->skills.get(st->skill);
                const auto md = d2d::rules::missile_damage(scene->skills, *k, calc_env(), st->level);
                const int r = std::max(calc(*k, k->aurarange, st->level), 1);
                for (std::size_t j = 0; j < monsters.size(); ++j)
                    if (monsters[j].alive() && std::hypot(monsters[j].u.x - player.x, monsters[j].u.y - player.y) * 5 <= float(r)) {
                        land(j, d2d::rules::missile_blow(md, target_of(j), pierce(), rng), true, ms);
                        if (name[0] == 'b') break;
                    }
            }
        storm_frame = ms / 40;
        if (const auto* ts = state_of("thunderstorm"); ts && ms >= storm_next) {
            storm_next = ms + 1600;
            const auto* k = scene->skills.get(ts->skill);
            int best = -1; float bd = 5.f;
            for (std::size_t j = 0; j < monsters.size(); ++j)
                if (const float d = std::hypot(monsters[j].u.x - player.x, monsters[j].u.y - player.y); monsters[j].alive() && d < bd) { bd = d; best = int(j); }
            if (best >= 0)
                land(std::size_t(best), d2d::rules::missile_blow(d2d::rules::missile_damage(scene->skills, *k, calc_env(), ts->level),
                                                                 target_of(std::size_t(best)), pierce(), rng), true, ms);
        }
    }

auto Fight::monster_states(std::uint32_t ms) -> void {
        for (auto& m : monsters) {
            m.dmg_pct = m.speed_pct = m.reflect_pct = 0;
            for (auto* st : { &m.curse, &m.cry }) {
                if (st->skill < 0) continue;
                if (ms >= st->until) { *st = {}; continue; }
                const auto* k = scene->skills.get(st->skill);
                if (!k) continue;
                for (std::size_t a = 0; a < k->aurastat.size(); ++a) {
                    if (k->aurastat[a] == 25) m.dmg_pct += calc(*k, k->aura_calc[a], st->level);
                    if (k->aurastat[a] == 67) m.speed_pct += calc(*k, k->aura_calc[a], st->level);
                }
                const auto& n = k->auratarget;
                if (n == "ironmaiden") m.reflect_pct += calc(*k, k->calc[0], st->level);
                if (n == "terror") m.flee_until = std::max(m.flee_until, st->until);
                if (n == "dimvision" || n == "cloaked" || n == "confuse" || n == "attract" || n == "conversion") m.blind_until = st->until;
            }
        }
    }

auto Fight::charge(const d2d::rules::Skill& s, std::uint32_t ms) -> void {
        const int lvl = skill_level ? skill_level(s.id) : 1;
        auto it = std::ranges::find(charges, s.id, &Charge::skill);
        if (it == charges.end()) it = charges.insert(charges.end(), Charge{ s.id });
        it->level = std::max(it->level, lvl);
        it->count = std::min(it->count + 1, 3);
        const auto env = calc_env();
        it->until = ms + std::uint32_t(std::max(d2d::rules::eval_calc(scene->skills, s.auralen, env, s.id, lvl), 1)) * 40;
    }

auto Fight::add_charges(d2d::rules::Fighter& f, d2d::rules::Swing& sw) -> void {
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

auto Fight::row_damage(const GameData::MissileInfo& mi, int lvl) -> d2d::rules::MissileDamage {
        auto md = d2d::rules::row_damage(mi.etype, mi.emin, mi.emax, mi.emin_lev, mi.emax_lev, mi.hitshift, mi.elen, mi.elen_lev, lvl);
        md.srcdam = mi.src_damage;
        return md;
    }

auto Fight::release(std::size_t on, std::uint32_t ms) -> void {
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

auto Fight::prg(const d2d::rules::Skill& s, int func, int count, int lvl, float tx, float ty, std::uint32_t ms) -> void {
        if (func <= 0) return;
        const auto env = calc_env();
        const auto& mname = count <= 1 ? s.srvmissilea : count == 2 ? s.srvmissileb : s.srvmissilec;
        const auto mit = scene->missiles.find(mname);
        const GameData::MissileInfo* mi = mit == scene->missiles.end() ? nullptr : &mit->second;
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
        // 40 (FUN_005d5010, Royal Strike's first): the count's missile standing
        // at the target (FUN_0056ede0: its meteor). 41 (FUN_005d5080, its
        // third): prgcalc of them from the target toward random points
        // round it. 37 / 143 (FUN_005d4e70 -> FUN_005d4150, FUN_005d4f40 ->
        // FUN_005d4870: Claws of Thunder's third, Royal Strike's second):
        // a scan of 64 directions (0x100 / 4) for the units within prgcalc
        // (else aurarange) subtiles, a missile at each carrying
        // FUN_004efc20 + 1 hops.
        // ponytail: the scan's pick (FUN_005d40f0 / FUN_005d4680) and the
        // hop count read as: every monster in reach, the level's hops; 41's
        // spread as ±20 subtiles.
        if (!mi) return;
        if (func == 40) { launch(*mi, s, lvl, tx, ty, 0, 0, mi->range, ms); return; }
        if (func == 41) {
            for (int k = 0; k < std::max(r, 1); ++k) {
                const float dx = float(int(rng(41)) - 20) / 5, dy = float(int(rng(41)) - 20) / 5;
                launch(*mi, s, lvl, tx, ty, dx, dy, mi->range + mi->lev_range * lvl, ms);
            }
            return;
        }
        if (func == 37 || func == 143) {
            const int reach = r > 0 ? r : d2d::rules::eval_calc(scene->skills, s.aurarange, env, s.id, lvl);
            for (std::size_t i = 0; i < monsters.size(); ++i)
                if (monsters[i].alive() && std::hypot(monsters[i].u.x - tx, monsters[i].u.y - ty) * 5 <= float(std::max(reach, 1)))
                    launch(*mi, s, lvl, tx, ty, monsters[i].u.x - tx + 0.01f, monsters[i].u.y - ty, mi->range + mi->lev_range * lvl, ms).hops = lvl + 1;
            return;
        }
        if (std::ranges::find(told, -1000 - func) != told.end()) return;
        told.push_back(-1000 - func);
        d2d::log::info("not implemented: {}'s release srvprgfunc {} ({} charges)", s.name, func, count);
    }

auto Fight::dragon_tail(const d2d::rules::Skill& s, std::size_t target, int phys, std::uint32_t ms) -> void {
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

auto Fight::boss_events(std::span<Foe> foes, std::uint32_t ms) -> void {
        using d2d::rules::Boss;
        for (auto& m : monsters) {
            const bool boss = m.boss == Boss::unique || m.boss == Boss::superunique;
            const auto has = [&](int id) { return boss && std::ranges::contains(m.mods, id); };
            if (m.alive()) {
                if (has(d2d::rules::umod::lightning) && m.hp < m.last_hp && ms - m.bolts_at >= 10 * 40) {
                    m.bolts_at = ms;
                    d2d::log::info("{} lets off charged bolts", m.npc.name);
                    const auto ring = std::make_shared<std::vector<int>>();
                    for (const auto& [bx, by] : { std::pair{ 0.f, -1.f }, { 1.f, 0.f }, { 0.f, 1.f }, { -1.f, 0.f } })
                        for (int k = 0; k < 2; ++k) {
                            const float a = float(rng(51) - 25) * 3.14159265f / 180, c = std::cos(a), sn = std::sin(a);
                            boss_missile(m, "lightunique", bx * c - by * sn, bx * sn + by * c, ring, ms);
                        }
                }
            } else if (boss && !m.fx_done) {
                if (!m.fx_at) m.fx_at = ms + 4 * 40;
                else if (ms >= m.fx_at) {
                    m.fx_done = true;
                    if (has(d2d::rules::umod::fire)) fire_blast(m, foes, ms);
                    if (has(d2d::rules::umod::cold)) {
                        const auto ring = std::make_shared<std::vector<int>>();
                        for (int k = 0; k < 64; ++k) {
                            const float a = float(k) * 6.2831853f / 64;
                            boss_missile(m, "coldunique", std::cos(a), std::sin(a), ring, ms);
                        }
                    }
                }
            }
            m.last_hp = m.hp;
        }
    }

auto Fight::boss_missile(const Monster& m, const char* name, float dx, float dy, const std::shared_ptr<std::vector<int>>& ring, std::uint32_t ms) -> void {
        const auto it = scene->missiles.find(name);
        if (it == scene->missiles.end()) return;
        const auto& mi = it->second;
        const int lvl = std::max(m.st.level / 2, 1);
        const auto md = d2d::rules::row_damage(mi.etype, mi.emin, mi.emax, mi.emin_lev, mi.emax_lev, mi.hitshift, mi.elen, mi.elen_lev, lvl);
        d2d::rules::MonStats st;
        st.level = m.st.level;
        st.th = 1 << 20;                                     // ToHit 0: always hits
        if (md.etype >= 0) st.el[0] = { md.etype, 100, md.elo >> 8, std::max(md.ehi >> 8, 1), md.elen, "A2" };
        const float speed = cells_per_sec(float(mi.vel)), d = std::max(std::hypot(dx, dy), 0.01f);
        Missile x{ &mi, m.u.x, m.u.y, dx / d * speed, dy / d * speed, direction32(dx, dy), ms, ms + std::uint32_t(std::max(mi.range, 1)) * 40, st };
        x.struck = ring;
        pending.push_back(std::move(x));
    }

auto Fight::fire_blast(const Monster& m, std::span<Foe> foes, std::uint32_t ms) -> void {
        const auto [lo, hi] = d2d::rules::fire_blast(m.st.hp, m.difficulty);
        const int pts = (lo + rng(std::max(hi - lo, 1))) * 64 / 256;
        const float r = float(m.difficulty + 4) / 5;
        for (auto& f : foes) {
            if (!f.alive || std::hypot(f.x - m.u.x, f.y - m.u.y) > r) continue;
            d2d::rules::Taken k;
            k.hit = true;
            k.damage = std::max(pts * (100 - f.f.dr_pct) / 100 - f.f.dr_flat, 0) + d2d::rules::resisted(pts, f.f.res[0]);
            f.take(k);
        }
        if (const auto it = scene->missiles.find("monstercorpseexplode"); it != scene->missiles.end()) {
            Missile x{ &it->second, m.u.x, m.u.y, 0, 0, 0, ms, ms + std::uint32_t(std::max(it->second.range, 1)) * 40, {} };
            x.fx = true;
            pending.push_back(std::move(x));
        }
        d2d::log::info("{} explodes ({} fire, {} physical)", m.npc.name, pts, pts);
    }

auto Fight::killed(std::size_t i, std::uint32_t ms) -> void {
        const auto& m = monsters[i];
        const auto sc = std::size_t(kUiToSaveClass[std::max(cc.selected, 0)]);
        auto exp = d2d::rules::kill_exp(m.st.exp, int(cc.stats.get(d2d::d2s::kLevel)), m.st.level);
        exp += exp * int(psum[85]) / 100;                   // item_addexperience (the experience shrine)
        const int up = d2d::rules::gain_exp(cc.stats, exp, scene->exp_next, scene->class_gains[sc]);
        d2d::log::info("killed {} (+{} exp){}", m.npc.name, exp, up ? std::format(", level {}", cc.stats.get(d2d::d2s::kLevel)) : "");
        if (up) cc.panel = panel_stats(*scene, cc.header, cc.items, cc.stats);
        fallen_scatter(*scene, monsters, i, rng, ms);
        loot.drop(m, ms);
    }

auto Fight::merc_fighter() const -> d2d::rules::Fighter {
        return d2d::rules::simple_fighter(merc_st.dmg_min, merc_st.dmg_max, merc_st.ar, merc_st.def);
    }

auto Fight::merc_turn(std::uint32_t ms, float dt, const Crowd& crowd) -> void {
        auto& u = *merc;
        const auto set = [&](std::string_view mode) {
            merc_mode = mode; u.mode_ms = ms; u.walking = mode == "WL";
            u.path.clear();
            merc_until = mode == "NU" || mode == "WL" ? 0 : ms + scene->npc_timing(*merc_npc, mode).length_ms();
        };
        if (merc_mode == "DT") {
            if (ms >= merc_until) { merc.reset(); cc.header.merc_dead = true; d2d::log::info("the merc died"); }
            return;
        }
        if (merc_mode == "GH") { if (ms < merc_until) return; set("NU"); }
        const bool archer = merc_npc && merc_npc->id == "roguehire";
        const float reach = archer ? 6.f : kMeleeReach;
        if (merc_mode == "A1") {
            if (!merc_struck && merc_target >= 0 && ms >= u.mode_ms + scene->npc_timing(*merc_npc, "A1").action_ms()) {
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

auto Fight::player_modes(bool held, std::uint32_t ms, float dt) -> bool {
        if (dead()) {
            if (pmode == kModeDT && ms >= pmode_until) set_pmode(kModeDD, ms);
            return pmode == kModeDD;
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
                if (!held) attack_mon = -1;
            }
        } else if (pmode == kModeSC) {                       // a self cast: its state on the action frame
            if (!pstruck && ms >= player.mode_ms + player_anim(kModeSC).action_ms()) {
                pstruck = true;
                if (const auto* s = scene->skills.get(swing_skill); s && s->srvdofunc == 113) {
                    portal_due = true;                         // Town Portal (FUN_005bf3d0): the World opens it
                } else if (s && missile_skill(*s)) {
                    fire(*s, ms);
                } else if (s && spot_skill(*s)) {
                    spot(*s, ms);
                } else if (s && summon_skill(*s)) {
                    summon(*s, ms);
                } else if (s) {
                    start_state(*s, skill_level ? skill_level(s->id) : 1, ms);
                }
            }
            if (ms >= pmode_until) { pmode = -1; player.mode_ms = ms; if (!held) attack_mon = -1; }
        } else if ((pmode == kModeGH || pmode == kModeBL) && ms >= pmode_until) {
            pmode = -1; player.mode_ms = ms;
        }
        return false;
    }

auto Fight::skill_step(std::uint32_t ms, float dt) -> void {
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

auto Fight::move_event(const d2d::rules::Skill& s, std::uint32_t ms) -> void {
        if (s.srvdofunc == 76) {
            if (ms < whirl_next) return;
            const auto& a1 = player_anim(kModeA1);
            const int f = d2d::rules::attack_ticks(int(a1.frames ? a1.frames : 16), int(a1.speed ? a1.speed : 256), pf.ias, pf.wsm);
            whirl_next = ms + std::uint32_t(d2d::rules::whirlwind_gap(f)) * 40;
            const int hands = int(std::ranges::count_if(cc.items, [&](const d2d::d2s::Item& it) {
                return it.location == 1 && (it.slot == 4 || it.slot == 5) && scene->rules.item_info.contains(it.code)
                    && scene->rules.item_info.at(it.code).kind == 2; }));
            for (int h = 0; h < (hands >= 2 ? 2 : 1); ++h) {
                std::vector<int> nearby;
                for (std::size_t i = 0; i < monsters.size(); ++i)
                    if (monsters[i].alive() && std::hypot(monsters[i].u.x - player.x, monsters[i].u.y - player.y) <= 5.f) nearby.push_back(int(i));
                if (nearby.empty()) return;
                const auto nx = std::ranges::upper_bound(nearby, attack_mon);
                attack_mon = nx != nearby.end() ? *nx : nearby.front();
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

auto Fight::other_target() -> void {
        const auto r = in_reach();
        if (r.empty()) { attack_mon = -1; return; }
        const auto next = std::ranges::upper_bound(r, attack_mon);
        attack_mon = next != r.end() ? *next : r.front();
        player.dir = direction16(monsters[std::size_t(attack_mon)].u.x - player.x, monsters[std::size_t(attack_mon)].u.y - player.y);
    }

auto Fight::impale_wear(const d2d::rules::Skill& s) -> void {
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

auto Fight::skill_missile(const d2d::rules::Skill& s, bool any_owner ) const -> const GameData::MissileInfo* {
        static constexpr int kDo[] = { 8, 17, 22, 10, 12, 26, 28, 24, 19, 73, 80, 117, 118, 123, 43, 48, 68, 75, 44, 125, 95 };
        static constexpr int kSt[] = { 0, 4, 8, 11, 26, 33 };
        const bool plain = (s.srvstfunc == 0 || s.srvstfunc == 4) && s.srvdofunc == 0;
        const bool multi = std::ranges::contains(kSt, s.srvstfunc) && std::ranges::contains(kDo, s.srvdofunc);
        const auto& name = plain ? s.srvmissile : s.srvmissilea;
        if ((!plain && !multi) || name.empty()) return nullptr;
        const auto m = scene->missiles.find(name);
        static constexpr int kHit[] = { 0, 1, 2, 3, 4, 7, 9, 10, 12, 13, 14, 17, 18, 20, 21, 22, 26, 29, 36, 37, 47, 48, 56 };
        if (m == scene->missiles.end() || !std::ranges::contains(kHit, m->second.hit_func)) return nullptr;
        return any_owner || m->second.skill == s.name || m->second.skill.empty() ? &m->second : nullptr;
    }

auto Fight::missile_skill(const d2d::rules::Skill& s) const -> bool { return skill_missile(s) != nullptr; }

auto Fight::cast_missile(int skill, float tx, float ty, std::uint32_t ms) -> bool {
        using namespace d2d::d2s;
        const auto* s = scene->skills.get(skill);
        if (!s || dead() || pmode >= 0 || (!missile_skill(*s) && !spot_skill(*s))) return false;
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

auto Fight::launch(const GameData::MissileInfo& mi, const d2d::rules::Skill& s, int lvl, float x, float y, float dx, float dy,
                    int range, std::uint32_t ms) -> Missile& {
        const float v = cells_per_sec(float(mi.vel)), d = std::max(std::hypot(dx, dy), 0.01f);
        Missile a{ &mi, x, y, dx / d * v, dy / d * v, direction32(dx, dy), ms, ms + std::uint32_t(std::max(range, 1)) * 40, {} };
        a.friendly = true; a.skill = s.id; a.level = lvl;
        pending.push_back(a);
        return pending.back();
    }

auto Fight::calc(const d2d::rules::Skill& s, const d2d::rules::Calc& c, int lvl) -> int {
        return d2d::rules::eval_calc(scene->skills, c, calc_env(), s.id, lvl);
    }

auto Fight::fire(const d2d::rules::Skill& s, std::uint32_t ms) -> void {
        const auto& mi = *skill_missile(s);
        const int lvl = skill_level ? skill_level(s.id) : 1;
        const int calc1 = calc(s, s.calc[0], lvl);
        const auto shared = std::make_shared<std::vector<int>>();
        const float dx = cast_x - player.x, dy = cast_y - player.y;
        const int range = mi.range + mi.lev_range * lvl;
        const int target = attack_mon >= 0 && monsters[std::size_t(attack_mon)].alive() ? attack_mon : -1;
        auto go = [&](float ddx, float ddy, int r) -> Missile& {
            auto& a = launch(mi, s, lvl, player.x, player.y, ddx, ddy, r, ms);
            a.ox = cast_x; a.oy = cast_y;                    // where it was sent (Molten Boulder's roll)
            return a;
        };
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
        } else if (s.srvdofunc == 22 || s.srvdofunc == 68) {
            // A war cry (do 68, FUN_005d83e0): the nova of its missile (range
            // its row's), then its state on the caster (FUN_005d8290).
            for (int k = 0; k < 64; ++k) {
                const float a = float(k) * 2 * 3.14159265f / 64;
                go(std::cos(a), std::sin(a), s.srvdofunc == 68 ? mi.range : mi.range + calc1).struck = shared;
            }
            if (s.srvdofunc == 68 && !s.aurastate.empty()) start_state(s, lvl, ms);
        } else if (s.srvdofunc == 10) {
            auto& a = go(dx, dy, range);
            a.target = target; a.ed_pct = calc1;
        } else if (s.srvdofunc == 12) {
            const int r = calc(s, s.aurarange, lvl);
            std::vector<int> nearby;
            if (target >= 0) nearby.push_back(target);
            for (std::size_t i = 0; i < monsters.size(); ++i)
                if (int(i) != target && monsters[i].alive()
                    && std::hypot(monsters[i].u.x - player.x, monsters[i].u.y - player.y) * 5 <= float(r)) nearby.push_back(int(i));
            const int hi = calc1, lo = std::min(calc(s, s.calc[2], lvl), hi);
            const int n = std::clamp(int(nearby.size()), lo, hi);
            for (int k = 0; k < n; ++k)
                strafe.push_back({ nearby.empty() ? -1 : nearby[std::size_t(k) % nearby.size()], ms + std::uint32_t(k) * 120 });
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
        } else if (s.srvdofunc == 19 || s.srvdofunc == 48) {
            channel = s.id;
        } else if (s.srvdofunc == 73) {                      // Blessed Hammer (FUN_005d0040): path 14, a spiral out
            auto& a = go(dx, dy, range);
            a.ox = player.x; a.oy = player.y;
            a.turn = int(std::atan2(dy, dx) * 1000);
        } else if (s.srvdofunc == 80) {                      // Fist of the Heavens (FUN_005d0670): on the target
            if (target < 0) return;
            const auto& m = monsters[std::size_t(target)];
            launch(mi, s, lvl, m.u.x, m.u.y, 0, 0, range, ms).target = target;
        } else if (s.srvdofunc == 117 || s.srvdofunc == 118) {   // Firestorm (FUN_005c7160), Twister / Tornado (FUN_005c72f0)
            for (int k = 0; k < std::max(calc1, 1); ++k) {
                const float a = k == 0 ? 0.f : (float(rng(41)) - 20) * 3.14159265f / 180;
                go(dx * std::cos(a) - dy * std::sin(a), dx * std::sin(a) + dy * std::cos(a), range);
            }
        } else if (s.srvdofunc == 44) {                      // Blade Sentinel: out to the point and back, Param1 frames
            // ponytail: do 44 (FUN_005d6020) moves a trap monster between
            // the caster and the point; here its missile row does, hitting
            // as it passes (NextHit).
            auto& a = go(dx, dy, std::max(range, s.par[0]));
            a.bx = player.x; a.by = player.y;
            const float d = std::max(std::hypot(dx, dy), 0.01f), v = cells_per_sec(10.f);   // bladecreeper's Velocity
            a.vx = dx / d * v; a.vy = dy / d * v;
        } else if (s.srvdofunc == 75) {                      // Grim Ward: its start row at the point
            launch(mi, s, lvl, cast_x, cast_y, 0, 0, mi.range, ms);
        } else if (s.srvdofunc == 123) {                     // Volcano (FUN_005c8080): at the target point
            launch(mi, s, lvl, cast_x, cast_y, 0, 0, range, ms);
        } else if (s.srvdofunc == 43) {                      // Shock Web (FUN_005d5d70 -> FUN_005d5bf0): scattered round the target
            const int n = std::max(calc(s, s.prgcalc[0], lvl), 1), r = std::max(calc(s, s.aurarange, lvl), 1);
            for (int k = 0; k < n; ++k) {
                const float tx = cast_x + float(int(rng(2 * r + 1)) - r) / 5, ty = cast_y + float(int(rng(2 * r + 1)) - r) / 5;
                go(tx - player.x, ty - player.y, land_range(mi, tx - player.x, ty - player.y));
            }
        } else {
            go(dx, dy, mi.hit_func == 36 ? land_range(mi, dx, dy) : range);
        }
    }

auto Fight::land_range(const GameData::MissileInfo& mi, float dx, float dy) -> int {
        const float per_frame = cells_per_sec(float(std::max(mi.vel, 1))) * 0.04f;
        return std::max(int(std::hypot(dx, dy) / per_frame), 1);
    }

auto Fight::missile_tick(std::uint32_t ms) -> void {
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
                const auto& mi = *skill_missile(*s);
                if (s->srvdofunc == 19)
                    launch(mi, *s, lvl, player.x, player.y, cast_x - player.x, cast_y - player.y, std::max(calc(*s, s->calc[0], lvl), 1), ms);
                else if (frame % 3 == 0)                     // Blade Fury (FUN_005d68a0): one per attack frame
                    launch(mi, *s, lvl, player.x, player.y, cast_x - player.x, cast_y - player.y, mi.range, ms);
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
            // The Range runs out: the hit function goes off with no unit.
            static constexpr int kEnd[] = { 1, 3, 4, 9, 13, 14, 20, 22, 26, 29, 36, 48, 56 };
            if (ms + 40 >= a.dies && a.frame < 0 && std::ranges::contains(kEnd, mi.hit_func)) burst(a, ms);
            // 15 (FUN_005af030, Frozen Orb): every Param1 frames SubMissile1
            // toward direction `turn` of 64 (0x6e2b78 / 0x6e2a78), turning
            // Param2 on.
            if (mi.srv_do == 15 && sub != scene->missiles.end() && age % std::max(mi.param1, 1) == 0) {
                const float t = float(a.turn & 63) * 2 * 3.14159265f / 64;
                launch(sub->second, *s, a.level, a.x, a.y, std::cos(t), std::sin(t), sub->second.range, ms);
                a.turn += mi.param2;
            }
            // 14 (Grim Ward's totem): every Param1 frames the monsters within
            // aurarange of it run for auralen frames.
            // ponytail: missile do 14 isn't traced; Param1 / aurarange read so.
            if (mi.srv_do == 14 && age % std::max(mi.param1, 1) == 0)
                for (auto& m : monsters)
                    if (m.alive() && std::hypot(m.u.x - a.x, m.u.y - a.y) * 5 <= float(std::max(calc(*s, s->aurarange, a.level), 1)))
                        m.flee_until = std::max(m.flee_until, ms + std::uint32_t(std::max(calc(*s, s->auralen, a.level), 1)) * 40);
            // 20 (Blade Sentinel): back and forth between the caster's spot and
            // where it was sent.
            if (mi.srv_do == 20 && std::hypot(a.ox - a.x, a.oy - a.y) < 0.3f) {
                std::swap(a.ox, a.bx); std::swap(a.oy, a.by);
                const float v = std::hypot(a.vx, a.vy), dx = a.ox - a.x, dy = a.oy - a.y, d = std::max(std::hypot(dx, dy), 0.01f);
                a.vx = dx / d * v; a.vy = dy / d * v; a.dir = direction32(dx, dy);
                a.hit_at.clear();
            }
            // 31 (Wake of Fire's maker): its fire where it goes.
            if (mi.srv_do == 31 && sub != scene->missiles.end())
                launch(sub->second, *s, a.level, a.x, a.y, a.vx, a.vy, sub->second.range, ms);
            // 23 (FUN_005af790, Firestorm): SubMissile1 where it is, each frame.
            if (mi.srv_do == 23 && sub != scene->missiles.end())
                launch(sub->second, *s, a.level, a.x, a.y, 0, 0, sub->second.range, ms);
            // 27 (FUN_005afa30, Tornado): every Param1 (else calc4) frames its
            // damage within Param2 (else aurarange) subtiles.
            if (mi.srv_do == 27 && age % std::max(mi.param1 > 0 ? mi.param1 : calc(*s, s->calc[3], a.level), 1) == 0)
                area(a, a.x, a.y, mi.param2 > 0 ? mi.param2 : calc(*s, s->aurarange, a.level), ms);
            // 28 (FUN_005afb80, Volcano): every Param1 (else calc4) frames
            // SubMissile1 thrown at a point within Param2 (else aurarange).
            // ponytail: its Param3 / Param4 frame window isn't applied.
            if (mi.srv_do == 28 && sub != scene->missiles.end()
                && age % std::max(mi.param1 > 0 ? mi.param1 : calc(*s, s->calc[3], a.level), 1) == 0) {
                const int r = std::max(mi.param2 > 0 ? mi.param2 : calc(*s, s->aurarange, a.level), 1);
                const float tx = float(int(rng(2 * r + 1)) - r) / 5, ty = float(int(rng(2 * r + 1)) - r) / 5;
                launch(sub->second, *s, a.level, a.x, a.y, tx, ty, land_range(sub->second, tx, ty), ms);
            }
            // Blessed Hammer (do 73's path 14): round its caster's spot and out.
            // ponytail: the client path's shape isn't traced: a turn each 1.6
            // s, out at a fifth of its speed.
            if (s->srvdofunc == 73 && mi.srv_do <= 1) {
                const float t = float(ms - a.born) / 1000, r = t * cells_per_sec(float(mi.vel)) * 0.2f;
                const float ang = float(a.turn) / 1000 + t * 2 * 3.14159265f / 1.6f;
                a.x = a.ox + r * std::cos(ang); a.y = a.oy + r * std::sin(ang);
                a.vx = a.vy = 0;
            }
        }
    }

auto Fight::burn(std::size_t i, const d2d::rules::MissileDamage& md, std::uint32_t ms) -> void {
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

auto Fight::pierce() const -> std::array<int, 4> { return { int(psum[333]), int(psum[334]), int(psum[335]), int(psum[336]) }; }

auto Fight::skill_missile_hits(Missile& a, std::size_t i, std::uint32_t ms) -> bool {
        const auto* s = scene->skills.get(a.skill);
        if (!s) return true;
        const auto& mi = *a.info;
        if (mi.srv_do == 5) return false;                    // a burner: missile_tick's
        // Hit function 10 (FUN_005aa650): a guided one passes by all but its target.
        if (mi.hit_func == 10 && a.target >= 0 && a.target != int(i) && monsters[std::size_t(a.target)].alive()) return false;
        // 36 (FUN_005abf70: Fire Blast, Shock Web in the air), 14 / 22 / 29 /
        // 48 (Meteor, Fist of the Heavens, Frozen Orb, Molten Boulder rising)
        // pass over units and act where they come down.
        if (mi.hit_func == 36 || mi.hit_func == 14 || mi.hit_func == 22 || mi.hit_func == 29 || mi.hit_func == 48) return false;
        // 7 (FUN_005a9fb0: Holy Bolt, the Fist's bolts): sHitPar2 1 strikes
        // the undead only (FUN_0063e990), 2 demons (FUN_0063e940); the rest
        // it passes (4). ponytail: it doesn't heal the player's side (calc1).
        if (mi.hit_func == 7) {
            const auto& t = scene->monsters.types[std::size_t(monsters[i].type)];
            if ((mi.hit_par2 == 1 && !t.undead) || (mi.hit_par2 == 2 && !t.demon)) return false;
        }
        // 18 (FUN_005ab0b0: Shout, Battle Orders, Battle Command) is for
        // allies; 21 (FUN_005ab500, Battle Cry) puts its auratargetstate on
        // the monster for auralen frames; 17 (FUN_005aafb0, Howl) sends one
        // below the caster's level + calc1 + the skill level running for
        // Param3 + Param4 x (level - 1) frames (FUN_004cc7c0); neither hurts.
        // ponytail: Howl's level sum (FUN_004efc20) read as calc1 + level.
        if (mi.hit_func == 18) return false;
        if (mi.hit_func == 21) {
            if (monsters[i].cry.skill != s->id || ms >= monsters[i].cry.until)
                monsters[i].cry = { s->id, a.level, ms + std::uint32_t(std::max(calc(*s, s->auralen, a.level), 1)) * 40 };
            return false;
        }
        if (mi.hit_func == 17) {
            const int clvl = int(cc.stats.get(d2d::d2s::kLevel));
            if (monsters[i].st.level < clvl + calc(*s, s->calc[0], a.level) + a.level)
                monsters[i].flee_until = std::max(monsters[i].flee_until, ms + std::uint32_t(std::max(s->par[2] + (a.level - 1) * s->par[3], 25)) * 40);
            a.struck->push_back(int(i));
            return false;
        }
        if (mi.hit_func == 26) return false;                 // Grim Ward's start: it lands (burst)
        // NextHit rows strike a monster again NextDelay frames on.
        if (mi.next_hit) {
            auto it = std::ranges::find(a.hit_at, int(i), &std::pair<int, std::uint32_t>::first);
            if (it != a.hit_at.end() && ms < it->second + std::uint32_t(std::max(mi.next_delay, 1)) * 40) return false;
            if (it == a.hit_at.end()) a.hit_at.emplace_back(int(i), ms); else it->second = ms;
        }
        // 1 (FUN_005a9a70) and 13 (FUN_005aa8b0) go off here, the area taking in the unit.
        if (mi.hit_func == 1 || mi.hit_func == 13) { burst(a, ms); return true; }
        strike(a, i, ms);
        if (!mi.next_hit) a.struck->push_back(int(i));
        // Hit function 12 (FUN_005aa730, Chain Lightning): with hops left, on
        // from here at another monster within sHitPar1 (else aurarange)
        // subtiles (FUN_0056bd10), a hop fewer.
        // ponytail: the nearest one, not FUN_0056bd10's pick.
        if (mi.hit_func == 12 && a.hops > 1) {
            const int r = mi.hit_par1 > 0 ? mi.hit_par1 : calc(*s, s->aurarange, a.level);
            int best = -1; float bd = 1e9f;
            for (std::size_t j = 0; j < monsters.size(); ++j)
                if (const float d = std::hypot(monsters[j].u.x - a.x, monsters[j].u.y - a.y);
                    j != i && monsters[j].alive() && d * 5 <= float(r) && d < bd) { bd = d; best = int(j); }
            if (best >= 0) {
                const auto& m = monsters[std::size_t(best)];
                launch(mi, *s, a.level, a.x, a.y, m.u.x - a.x, m.u.y - a.y, mi.range, ms).hops = a.hops - 1;
            }
        }
        if (mi.hit_func == 2 || mi.hit_func == 4 || mi.hit_func == 9 || mi.hit_func == 20 || mi.hit_func == 47) {
            a.target = int(i);
            burst(a, ms);
            if (mi.hit_func == 47) return false;             // the boulder rolls on (FUN_005ac550 returns 1)
            return true;
        }
        return mi.collide_kill && !(mi.pierce && int(rng(100)) < int(psum[328]));
    }

auto Fight::strike(const Missile& a, std::size_t i, std::uint32_t ms, int freeze ) -> void {
        const auto* s = scene->skills.get(a.skill);
        if (!s || !monsters[i].alive()) return;
        const auto target = target_of(i);
        const int clvl = int(cc.stats.get(d2d::d2s::kLevel));
        auto md = a.fixed >= 0 ? d2d::rules::MissileDamage{ .etype = a.info->etype < 0 ? 0 : a.info->etype, .elo = a.fixed, .ehi = a.fixed }
                : a.info->skill.empty() ? row_damage(*a.info, a.level)
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
        if (b.hit) b = d2d::rules::missile_blow(md, target, pierce(), rng, b);
        if (b.hit && freeze > 0) { b.chill_ticks = std::max(b.chill_ticks, freeze); b.stun_ticks = std::max(b.stun_ticks, freeze); }
        land(i, b, true, ms);
    }

auto Fight::area(const Missile& a, float x, float y, int r, std::uint32_t ms, int freeze ) -> void {
        for (std::size_t j = 0; j < monsters.size(); ++j)
            if (monsters[j].alive() && std::hypot(monsters[j].u.x - x, monsters[j].u.y - y) * 5 <= float(std::max(r, 1))) strike(a, j, ms, freeze);
    }

auto Fight::burst(Missile& a, std::uint32_t ms) -> void {
        const auto* s = scene->skills.get(a.skill);
        if (!s || a.frame >= 0) return;
        a.frame = int(ms / 40);
        const auto& mi = *a.info;
        const auto hs = mi.hit_sub.empty() ? scene->missiles.end() : scene->missiles.find(mi.hit_sub);
        const bool sub = hs != scene->missiles.end();
        auto sub_at = [&](float x, float y, float dx, float dy) -> Missile& {
            return launch(hs->second, *s, a.level, x, y, dx, dy, hs->second.range + hs->second.lev_range * a.level, ms);
        };
        auto radius = [&](int par, const d2d::rules::Calc& c) { return par > 0 ? par : std::max(calc(*s, c, a.level), 1); };
        static constexpr int kX[18] = { 2, -2, 0, 0, -3, 0, 3, -1, 1, -1, 2, -4, -3, -1, 0, 1, 3, 4 };
        static constexpr int kY[18] = { -2, -2, 2, 5, 3, 3, 3, 2, 1, -1, -1, -2, -2, -3, -4, -3, -3, -2 };
        switch (mi.hit_func) {
            case 1: area(a, a.x, a.y, radius(mi.hit_par1, s->calc[0]), ms); break;
            case 3: area(a, a.x, a.y, radius(mi.hit_par1, s->aurarange), ms); break;
            case 13: area(a, a.x, a.y, radius(mi.hit_par1, s->aurarange), ms, radius(mi.hit_par2, s->auralen)); break;
            case 2: case 4: case 26: case 36: case 51: if (sub) sub_at(a.x, a.y, 0, 0); break;
            case 9: {
                const int r = radius(mi.hit_par1, s->calc[0]);
                area(a, a.x, a.y, r, ms);
                if (sub)
                    for (int ox = -r; ox <= r; ++ox)
                        for (int oy = -r; oy <= r; ++oy)
                            if (ox * ox + oy * oy <= r * r) sub_at(a.x + float(ox) / 5, a.y + float(oy) / 5, 0, 0).fixed = hs->second.emin;
                break;
            }
            case 14: case 47: case 56: {
                area(a, a.x, a.y, radius(mi.hit_par1, s->aurarange), ms);
                const int burn256 = s->par[2] + (a.level - 1) * s->par[3];   // Meteor's (FUN_004cc7c0)
                if (sub)
                    for (std::size_t j = 0; j < 18; j += std::size_t(std::max(mi.hit_par2, 1)))
                        sub_at(a.x + float(kX[j]) / 5, a.y + float(kY[j]) / 5, 0, 0).fixed
                            = mi.hit_func == 14 && burn256 > 0 ? burn256 : hs->second.emin > 0 ? hs->second.emin : -1;
                break;
            }
            case 20: {
                if (!sub) break;
                const int r = radius(mi.hit_par1, s->aurarange);
                int n = radius(mi.hit_par2, s->calc[0]);
                for (std::size_t j = 0; j < monsters.size() && n > 0; ++j)
                    if (int(j) != a.target && monsters[j].alive() && std::hypot(monsters[j].u.x - a.x, monsters[j].u.y - a.y) * 5 <= float(r)) {
                        sub_at(a.x, a.y, monsters[j].u.x - a.x, monsters[j].u.y - a.y);
                        --n;
                    }
                break;
            }
            case 22: {
                const float x = a.target >= 0 ? monsters[std::size_t(a.target)].u.x : a.x;
                const float y = a.target >= 0 ? monsters[std::size_t(a.target)].u.y : a.y;
                const int r = radius(mi.hit_par1, s->aurarange);
                area(a, x, y, r, ms);
                if (sub)
                    for (std::size_t j = 0; j < monsters.size(); ++j)
                        if (monsters[j].alive() && scene->monsters.types[std::size_t(monsters[j].type)].undead
                            && std::hypot(monsters[j].u.x - x, monsters[j].u.y - y) * 5 <= float(r))
                            sub_at(x, y, monsters[j].u.x - x, monsters[j].u.y - y);
                break;
            }
            case 29:
                if (sub)
                    for (int k = 0; k < 64; k += std::max(mi.hit_par1, 1)) {
                        const float t = float(k) * 2 * 3.14159265f / 64;
                        sub_at(a.x, a.y, std::cos(t), std::sin(t));
                    }
                break;
            case 48: if (sub) sub_at(a.x, a.y, a.ox - a.x, a.oy - a.y); break;
            default: break;
        }
    }

auto Fight::aura_pulse(std::uint32_t ms) -> void {
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
        // Redemption (do 82): a corpse within aurarange redeemed at calc1 %,
        // calc2 life and mana back.
        // ponytail: its callback isn't traced; one corpse a pulse.
        if (s->srvdofunc == 82) {
            using namespace d2d::d2s;
            const int r = calc(*s, s->aurarange, lvl);
            for (auto& m : monsters)
                if (!m.alive() && !m.corpse_used && m.mode == "DD" && std::hypot(m.u.x - player.x, m.u.y - player.y) * 5 <= float(r)) {
                    if (int(rng(100)) < calc(*s, s->calc[0], lvl)) {
                        m.corpse_used = true;
                        const std::int64_t g = std::int64_t(calc(*s, s->calc[1], lvl)) << 8;
                        cc.stats.v[kLife] = std::min(cc.stats.v[kMaxLife], cc.stats.v[kLife] + g);
                        cc.stats.v[kMana] = std::min(cc.stats.v[kMaxMana], cc.stats.v[kMana] + g);
                    }
                    break;
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

auto Fight::in_aura(const Monster& m) -> bool {
        const auto* s = scene->skills.get(aura);
        if (!s || !s->aura) return false;
        const int r = d2d::rules::eval_calc(scene->skills, s->aurarange, calc_env(), s->id, skill_level ? skill_level(aura) : 1);
        return std::hypot(m.u.x - player.x, m.u.y - player.y) * 5 <= float(r);
    }

auto Fight::target_of(std::size_t i) -> d2d::rules::Target {
        auto t = monsters[i].target(*scene);
        // Its curse and cry (Amplify Damage, Lower Resist, Decrepify, Battle
        // Cry, Inner Sight, Cloak of Shadows): their aurastats as the aura's.
        for (const auto* st : { &monsters[i].curse, &monsters[i].cry })
            if (const auto* k = st->skill >= 0 ? scene->skills.get(st->skill) : nullptr)
                apply_target_stats(*k, st->level, t);
        const auto* s = scene->skills.get(aura);
        if (!s || (s->srvdofunc != 66 && s->srvdofunc != 81) || !in_aura(monsters[i])) return t;
        apply_target_stats(*s, skill_level ? skill_level(aura) : 1, t);
        return t;
    }

auto Fight::apply_target_stats(const d2d::rules::Skill& s_, int lvl, d2d::rules::Target& t) -> void {
        const auto* s = &s_;
        const auto env = calc_env();
        for (std::size_t k = 0; k < s->aurastat.size(); ++k) {
            const int v = d2d::rules::eval_calc(scene->skills, s->aura_calc[k], env, s->id, lvl);
            switch (s->aurastat[k]) {
                case 36: t.res[0] += v; break;  case 37: t.res[1] += v; break;
                case 39: t.res[2] += v; break;  case 41: t.res[3] += v; break;
                case 43: t.res[4] += v; break;  case 45: t.res[5] += v; break;
                case 171: t.ac += t.ac * v / 100; break;
                case 31: t.ac = std::max(t.ac + v, 0); break;   // Inner Sight's armorclass
                default: break;
            }
        }
    }

auto Fight::summon_skill(const d2d::rules::Skill& s) const -> bool {
        if (s.srvdofunc == 58) return true;                  // Revive: the corpse's own monster
        return !s.summon.empty() && scene->monsters.row(s.summon) >= 0
            && (s.srvdofunc == 56 || s.srvdofunc == 57 || s.srvdofunc == 31 || s.srvdofunc == 16 || s.srvdofunc == 114
                || s.srvdofunc == 144 || s.srvdofunc == 15 || s.srvdofunc == 49 || s.srvdofunc == 60 || s.srvdofunc == 62
                || s.srvdofunc == 115
                || (s.srvdofunc == 119 && (s.pettype == "spiritwolf" || s.pettype == "fenris" || s.pettype == "grizzly" || s.pettype == "totem"))
                || (s.srvdofunc == 45 && trap_shot(s) >= 0));
    }

auto Fight::trap_shot(const d2d::rules::Skill& s) const -> int {
        for (const auto& n : s.sumskill)
            if (const auto it = scene->skills.by_name.find(n); !n.empty() && it != scene->skills.by_name.end())
                if (const auto* k = scene->skills.get(it->second); k && skill_missile(*k, true)) return k->id;
        return -1;
    }

auto Fight::cast_summon(int skill, float tx, float ty, std::uint32_t ms) -> bool {
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

auto Fight::corpse_near(float x, float y) const -> int {
        int best = -1; float bd = 2.f;
        for (std::size_t i = 0; i < monsters.size(); ++i)
            if (const auto& m = monsters[i]; !m.alive() && !m.corpse_used && m.mode == "DD")
                if (const float d = std::hypot(m.u.x - x, m.u.y - y); d < bd) { bd = d; best = int(i); }
        return best;
    }

auto Fight::skill_named(std::string_view n) const -> const d2d::rules::Skill* {
        if (n.empty()) return nullptr;
        if (const auto it = scene->skills.by_name.find(std::string(n)); it != scene->skills.by_name.end()) return scene->skills.get(it->second);
        for (const auto& [k, id] : scene->skills.by_name)
            if (std::ranges::equal(k, n, [](char a, char b) { return std::tolower(std::uint8_t(a)) == std::tolower(std::uint8_t(b)); }))
                return scene->skills.get(id);
        return nullptr;
    }

auto Fight::pet_missile(const d2d::rules::Skill& k, int variant ) const -> const GameData::MissileInfo* {
        std::string n = k.srvmissile.empty() ? k.srvmissilea : k.srvmissile;
        if (k.srvdofunc == 149 && !n.empty() && n.back() == '1') n.back() = char('1' + std::clamp(variant, 0, 3));
        const auto it = scene->missiles.find(n);
        return n.empty() || it == scene->missiles.end() ? nullptr : &it->second;
    }

auto Fight::summon(const d2d::rules::Skill& s, std::uint32_t ms) -> void {
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
        // Bone Wall (do 60, FUN_005c58b0): one on the point, then its makers
        // (srvmissilea, missile do 13) lay calc2 / 2 more each way across
        // the caster's line. Bone Prison (62, FUN_005c5d00): twelve round the
        // point at 0x6e304c / 0x6e307c.
        // ponytail: the makers are laid at once, a subtile apart; calc2's
        // text is 'par34' (Param3 taken).
        if (s.srvdofunc == 60) {
            const float wx = y - player.y, wy = player.x - x, d = std::max(std::hypot(wx, wy), 0.01f);
            const int n = std::max(s.par[2], 2) / 2;
            for (int k = -n; k <= n; ++k) summon_one(s, type, lvl, env, x + wx / d * float(k) * 0.4f, y + wy / d * float(k) * 0.4f, ms);
            return;
        }
        if (s.srvdofunc == 62) {
            static constexpr int kX[12] = { -1, 1, 3, 4, 4, 3, -1, 1, -3, -4, -4, -3 };
            static constexpr int kY[12] = { -4, -4, -3, -1, 1, 3, 4, 4, 3, -1, 1, -3 };
            for (std::size_t k = 0; k < 12; ++k) summon_one(s, type, lvl, env, x + float(kX[k]) / 5, y + float(kY[k]) / 5, ms);
            return;
        }
        // Hydra (do 144, FUN_005ca910): three at once.
        for (int k = s.srvdofunc == 144 ? 3 : 1; k > 0; --k) summon_one(s, type, lvl, env, x + float(k - 1) * 0.6f, y, ms);
    }

auto Fight::summon_one(const d2d::rules::Skill& s, int type, int lvl, const d2d::rules::CalcEnv& env, float x, float y, std::uint32_t ms) -> void {
        const int max = std::max(d2d::rules::eval_calc(scene->skills, s.petmax, env, s.id, lvl), 1);
        auto same = [&](const Pet& p) { return scene->skills.get(p.skill) && scene->skills.get(p.skill)->pettype == s.pettype; };
        while (s.pettype != "none" && std::ranges::count_if(pets, same) >= max) pets.erase(std::ranges::find_if(pets, same));
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
            else if (pet_missile(*k_s) && s.srvdofunc != 115 && s.srvdofunc != 45) { p.ranged = k_s->id; p.ranged_level = kl; }
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
            case 60: case 62: p.idle = true; p.until = ms + 24000; break;   // bone: it stands (24 s: ponytail, the published time)
            case 115: m.st.level = std::max(calc(s, s.calc[1], lvl), 1); break;   // Vines (FUN_005c6a80): calc2 is its level
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

auto Fight::trap_turn(Pet& p, std::uint32_t ms) -> void {
        auto& m = p.m;
        if (ms < m.next_act) return;
        const auto& d = scene->monsters.types[std::size_t(m.type)].diff[std::size_t(m.difficulty)];
        m.next_act = ms + std::uint32_t(std::max(d.aidel, 1)) * 40;
        const float range = float(d.aip[3] > 0 ? d.aip[3] : 25) / 5;
        int best = -1; float bd = range;
        for (std::size_t i = 0; i < monsters.size(); ++i)
            if (const float dd = std::hypot(monsters[i].u.x - m.u.x, monsters[i].u.y - m.u.y); monsters[i].alive() && dd <= bd) { bd = dd; best = int(i); }
        // Death Sentry's other skill, 'mon death sentry' (do 55): a corpse in
        // its reach goes up.
        if (const auto* ds = scene->skills.get(p.skill))
            for (std::size_t n = 0; n < 5; ++n)
                if (const auto* b = skill_named(ds->sumskill[n]); b && b->srvdofunc == 55) {
                    const int c = corpse_near(m.u.x, m.u.y);
                    if (c >= 0) { corpse_blast(*b, p.shot_level, std::size_t(c), ms); break; }
                }
        if (best < 0) return;
        const auto* k = scene->skills.get(p.shot_skill);
        if (!k) return;
        const auto& mi = *skill_missile(*k, true);
        const auto owner = scene->skills.by_name.find(mi.skill);
        const float dx = monsters[std::size_t(best)].u.x - m.u.x, dy = monsters[std::size_t(best)].u.y - m.u.y, dist = std::max(std::hypot(dx, dy), 0.01f);
        const float v = cells_per_sec(float(mi.vel));
        // Inferno Sentry's (do 95, FUN_005cc4e0) is a stream of flames:
        // here eight at once, their reach staggered.
        const int n = k->srvdofunc == 17 ? std::max(d2d::rules::eval_calc(scene->skills, k->calc[0], calc_env(), k->id, p.shot_level), 1)
                    : k->srvdofunc == 95 ? 8 : 1;
        for (int j = 0; j < n; ++j) {
            const float a = n > 1 ? (float(rng(81)) - 40) * 3.14159265f / 180 : 0.f;
            const float ex = dx * std::cos(a) - dy * std::sin(a), ey = dx * std::sin(a) + dy * std::cos(a);
            Missile x{ &mi, m.u.x, m.u.y, ex / dist * v, ey / dist * v, direction32(ex, ey), ms,
                       ms + std::uint32_t(std::max(mi.range, 1)) * 40, {} };
            x.friendly = true; x.level = p.shot_level;
            x.skill = owner != scene->skills.by_name.end() ? owner->second : k->id;
            if (k->srvdofunc == 95) { x.vx = dx / dist * v; x.vy = dy / dist * v; x.dies = ms + std::uint32_t(4 + 3 * j) * 40; }
            missiles.push_back(x);
        }
        if (--p.shots <= 0) { m.hp = 0; set_mode(*scene, m, "DT", ms); }
    }

auto Fight::pet_foe(const Pet& p) const -> Foe {
        auto f = d2d::rules::simple_fighter(p.m.st.a1_min, p.m.st.a1_max, p.m.st.th, p.m.st.ac);
        for (std::size_t k = 0; k < 4; ++k) f.res[k] = std::min(p.res[k], 95);
        f.thorns_pct = p.thorns;                             // Iron Golem's thorns
        const auto& t = scene->monsters.types[std::size_t(p.m.type)];
        const bool still = t.velocity == 0 && t.run == 0 && p.ranged >= 0;   // a Hydra, like a trap, isn't there to hit
        return Foe{ p.m.u.x, p.m.u.y, p.m.st.level, p.m.alive() && p.m.mode != "DT" && p.shot_skill < 0 && !still && p.where == level,
                    p.m.u.walking, f };
    }

auto Fight::pet_hurt(Pet& p, int damage, std::uint32_t ms) -> void {
        if (damage > 0 && p.m.mode != "DT") hurt(*scene, p.m, damage, ms);
    }

auto Fight::pets_cross(const Level* from, const Level* to, float dx, float dy) -> void {
        for (auto& p : pets)
            if (p.where == from && p.shot_skill < 0 && p.m.alive()) {
                p.where = to; p.target = -1;
                p.m.u.x -= dx; p.m.u.y -= dy; p.m.u.goal_x -= dx; p.m.u.goal_y -= dy;
                for (auto& [px, py] : p.m.u.path) { px -= dx; py -= dy; }
                p.m.home_x -= dx; p.m.home_y -= dy;
            }
    }

auto Fight::pet_aura(Pet& p, std::uint32_t ms) -> void {
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

auto Fight::pets_turn(std::uint32_t ms, float dt, const Crowd& crowd) -> void {
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
            if (const auto* k = scene->skills.get(p.skill); k && k->srvdofunc == 115 && ms >= p.aura_next) {
                // The vines: Poison Creeper poisons what's within 1.5 cells
                // with the skill's poison; Carrion Vine / Solar Creeper eat a
                // corpse within 5 cells for Param6 seconds, giving Param5 %
                // of max life / mana a second.
                // ponytail: their AI (Vines, CycleOfLife) and Vine Attack /
                // the cyclers (do 130, st 63) aren't traced: the published
                // behaviour.
                p.aura_next = ms + 1000;
                using namespace d2d::d2s;
                if (k->etype == 3) {
                    const auto md = d2d::rules::missile_damage(scene->skills, *k, calc_env(), p.m.st.level);
                    for (std::size_t j = 0; j < monsters.size(); ++j)
                        if (monsters[j].alive() && std::hypot(monsters[j].u.x - u.x, monsters[j].u.y - u.y) < 1.5f)
                            land(j, d2d::rules::missile_blow(md, target_of(j), pierce(), rng), false, ms);
                } else if (p.hits > 0 || [&] {
                               const int c = corpse_near(u.x, u.y);
                               if (c < 0 || std::hypot(monsters[std::size_t(c)].u.x - u.x, monsters[std::size_t(c)].u.y - u.y) > 5) return false;
                               monsters[std::size_t(c)].corpse_used = true;
                               p.hits = std::max(k->par[5], 1);
                               return true;
                           }()) {
                    --p.hits;
                    const bool life = k->name == "Cycle of Life";
                    auto& v = cc.stats.v[life ? kLife : kMana];
                    v = std::min(cc.stats.v[life ? kMaxLife : kMaxMana], v + cc.stats.v[life ? kMaxLife : kMaxMana] * k->par[4] / 100);
                }
            }
            if (m.mode == "A1") {
                if (!m.struck && p.target >= 0 && ms >= u.mode_ms + scene->npc_timing(m.npc, "A1").action_ms()) {
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
                    set_mode(*scene, m, scene->npc_timing(m.npc, "A1").directions ? "A1" : "NU", ms);
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

auto Fight::cast_scroll(int skill, std::uint32_t ms) -> bool {
        if (dead() || pmode >= 0) return false;
        swing_skill = skill;
        attack_mon = -1;
        set_pmode(kModeSC, ms);
        pstruck = false;
        return true;
    }

auto Fight::cast(int skill, std::uint32_t ms) -> bool {
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

auto Fight::frenzy(const d2d::rules::Skill& s, std::uint32_t ms) -> void {
        const int lvl = skill_level ? skill_level(s.id) : 1;
        const auto env = calc_env();
        const auto it = std::ranges::find(self_states, s.id, &SelfState::skill);
        const int n = std::min(it == self_states.end() ? 1 : it->level + 1, lvl);
        const auto until = ms + std::uint32_t(std::max(d2d::rules::eval_calc(scene->skills, s.auralen, env, s.id, lvl), 1)) * 40;
        if (it == self_states.end()) self_states.push_back({ s.id, n, until });
        else *it = { s.id, n, until };
    }

auto Fight::next_target() -> bool {
        const auto* s = scene->skills.get(swing_skill);
        if (s && s->srvdofunc == 13) { other_target(); return attack_mon >= 0; }
        return attack_mon >= 0 && monsters[std::size_t(attack_mon)].alive();
    }

auto Fight::in_reach() const -> std::vector<int> {
        std::vector<int> out;
        for (std::size_t i = 0; i < monsters.size(); ++i)
            if (monsters[i].alive() && std::hypot(monsters[i].u.x - player.x, monsters[i].u.y - player.y) <= kMeleeReach + 0.5f)
                out.push_back(int(i));
        return out;
    }

auto Fight::engage(std::uint32_t ms) -> std::optional<std::pair<float, float>> {
        if (attack_mon < 0 || pmode >= 0) return std::nullopt;
        const auto& m = monsters[std::size_t(attack_mon)];
        if (!m.alive()) { attack_mon = -1; return std::nullopt; }
        const auto* s = scene->skills.get(attack_skill);
        if (s && (missile_skill(*s) || spot_skill(*s))) {   // from here, at it
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

auto Fight::crowd(Crowd& c) -> void {
        for (auto& m : monsters)
            if (m.alive() && std::abs(m.u.x - player.x) < 12 && std::abs(m.u.y - player.y) < 12) c.units.push_back(&m.u);
        for (auto& p : pets)                                 // pets stand in the way too (Bone Wall)
            if (p.m.alive() && p.where == level) c.units.push_back(&p.m.u);
    }

auto Fight::world(bool in_moor, std::uint32_t ms, float dt, const Crowd& crowd) -> void {
        // Monsters think while the player is near (D2 runs the rooms
        // around each player); what they hit comes off the player's life,
        // and a hit of a twelfth of max life or more makes them flinch (GH).
        if (in_moor) {
            std::vector<Foe> foes{ Foe{ player.x, player.y, int(cc.stats.get(d2d::d2s::kLevel)), true, player.walking, pf },
                                   Foe{ merc ? merc->x : 0, merc ? merc->y : 0, merc_st.level, merc && merc_mode != "DT",
                                        merc && merc->walking, merc_fighter() } };
            for (std::size_t k = 0; k < 2; ++k)                  // Amplify Damage on them: damage reduced -100 %
                if (ms < amplified[k]) foes[k].f.dr_pct -= 100;
            monster_auras(foes, ms);
            for (const auto& p : pets) foes.push_back(pet_foe(p));
            for (std::size_t i = 0; i < monsters.size(); ++i) {
                auto& m = monsters[i];
                if (std::abs(m.u.x - player.x) < 30 && std::abs(m.u.y - player.y) < 30
                    && monster_update(*scene, *level, m, foes, rng, ms, dt, crowd, missiles))
                    killed(i, ms);                           // on the player's thorns
            }
            boss_events(foes, ms);
            monster_dots(ms, dt);
            monster_states(ms);
            buff_events(foes[0], ms);
            buff_tick(ms);
            aura_pulse(ms);
            for (auto& m : monsters) m.in_aura = m.alive() && in_aura(m);   // its auratargetstate shows (states.md)
            for (std::size_t k = 0; k < pets.size(); ++k) pet_hurt(pets[k], foes[2 + k].damage, ms);
            pets_turn(ms, dt, crowd);
            missile_tick(ms);
            // The merc's arrows strike the first live monster they reach.
            missiles_update(*level, missiles, foes, rng, ms, dt, [&](Missile& a) {
                for (std::size_t i = 0; i < monsters.size(); ++i) {
                    auto& m = monsters[i];
                    if (!m.alive() || std::hypot(m.u.x - a.x, m.u.y - a.y) > 0.5f) continue;
                    if (a.row) {                             // a shrine's potion: everyone in its burst
                        for (std::size_t j = 0; j < monsters.size(); ++j)
                            if (monsters[j].alive() && std::hypot(monsters[j].u.x - a.x, monsters[j].u.y - a.y) * 5 <= float(std::max(a.burst, 1)))
                                land(j, d2d::rules::missile_blow(*a.row, target_of(j), {}, rng), true, ms);
                        return true;
                    }
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
                    merc_until = ms + scene->npc_timing(*merc_npc, mode).length_ms();
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
            for (std::size_t k = 0; k < 2; ++k)                  // a Cursed boss's Amplify Damage: its auralen
                if (const auto* s = scene->skills.get(66); s && foes[k].amplify > 0) {
                    amplified[k] = ms + std::uint32_t(std::max(calc(*s, s->auralen, foes[k].amplify), 25)) * 40;
                    d2d::log::info("{} cursed: Amplify Damage level {}", k ? "the merc" : "the player", foes[k].amplify);
                }
            if (foe.mana_burn > 0) {                             // Mana Burn
                cc.stats.v[d2d::d2s::kMana] = std::max<std::int64_t>(cc.stats.v[d2d::d2s::kMana] - (std::int64_t(foe.mana_burn) << 8), 0);
                d2d::log::info("mana burn: -{} mana", foe.mana_burn);
            }
            if (foe.blocked && pmode < 0) set_pmode(kModeBL, ms);   // a block plays out (FBR)
            foe.damage = absorb(foe.damage);
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

auto Fight::monster_auras(std::vector<Foe>& foes, std::uint32_t ms) -> void {
        for (auto& m : monsters) m.aura_dmg = m.aura_th = 0;
        for (auto& b : monsters) {
            if (!b.alive() || !b.aura || std::abs(b.u.x - player.x) >= 30 || std::abs(b.u.y - player.y) >= 30) continue;
            const auto* s = scene->skills.get(b.aura);
            if (!s) continue;
            const d2d::rules::CalcEnv env{ [](int) { return 0; }, [](int) { return 0; }, [](int) { return 0; }, b.st.level, &rng };
            auto val = [&](const d2d::rules::Calc& c) { return d2d::rules::eval_calc(scene->skills, c, env, s->id, b.aura_lvl); };
            const float r = float(val(s->aurarange)) / 5;                     // subtiles -> cells
            auto in_range = [&](float x, float y) { return std::hypot(x - b.u.x, y - b.u.y) <= r; };
            if (s->srvdofunc == 65) {
                for (auto& m : monsters)
                    if (m.alive() && in_range(m.u.x, m.u.y))
                        for (std::size_t i = 0; i < s->aurastat.size(); ++i) {
                            if (s->aurastat[i] == 25) m.aura_dmg += val(s->aura_calc[i]);     // damagepercent
                            if (s->aurastat[i] == 119) m.aura_th += val(s->aura_calc[i]);     // item_tohit_percent
                        }
                continue;
            }
            for (auto& f : foes) {
                if (!f.alive || !in_range(f.x, f.y)) continue;
                for (std::size_t i = 0; i < s->aurastat.size(); ++i) {
                    const int v = val(s->aura_calc[i]);
                    switch (s->aurastat[i]) {
                        case 39: f.f.res[0] += v; break;                         // fireresist
                        case 41: f.f.res[1] += v; break;                         // lightresist
                        case 43: f.f.res[2] += v; break;                         // coldresist
                        case 171: f.f.defense += f.f.defense * v / 100; break;   // skill_armor_percent
                        default: break;
                    }
                }
            }
            if (s->etype < 0 || s->etype > 2 || ms < b.aura_next) continue;
            b.aura_next = ms + std::uint32_t(std::max(s->perdelay, 25)) * 40;
            const int lo = d2d::rules::elem_damage(scene->skills, *s, env, b.aura_lvl, false) >> 8;
            const int hi = std::max(d2d::rules::elem_damage(scene->skills, *s, env, b.aura_lvl, true) >> 8, lo);
            for (auto& f : foes)
                if (f.alive && in_range(f.x, f.y)) f.damage += d2d::rules::resisted(rng.range(lo, hi), f.f.res[std::size_t(s->etype)]);
        }
    }

auto Fight::shrine_missiles(int code, float x, float y, int clvl, std::uint32_t ms) -> void {
        const int lvl = std::clamp(clvl / 5, 1, 8);
        if (code == 19) {
            const auto m = scene->missiles.find("fireball");
            const auto k = scene->skills.by_name.find("Fire Ball");
            if (m == scene->missiles.end() || k == scene->skills.by_name.end()) return;
            for (int ring = 1; ring <= 4; ++ring)
                for (const int ty : { 5, -10, 15, -20 }) {
                    const float tx = float(ring % 2 ? 5 * ring : -5 * ring);
                    launch(m->second, *scene->skills.get(k->second), lvl, x, y, tx / 5, float(ty) / 5, m->second.range, ms);
                }
            return;
        }
        const auto m = scene->missiles.find(code == 21 ? "explosivepotion" : "chokinggaspoition");
        if (m == scene->missiles.end()) return;
        const auto& mi = m->second;
        const auto md = d2d::rules::row_damage(mi.etype, mi.emin, mi.emax, mi.emin_lev, mi.emax_lev, mi.hitshift, mi.elen, mi.elen_lev, lvl);
        for (const auto& [dx, dy] : { std::pair{ -6, 6 }, { -6, -6 }, { 0, 6 }, { 0, -6 }, { 6, 6 }, { 6, -6 } }) {
            const float fx = float(dx) / 5, fy = float(dy) / 5, v = cells_per_sec(float(mi.vel)), d = std::hypot(fx, fy);
            Missile a{ &mi, x, y, fx / d * v, fy / d * v, direction32(fx, fy), ms, ms + std::uint32_t(std::max(mi.range, 1)) * 40, {} };
            a.friendly = true; a.row = md; a.burst = mi.hit_par1;
            pending.push_back(a);
        }
    }

auto Fight::enter(const Level* to) -> void {
        if (to == &scene->town || to == mon_level) return;
        if (mon_level) kept[mon_level] = std::move(monsters);
        if (const auto k = kept.find(to); k != kept.end()) {
            monsters = std::move(k->second);
            kept.erase(k);
        } else {
            monsters = spawn_monsters(*scene, *to, rng, game_difficulty);
            for (auto& m : monsters) m.id = next_id++;
        }
        mon_level = to;
        missiles.clear();
        attack_mon = merc_target = -1;
        for (auto& p : pets) p.target = -1;
    }

}  // namespace d2d::game
