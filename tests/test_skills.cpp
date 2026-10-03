// SPDX-License-Identifier: GPL-3.0-or-later
// Skills over hand-made rows: the calc compiler on expressions from
// Skills.txt, ln / dm, the level brackets, elemental damage with a synergy,
// mana cost, item skill bonuses.
#include <combat.hpp>
#include <d2s_items.hpp>
#include <sequences.hpp>
#include <skills.hpp>

#include <algorithm>
#include <array>
#include <cassert>
#include <cstdio>
#include <string_view>
#include <vector>

using namespace d2d::rules;

int main() {
    SkillTables skill_tables;
    skill_tables.names.operands = { "ln12", "dm12", "ln34", "dm34", "ln56", "dm56", "ln78", "dm78", "par1", "par2", "par3", "par4",
                         "par5", "par6", "par7", "par8", "lvl", "edmn", "edmx", "edln", "toht", "mana", "mps" };
    skill_tables.names.operands.resize(73);
    skill_tables.names.operands[40] = "ulvl";
    skill_tables.names.operands[41] = "blvl";
    skill_tables.names.operands[55] = "clc1";
    skill_tables.names.stats = { { "passive_fire_mastery", 329 } };
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
    skill_tables.rows = { bash, stun, bolt };
    for (const auto& skill : skill_tables.rows) skill_tables.names.skills[skill.name] = skill.id, skill_tables.by_name[skill.name] = skill.id;

    std::string err;
    auto compile = [&](std::string_view expression) { err.clear(); return compile_calc(expression, skill_tables.names, &err); };
    int stun_blvl = 3;
    CalcEnv env{ .base_level = [&](int skill_id) { return skill_id == 1 ? stun_blvl : 4; },
                 .level = [&](int skill_id) { return skill_id == 1 ? stun_blvl + 1 : 5; },
                 .stat = [](int stat) { return stat == 329 ? 40 : 0; }, .clvl = 30 };
    auto evaluate = [&](std::string_view expression, int skill, int lvl) {
        const auto calc = compile(expression);
        if (!err.empty()) std::printf("calc error: %s in %.*s\n", err.c_str(), int(expression.size()), expression.data());
        assert(err.empty());
        return eval_calc(skill_tables, calc, env, skill, lvl);
    };
    // Bash calc1: ln12 + skill('Stun'.blvl) * par8 = 50 + 5 x 3 + 3 x 5.
    assert(evaluate("ln12+skill('Stun'.blvl)*par8", 0, 4) == 50 + 15 + 15);
    assert(evaluate("15+lvl*5+skill('Stun'.blvl)*par7", 0, 2) == 15 + 10 + 15);
    // Ternaries, comparisons, precedence, min / max / unary minus, division by 0.
    assert(evaluate("(lvl < 4) ?lvl:(2+lvl/3)", 0, 3) == 3 && evaluate("(lvl < 4) ?lvl:(2+lvl/3)", 0, 9) == 5);
    assert(evaluate("(lvl < 5) ? lvl : min(12,5+(lvl-5)/3)", 0, 40) == 12);
    assert(evaluate("((lvl < 4) ? 0 : ((lvl-3)*par3))", 0, 10) == 7);
    assert(evaluate("-par1+2*3", 0, 1) == -44 && evaluate("max(1,0-2)", 0, 1) == 1 && compile("max(1,-2)").empty() && evaluate("par1/0", 0, 1) == 0);
    assert(evaluate("stat('passive_fire_mastery'.accr)", 0, 1) == 40 && evaluate("ulvl", 0, 1) == 30);
    assert(evaluate("blvl", 0, 7) == 4 && evaluate("clc1", 0, 1) == 0);
    // game.exe's compiler (FUN_006c1ae0) on odd text: names by their first
    // four characters, unknown ones 0, `?` below the comparisons, `^`, an
    // open "(" ends the program; too few operands stores no calc.
    assert(evaluate("zzzz+5", 0, 1) == 5 && evaluate("2^3", 0, 1) == 8 && evaluate("(1)2", 0, 1) == 2);
    assert(evaluate("lvl<4?1:2", 0, 3) == 0 && evaluate("(lvl<4)?1:2", 0, 3) == 1);
    assert(evaluate("1+(2", 0, 1) == 2 && evaluate("lvl#2", 0, 7) == 7 && evaluate("Min(lvl,3)", 0, 7) == 3);
    assert(compile("1+").empty() && !err.empty() && compile("min(lvl,-1)").empty() && compile("lvl)").empty());
    assert(evaluate("\"min(par1,par2)\"", 0, 1) == 5);                      // quoted cells
    assert(evaluate("(par1*2+par2", 0, 1) == 105);                          // Fire Wall's missing ")"
    // Bone Wall's calc2 "par34" (a Skills.txt typo) reads par3: 8 at every level.
    Skill bone_wall;
    bone_wall.id = 3; bone_wall.name = "Bone Wall"; bone_wall.par = { 25, 600, 8, 0, 0, 0, 0, 10 };
    skill_tables.rows.push_back(bone_wall);
    for (const int level : { 1, 10, 20 }) assert(evaluate("par34", 3, level) == 8);
    skill_tables.rows.pop_back();

    // ln / dm (FUN_004e6ca0 / FUN_00645b20) and the brackets (FUN_00644b70).
    assert(calc_ln(50, 5, 4) == 65 && calc_ln(50, 5, 0) == 0);
    assert(calc_dm(25, 75, 1) == 25 + 15 * 50 / 100 && calc_dm(0, 100, 1000) == 100);
    const std::array<int, 5> lev{ 1, 2, 3, 4, 5 };
    assert(level_bonus(lev, 1) == 0 && level_bonus(lev, 8) == 7 && level_bonus(lev, 9) == 7 + 2);
    assert(level_bonus(lev, 17) == 7 + 16 + 3 && level_bonus(lev, 23) == 7 + 16 + 18 + 4);
    assert(level_bonus(lev, 30) == 7 + 16 + 18 + 24 + 10);
    // Elemental damage: (EMin + brackets) << HitShift, + the synergy %.
    skill_tables.rows[2].edmg_sym = compile("skill('Bash'.blvl)*par8");     // 4 x 16 = +64%
    const auto& fire_bolt = skill_tables.rows[2];
    assert(elem_damage(skill_tables, fire_bolt, CalcEnv{}, 1, false) == 3 * 256);
    assert(elem_damage(skill_tables, fire_bolt, env, 3, true) == (6 + 2) * 256 * 164 / 100);
    assert(evaluate("edmn", 2, 1) == 3 * 256 * 164 / 100 >> 8);
    // Physical: (MinDam + brackets + DmgSymPerCalc %) << HitShift.
    skill_tables.rows[0].mindam = 2; skill_tables.rows[0].mindam_lev = { 1, 1, 1, 1, 1 }; skill_tables.rows[0].dmg_sym = compile("par8*10");
    assert(skill_phys(skill_tables, skill_tables.rows[0], env, 3, false) == (4 + 4 * 50 / 100) << 8);
    // Mana cost: (mana + lvlmana x (lvl - 1)) << manashift, at least minmana.
    assert(mana_cost(fire_bolt, 1) == 5 << 7 && mana_cost(fire_bolt, 5) == 9 << 7);
    assert(mana_cost(skill_tables.rows[0], 1) == 512 && mana_cost(skill_tables.rows[0], 0) == 0);
    Skill cheap = skill_tables.rows[0];
    cheap.mana = 0;
    assert(mana_cost(cheap, 3) == 256);                     // minmana 1

    // Item bonuses: all skills, class skills, the skill's tab, single skills.
    using P = d2d::d2s::ItemProp;
    const std::vector<P> props{ { .stat = 127, .value = 1 }, { .stat = 83, .param = 4, .value = 2 },
                                { .stat = 188, .param = 4 * 8 + 1, .value = 3 }, { .stat = 107, .param = 0, .value = 1 },
                                { .stat = 83, .param = 1, .value = 5 }, { .stat = 97, .param = 2, .value = 2 } };
    assert(item_skill_bonus(skill_tables.rows[0], 4, props) == 1 + 2 + 3 + 1);   // Bash, barbarian, page 2 = tab 1
    assert(item_skill_bonus(skill_tables.rows[2], 4, props) == 2);                // Fire Bolt for a barbarian: the oskill only

    // Charge-ups: Tiger Strike's calc1 x charges ED, Cobra Strike's ln12
    // steal (life, then mana too, then both doubled), prgdam 4's element.
    Skill tiger;
    tiger.id = 3; tiger.prgdam = 1; tiger.par = { 100, 20 }; tiger.calc[0] = compile("ln12");
    skill_tables.rows.push_back(tiger);                                // calcs read their params by skill id
    assert(charge_bonus(skill_tables, tiger, env, 3, 2).ed_pct == 140 * 2);
    assert(charge_bonus(skill_tables, tiger, env, 3, 5).ed_pct == 140 * 3);        // at most 3
    Skill cobra;
    cobra.prgdam = 2; cobra.par = { 40, 5 };
    const auto charge1 = charge_bonus(skill_tables, cobra, env, 1, 1), charge2 = charge_bonus(skill_tables, cobra, env, 1, 2), charge3 = charge_bonus(skill_tables, cobra, env, 1, 3);
    assert(charge1.life_steal == 40 && charge1.mana_steal == 0 && charge2.mana_steal == 40 && charge3.life_steal == 80 && charge3.mana_steal == 80);
    Skill fists = bolt;
    fists.prgdam = 4;
    const auto fists_charge = charge_bonus(skill_tables, fists, CalcEnv{}, 1, 1);
    assert(fists_charge.etype == bolt.etype && fists_charge.elem_lo == 3 && charge_bonus(skill_tables, fists, CalcEnv{}, 1, 0).etype == -1);

    // Sequences (0x7483b8): Jab with a spear, 21 frames, three hits; the
    // claws' seq 16 with two claws hits in A2 then S4; none for a bow.
    const auto jab = sequence(1, "2ht");
    assert(jab.size() == 21 && std::ranges::count(jab, 1, &SeqFrame::event) == 3 && jab[0].mode == 7 && jab[0].frame == 2);
    const auto claws = sequence(16, "ht2");
    assert(claws.size() == 16 && claws[6].event == 1 && claws[6].mode == 8 && claws[10].event == 1 && claws[10].mode == 16);
    assert(sequence(1, "bow").empty() && sequence(23, "hth").size() == 19);
    // Whirlwind's hit gap (FUN_005d9320's brackets).
    assert(whirlwind_gap(11) == 4 && whirlwind_gap(12) == 6 && whirlwind_gap(19) == 10 && whirlwind_gap(25) == 14 && whirlwind_gap(26) == 16);
    // A missile's elemental damage with the mastery (FUN_00644c90): % of
    // the damage after the synergy. Fire Bolt, stat 329 = 40.
    {
        Skill fire = skill_tables.rows[2];
        fire.etype = 0;
        assert(elem_damage(skill_tables, fire, env, 1, false, 0, true) == elem_damage(skill_tables, fire, env, 1, false) * 140 / 100);
    }
    // Passives (FUN_00646d60): each passivestat at the skill's level, on its
    // passiveitype's layer; skills with no level give nothing.
    {
        SkillTables passive_tables = skill_tables;
        Skill mastery;
        mastery.id = int(passive_tables.rows.size()); mastery.name = "Sword Mastery"; mastery.passive = true; mastery.passive_itype = "swor";
        mastery.par = { 28, 7, 30, 5, 1, 1, 0, 0 };
        mastery.passive_stat = { 342, 343, -1, 344, -1 };             // stops at the first empty one
        mastery.passive_calc[0] = compile_calc("ln12", passive_tables.names); mastery.passive_calc[1] = compile_calc("ln34", passive_tables.names);
        Skill idle = mastery;
        idle.id = mastery.id + 1; idle.passive_itype.clear();
        passive_tables.rows.push_back(mastery); passive_tables.rows.push_back(idle);
        CalcEnv passive_env{ .level = [&](int skill_id) { return skill_id == mastery.id ? 3 : 0; } };
        const auto passives = passive_stats(passive_tables, passive_env);
        assert(passives.size() == 2 && passives[0].stat == 342 && passives[0].value == 28 + 14 && passives[0].itype == "swor");
        assert(passives[1].stat == 343 && passives[1].value == 30 + 10);
        // An aura's passive stats count too (Holy Fire's weapon fire), on
        // or off (FUN_00646d60 while off, FUN_005cf3a0's state while on).
        Skill fire_aura = idle;
        fire_aura.id = int(passive_tables.rows.size()); fire_aura.passive = false; fire_aura.aura = true;
        passive_tables.rows.push_back(fire_aura);
        CalcEnv aura_env{ .level = [&](int skill_id) { return skill_id == fire_aura.id ? 2 : 0; } };
        assert(passive_stats(passive_tables, aura_env).size() == 2);
    }
    // A skill-less missile row's own element (FUN_0064b100 ..): brackets,
    // HitShift, length brackets. Claws of Thunder's nova at level 9.
    {
        const auto damage = row_damage(1, 1, 30, { 0, 0, 0, 0, 0 }, { 15, 25, 0, 0, 0 }, 8, 10, { 2, 1, 0 }, 9);
        assert(damage.etype == 1 && damage.elo == 1 << 8 && damage.ehi == (30 + 15 * 7 + 25) << 8 && damage.elen == 10 + 14 + 1);
    }
    // enms / exms (52 / 53): the elemental damage with the mastery, 256ths.
    {
        SkillTables element_tables = skill_tables;
        element_tables.names.operands[52] = "enms";
        Skill fire = element_tables.rows[2];
        fire.etype = 0;
        fire.passive_calc[0] = compile_calc("enms*6/256", element_tables.names);
        element_tables.rows[2] = fire;
        assert(eval_calc(element_tables, fire.passive_calc[0], env, 2, 1) == elem_damage(element_tables, fire, env, 1, false, 0, true) * 6 / 256);
    }
    // The char panel's attack block (FUN_004ed570): Bash's descdam 7 /
    // descatt 1 over a 3-7 weapon at +30 %, fire 2-4, attack rating 100 at
    // +20 % (5 of it the mastery).
    {
        Fighter fighter;
        fighter.phys_lo = 3 << 8; fighter.phys_hi = 7 << 8; fighter.phys_pct = 30;
        fighter.ar_base = 100; fighter.ar_pct = 20;
        fighter.elem[0] = { 2, 4 };
        Skill weapon_skill = skill_tables.rows[0];
        weapon_skill.descdam = 7; weapon_skill.descatt = 1; weapon_skill.mindam = weapon_skill.maxdam = 0; weapon_skill.mindam_lev = weapon_skill.maxdam_lev = {};
        weapon_skill.ddam_calc1 = compile("par1"); weapon_skill.ddam_calc2 = compile("5");
        weapon_skill.tohit = 20; weapon_skill.levtohit = 5;
        auto line = attack_line(skill_tables, weapon_skill, env, 5, fighter, 5);
        // (3 x 256 x 180 % >> 8) + 5 + 2, (7 x 256 x 180 % >> 8) + 5 + 4; 100 x (20 - 5 + 40) %.
        assert(line.damage && line.min == 5 + 5 + 2 && line.max == 12 + 5 + 4 && line.damage_colour == 1);
        assert(line.attack_rating == 155);
        weapon_skill.descatt = 2;                           // the mastery stays in
        assert(attack_line(skill_tables, weapon_skill, env, 5, fighter, 5).attack_rating == 160);
        fighter.ar_base += 5;                               // a dexterity point: 5 rating before the %
        assert(attack_line(skill_tables, weapon_skill, env, 5, fighter, 5).attack_rating == 168);
        Skill spell = skill_tables.rows[2];                 // Fire Bolt: descdam 5, its own damage, fire's red, no rating
        spell.descdam = 5; spell.etype = 0;
        line = attack_line(skill_tables, spell, env, 5, fighter, 5);
        assert(line.damage && line.min == elem_damage(skill_tables, spell, env, 5, false, 0, true) >> 8 && line.damage_colour == 1);
        assert(line.attack_rating == 0);
    }
    std::puts("ok");
}
