// Skills over hand-made rows: the calc compiler on expressions from
// Skills.txt, ln / dm, the level brackets, elemental damage with a synergy,
// mana cost, item skill bonuses.
#include <sequences.hpp>
#include <skills.hpp>

#include <algorithm>
#include <cassert>
#include <cstdio>

using namespace d2d::rules;

int main() {
    SkillTables t;
    t.names.operands = { "ln12", "dm12", "ln34", "dm34", "ln56", "dm56", "ln78", "dm78", "par1", "par2", "par3", "par4",
                         "par5", "par6", "par7", "par8", "lvl", "edmn", "edmx", "edln", "toht", "mana", "mps" };
    t.names.operands.resize(73);
    t.names.operands[40] = "ulvl";
    t.names.operands[41] = "blvl";
    t.names.operands[55] = "clc1";
    t.names.stats = { { "passive_fire_mastery", 329 } };
    Skill bash;
    bash.id = 0; bash.name = "Bash"; bash.cls = "bar"; bash.page = 2;
    bash.par = { 50, 5, 1, 1, 0, 0, 5, 5 };
    bash.mana = 2; bash.manashift = 8; bash.minmana = 1;
    Skill stun;
    stun.id = 1; stun.name = "Stun";
    Skill bolt;                                             // Fire Bolt's damage columns
    bolt.id = 2; bolt.name = "Fire Bolt"; bolt.cls = "sor"; bolt.hitshift = 8; bolt.par[7] = 16;
    bolt.emin = 3; bolt.emax = 6; bolt.emin_lev = { 1, 2, 3, 4, 5 }; bolt.emax_lev = { 1, 2, 3, 4, 5 };
    bolt.mana = 5; bolt.lvlmana = 1; bolt.manashift = 7;
    t.rows = { bash, stun, bolt };
    for (const auto& r : t.rows) t.names.skills[r.name] = r.id, t.by_name[r.name] = r.id;

    std::string err;
    auto C = [&](std::string_view e) { err.clear(); return compile_calc(e, t.names, &err); };
    int stun_blvl = 3;
    CalcEnv env{ .base_level = [&](int s) { return s == 1 ? stun_blvl : 4; },
                 .level = [&](int s) { return s == 1 ? stun_blvl + 1 : 5; },
                 .stat = [](int st) { return st == 329 ? 40 : 0; }, .clvl = 30 };
    auto E = [&](std::string_view e, int skill, int lvl) {
        const auto c = C(e);
        if (!err.empty()) std::printf("calc error: %s in %.*s\n", err.c_str(), int(e.size()), e.data());
        assert(err.empty());
        return eval_calc(t, c, env, skill, lvl);
    };
    // Bash calc1: ln12 + skill('Stun'.blvl) * par8 = 50 + 5 x 3 + 3 x 5.
    assert(E("ln12+skill('Stun'.blvl)*par8", 0, 4) == 50 + 15 + 15);
    assert(E("15+lvl*5+skill('Stun'.blvl)*par7", 0, 2) == 15 + 10 + 15);
    // Ternaries, comparisons, precedence, min / max / unary minus, division by 0.
    assert(E("(lvl < 4) ?lvl:(2+lvl/3)", 0, 3) == 3 && E("(lvl < 4) ?lvl:(2+lvl/3)", 0, 9) == 5);
    assert(E("(lvl < 5) ? lvl : min(12,5+(lvl-5)/3)", 0, 40) == 12);
    assert(E("((lvl < 4) ? 0 : ((lvl-3)*par3))", 0, 10) == 7);
    assert(E("-par1+2*3", 0, 1) == -44 && E("max(1,-2)", 0, 1) == 1 && E("par1/0", 0, 1) == 0);
    assert(E("stat('passive_fire_mastery'.accr)", 0, 1) == 40 && E("ulvl", 0, 1) == 30);
    assert(E("blvl", 0, 7) == 4 && E("clc1", 0, 1) == 0);
    // Unreadable text: empty calc, 0, and why.
    assert(C("par34").empty() && err.find("par34") != std::string::npos);
    assert(C("skill('Nobody'.blvl)").empty() && C("1+").empty() && C("min(1").empty());
    assert(E("\"min(par1,par2)\"", 0, 1) == 5);                      // quoted cells
    assert(E("(par1*2+par2", 0, 1) == 105);                          // Fire Wall's missing ")"
    assert(C("(1)2").empty());                                       // but not in the middle

    // ln / dm (FUN_004e6ca0 / FUN_00645b20) and the brackets (FUN_00644b70).
    assert(calc_ln(50, 5, 4) == 65 && calc_ln(50, 5, 0) == 0);
    assert(calc_dm(25, 75, 1) == 25 + 15 * 50 / 100 && calc_dm(0, 100, 1000) == 100);
    const std::array<int, 5> lev{ 1, 2, 3, 4, 5 };
    assert(level_bonus(lev, 1) == 0 && level_bonus(lev, 8) == 7 && level_bonus(lev, 9) == 7 + 2);
    assert(level_bonus(lev, 17) == 7 + 16 + 3 && level_bonus(lev, 23) == 7 + 16 + 18 + 4);
    assert(level_bonus(lev, 30) == 7 + 16 + 18 + 24 + 10);
    // Elemental damage: (EMin + brackets) << HitShift, + the synergy %.
    t.rows[2].edmg_sym = C("skill('Bash'.blvl)*par8");     // 4 x 16 = +64%
    const auto& fb = t.rows[2];
    assert(elem_damage(t, fb, CalcEnv{}, 1, false) == 3 * 256);
    assert(elem_damage(t, fb, env, 3, true) == (6 + 2) * 256 * 164 / 100);
    assert(E("edmn", 2, 1) == 3 * 256 * 164 / 100 >> 8);
    // Physical: (MinDam + brackets + DmgSymPerCalc %) << HitShift.
    t.rows[0].mindam = 2; t.rows[0].mindam_lev = { 1, 1, 1, 1, 1 }; t.rows[0].dmg_sym = C("par8*10");
    assert(skill_phys(t, t.rows[0], env, 3, false) == (4 + 4 * 50 / 100) << 8);
    // Mana cost: (mana + lvlmana x (lvl - 1)) << manashift, at least minmana.
    assert(mana_cost(fb, 1) == 5 << 7 && mana_cost(fb, 5) == 9 << 7);
    assert(mana_cost(t.rows[0], 1) == 512 && mana_cost(t.rows[0], 0) == 0);
    Skill cheap = t.rows[0];
    cheap.mana = 0;
    assert(mana_cost(cheap, 3) == 256);                     // minmana 1

    // Item bonuses: all skills, class skills, the skill's tab, single skills.
    using P = d2d::d2s::ItemProp;
    const std::vector<P> props{ { .stat = 127, .value = 1 }, { .stat = 83, .param = 4, .value = 2 },
                                { .stat = 188, .param = 4 * 8 + 1, .value = 3 }, { .stat = 107, .param = 0, .value = 1 },
                                { .stat = 83, .param = 1, .value = 5 }, { .stat = 97, .param = 2, .value = 2 } };
    assert(item_skill_bonus(t.rows[0], 4, props) == 1 + 2 + 3 + 1);   // Bash, barbarian, page 2 = tab 1
    assert(item_skill_bonus(t.rows[2], 4, props) == 2);                // Fire Bolt for a barbarian: the oskill only

    // Charge-ups: Tiger Strike's calc1 x charges ED, Cobra Strike's ln12
    // steal (life, then mana too, then both doubled), prgdam 4's element.
    Skill tiger;
    tiger.id = 3; tiger.prgdam = 1; tiger.par = { 100, 20 }; tiger.calc[0] = C("ln12");
    t.rows.push_back(tiger);                                // calcs read their params by skill id
    assert(charge_bonus(t, tiger, env, 3, 2).ed_pct == 140 * 2);
    assert(charge_bonus(t, tiger, env, 3, 5).ed_pct == 140 * 3);        // at most 3
    Skill cobra;
    cobra.prgdam = 2; cobra.par = { 40, 5 };
    const auto c1 = charge_bonus(t, cobra, env, 1, 1), c2 = charge_bonus(t, cobra, env, 1, 2), c3 = charge_bonus(t, cobra, env, 1, 3);
    assert(c1.life_steal == 40 && c1.mana_steal == 0 && c2.mana_steal == 40 && c3.life_steal == 80 && c3.mana_steal == 80);
    Skill fists = bolt;
    fists.prgdam = 4;
    const auto cf = charge_bonus(t, fists, CalcEnv{}, 1, 1);
    assert(cf.etype == bolt.etype && cf.elem_lo == 3 && charge_bonus(t, fists, CalcEnv{}, 1, 0).etype == -1);

    // Sequences (0x7483b8): Jab with a spear, 21 frames, three hits; the
    // claws' seq 16 with two claws hits in A2 then S4; none for a bow.
    const auto jab = sequence(1, "2ht");
    assert(jab.size() == 21 && std::ranges::count(jab, 1, &SeqFrame::event) == 3 && jab[0].mode == 7 && jab[0].frame == 2);
    const auto claws = sequence(16, "ht2");
    assert(claws.size() == 16 && claws[6].event == 1 && claws[6].mode == 8 && claws[10].event == 1 && claws[10].mode == 16);
    assert(sequence(1, "bow").empty() && sequence(23, "hth").size() == 19);
    // Whirlwind's hit gap (FUN_005d9320's brackets).
    assert(whirlwind_gap(11) == 4 && whirlwind_gap(12) == 6 && whirlwind_gap(19) == 10 && whirlwind_gap(25) == 14 && whirlwind_gap(26) == 16);
    std::puts("ok");
}
