// SPDX-License-Identifier: GPL-3.0-or-later
// The skill and weapon checks fight.hpp declares: which skills d2d runs as
// game.exe does, Throw's and the item-type gates, a skill's level.

#include "fight.hpp"

#include <character.hpp>
#include <d2s.hpp>
#include <d2s_items.hpp>
#include <gamedata.hpp>
#include <item_price.hpp>
#include <rules.hpp>
#include <skills.hpp>
#include <wear.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace d2d::game {

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
bool moving_skill(const d2d::rules::Skill& skill) {
    return runs(skill, ServerStartFunction::kWhirlwind, ServerDoFunction::kWhirlwind) || runs(skill, ServerStartFunction::kCharge, ServerDoFunction::kCharge)
        || runs(skill, ServerStartFunction::kLeapAttack, ServerDoFunction::kLeapAttack);
}
bool skill_built(const d2d::rules::Skill& skill) {
    return runs(skill, ServerStartFunction::kBuildHit, ServerDoFunction::kResolveHit) || runs(skill, ServerStartFunction::kElementalStrike, ServerDoFunction::kResolveHit)
        || runs(skill, ServerStartFunction::kBerserk, ServerDoFunction::kResolveHit) || runs(skill, ServerStartFunction::kVengeance, ServerDoFunction::kResolveHit)
        || runs(skill, ServerStartFunction::kDragonTalon, ServerDoFunction::kDragonTalon)
        || runs(skill, ServerStartFunction::kChargeUp, ServerDoFunction::kChargeUp) || runs(skill, ServerStartFunction::kChargeUp, ServerDoFunction::kElementalChargeUp)
        || runs(skill, ServerStartFunction::kDragonTail, ServerDoFunction::kDragonTail)
        || runs(skill, ServerStartFunction::kRepeatedHit, ServerDoFunction::kRepeatedHit) || runs(skill, ServerStartFunction::kSacrifice, ServerDoFunction::kSacrifice)
        || skill.srvdofunc == ServerDoFunction::kSmite
        || runs(skill, ServerStartFunction::kJab, ServerDoFunction::kJab) || runs(skill, ServerStartFunction::kDragonClaw, ServerDoFunction::kDragonClaw)
        || skill.srvdofunc == ServerDoFunction::kFrenzy || skill.srvdofunc == ServerDoFunction::kDoubleSwing
        || runs(skill, ServerStartFunction::kFend, ServerDoFunction::kRepeatedHit) || runs(skill, ServerStartFunction::kImpale, ServerDoFunction::kResolveHit)
        || runs(skill, ServerStartFunction::kPoisonDagger, ServerDoFunction::kPoisonDagger)
        || runs(skill, ServerStartFunction::kElementalStrike, ServerDoFunction::kChargedStrike) || runs(skill, ServerStartFunction::kLightningStrike, ServerDoFunction::kLightningStrike)
        || runs(skill, ServerStartFunction::kBuildHit, ServerDoFunction::kConversion)
        || runs(skill, ServerStartFunction::kStackingStrike, ServerDoFunction::kStackingStrike) || runs(skill, ServerStartFunction::kFireClaws, ServerDoFunction::kResolveHit)
        || skill.srvdofunc == ServerDoFunction::kHunger
        || runs(skill, ServerStartFunction::kRabies, ServerDoFunction::kRabies)
        || moving_skill(skill);
}
// Self casts (right click, no target): their aurastate on the caster for
// auralen frames with its aurastats — do 18 (FUN_005c9480: Holy Shield,
// the Sorceress's armors, Bone Armor, Cyclone Armor, Burst of Speed, Fade,
// Venom), 23 (Energy Shield, Blaze), 25 (Enchant, on the caster), 29
// (Thunder Storm), 47 (Cloak of Shadows).
bool self_cast(const d2d::rules::Skill& skill) {
    return (skill.srvstfunc == ServerStartFunction::kHolyShield || skill.srvstfunc == ServerStartFunction::kNone || skill.srvstfunc == ServerStartFunction::kThunderStorm
            || skill.srvstfunc == ServerStartFunction::kBladeShield) && !skill.aurastate.empty()
        && (skill.srvdofunc == ServerDoFunction::kSelfState || skill.srvdofunc == ServerDoFunction::kEnchant || skill.srvdofunc == ServerDoFunction::kThunderStorm
            || skill.srvdofunc == ServerDoFunction::kCloakOfShadows || (skill.srvdofunc == ServerDoFunction::kSelfStateWithMissile && skill.srvmissilea.empty())
            || (skill.srvdofunc == ServerDoFunction::kSelfStateWithMissile && skill.name == "Blaze") || skill.srvdofunc == ServerDoFunction::kShapeShift
            || skill.srvdofunc == ServerDoFunction::kStormAroundCaster || skill.srvdofunc == ServerDoFunction::kBladeShield);
}
// A throwable (FUN_0062ba80, ItemTypes Throwable): a stacking weapon with
// a weapons.txt missiletype (javelins, throwing knives / axes, potions).
bool throwable(const GameData& game_data, const std::string& code) {
    const auto found = game_data.rules.item_base.find(code);
    return game_data.thrown.contains(code) && found != game_data.rules.item_base.end() && found->second.stackable;
}
// Throw's weapon gate (FUN_00647640, skill 2): the item Throw takes
// (rules::throw_hand: the first usable weapon in hand, right then left, or
// when that isn't the wielded one, the other hand's item) is a throwable
// or has stat 125 (item_throwable). Only a dual-wielder (FUN_006235a0:
// barbarian, assassin) may take the other hand's; anyone else's first
// weapon must be the wielded one. A spent magic stack stays in hand broken
// (Fight::out_of_ammo), so it can't throw. docs/research/re/throw.md.
// No 1.14d item rolls stat 125 (stubs.md), so the launch takes only a
// throwable.
bool throw_ready(const GameData& game_data, const Character& character) {
    bool wielded = false;
    const int index = d2d::rules::throw_hand(game_data.rules, character.items, &wielded);
    const int cls = character.header.cls;
    if (index < 0 || (!wielded && cls != d2d::d2s::kBarbarian && cls != d2d::d2s::kAssassin)) return false;
    const auto& item = character.items[std::size_t(index)];
    return throwable(game_data, item.code) || std::ranges::any_of(item.props, [](const d2d::d2s::ItemProp& prop) { return prop.stat == 125 && prop.value > 0; });
}
// FUN_00643d90: one hand's item ("" type: none) against one of a skill's sets, `other` the other hand's. No item: the
// set is empty, or `bare`: whichever set it is, set a's itype1 (+0x18) is weap / mele / h2h (ItemTypes 0x2d / 0x2e /
// 0x43), set b's itype1 (+0x1e) is empty and the other hand holds no weapon (FUN_00629bb0(.., 0x2d); 0x643dc2..0x643df9).
// An item of an excluded etype fails; a set with no itype takes any other; else the item must be of one of them,
// and one that is: flagged 0x4000 (requirements unmet) or 0x100 (broken) only Attack (skill 0); one that shoots
// (its type's Shoots, FUN_0062e6f0), unless the skill is noammo (flags bit 16, byte +6 bit 0): Attack with its
// stat 0x9d goes, else the other hand must hold that ammunition, Attack with its stat 0x9d goes, and a stack
// (FUN_006295b0) needs its quantity (stat 0x46) above 0.
bool hand_fits(const GameData& game_data, const d2d::d2s::Item* item, const d2d::d2s::Item* other, const d2d::rules::Skill& skill,
                      const std::array<std::string, 3>& itypes, const std::array<std::string, 2>& etypes, bool bare) {
    const auto type_of = [&](const d2d::d2s::Item* held) {
        const auto info = held ? game_data.rules.item_info.find(held->code) : game_data.rules.item_info.end();
        return info == game_data.rules.item_info.end() ? std::string{} : info->second.type;
    };
    const std::string type = type_of(item);
    const auto of_type = [&](std::string_view want) { return !want.empty() && d2d::rules::type_is(game_data.rules, type, want); };
    if (type.empty()) return (itypes[0].empty() && etypes[0].empty()) || bare;
    if (std::ranges::any_of(etypes, of_type)) return false;
    if (itypes[0].empty()) return true;
    if (std::ranges::none_of(itypes, of_type)) return false;
    if (item->flags & 0x4100u) return skill.id == 0;
    const auto own_type = game_data.rules.types.find(type);
    const std::string shoots = own_type == game_data.rules.types.end() ? std::string{} : own_type->second.shoots;
    if (shoots.empty() || skill.noammo) return true;
    const auto no_consume = [&skill](const d2d::d2s::Item& held) {
        return skill.id == 0 && std::ranges::any_of(held.props, [](const d2d::d2s::ItemProp& prop) { return prop.stat == 0x9d && prop.value != 0; });
    };
    if (no_consume(*item)) return true;
    if (!other || !d2d::rules::type_is(game_data.rules, type_of(other), shoots)) return false;
    if (no_consume(*other)) return true;
    return d2d::rules::stack_most(game_data.rules, *other) == 0 || other->quantity > 0;
}
// FUN_00643f80, the skills' weapon-type gate (FUN_00647960's status 2): no itypea1 and no etypea1 passes; else one
// hand (right, left: FUN_0063bde0(inv, 4 / 5)) fits set a and the other set b, either way round. For Left Hand
// Throw / Swing (ids 4 / 5) the wielded weapon's hand (FUN_0063bef0) counts as empty.
bool weapon_gate(const GameData& game_data, const Character& character, const d2d::rules::Skill& skill) {
    if (skill.itype_a[0].empty() && skill.etype_a[0].empty()) return true;
    std::array<const d2d::d2s::Item*, 2> hands{};   // right, left
    for (const auto& item : character.items)
        if (item.location == d2d::d2s::item_location::kEquipped && (item.slot == d2d::d2s::body_location::kRightArm || item.slot == d2d::d2s::body_location::kLeftArm)
            && game_data.rules.item_info.contains(item.code))
            hands[item.slot == d2d::d2s::body_location::kRightArm ? 0 : 1] = &item;
    if (skill.id == 4 || skill.id == 5)
        if (const auto* wielded = d2d::rules::wielded_weapon(game_data.rules, character.items))
            hands[wielded->slot == d2d::d2s::body_location::kRightArm ? 0 : 1] = nullptr;
    const auto is_weapon = [&](const d2d::d2s::Item* held) {
        return held && d2d::rules::type_is(game_data.rules, game_data.rules.item_info.at(held->code).type, "weap");
    };
    const bool neither_weapon = std::ranges::none_of(hands, is_weapon);
    const auto& first = skill.itype_a[0];
    const bool bare = (first == "weap" || first == "mele" || first == "h2h") && skill.itype_b[0].empty() && neither_weapon;
    const auto fits_a = [&](const d2d::d2s::Item* held, const d2d::d2s::Item* other) { return hand_fits(game_data, held, other, skill, skill.itype_a, skill.etype_a, bare); };
    const auto fits_b = [&](const d2d::d2s::Item* held, const d2d::d2s::Item* other) { return hand_fits(game_data, held, other, skill, skill.itype_b, skill.etype_b, bare); };
    return (fits_a(hands[0], hands[1]) && fits_b(hands[1], hands[0])) || (fits_b(hands[0], hands[1]) && fits_a(hands[1], hands[0]));
}
// A known skill the character can't use now (FUN_00647960 says 2): the
// picker and the button draw it in text colour 1 (FUN_004aa3a0 /
// FUN_004a8d30 on FUN_004d9fc0), and a cast stops with the class's
// impossible_1 (FUN_004610c0, reason 0x13 at 0x711ddc).
// A skill with AttackNoMana swings Attack instead, with no voice (LAB_00461101): Fight::ask.
// The weapon-type gate (FUN_00643f80), Throw's its own (FUN_00647640: a throwable, or stat 125).
// FUN_00643f80 runs on Throw too (LAB_006479f8; itypea1 thro): no 1.14d item has stat 125, so
// throw_ready alone answers the same. skills.md "Can the skill go now".
// FUN_00647960's other statuses are Fight::skill_status's. Throw's gate takes a dual-wielder's
// primary hand (FUN_006235a0).
bool skill_refused(const GameData& game_data, const Character& character, int id) {
    const auto* skill = game_data.skills.get(id);
    if (!skill) return false;
    return skill->srvdofunc == d2d::rules::ServerDoFunction::kThrow ? !throw_ready(game_data, character) : !weapon_gate(game_data, character, *skill);
}
// A finishing move releases charges (FUN_005d5220 runs after Attack's
// srvdofunc and the finishers'): Attack, Dragon Talon, Dragon Tail, each
// Dragon Claw hit (FUN_005d6340 releases after FUN_005d6200's). Dragon
// Flight's kick (do 52, FUN_005d7850) releases too, in its own path.
bool finisher(const d2d::rules::Skill* skill) {
    return !skill || skill->id == 0 || skill->srvdofunc == ServerDoFunction::kDragonTalon || skill->srvdofunc == ServerDoFunction::kDragonTail || skill->srvdofunc == ServerDoFunction::kDragonClaw;
}

// The character's level in a skill (the World's for the fight, the skill
// bar's for show): points (a class skill's from the save's skill bytes in
// Skills.txt order; Attack and the class's CharStats Skill 1..10 (Throw,
// Kick, the scrolls and tomes, Unsummon, ...), which every one of the class
// has (FUN_00647ee0 -> FUN_00647110, item or not), 1), then with what the
// gear gives (gear_props: worn, socketed, set bonuses, charms) and `extra`
// (the skill shrine's +all skills) — on skills that have points or that an
// item grants (rules::item_grants_skill). A charged entry is cast at its
// own level (World's fight.skill_level, Hero::charge).
int skill_base_level(const GameData& game_data, const Character& character, int id) {
    const auto cls = std::size_t(std::clamp(character.character_class, 0, 6));
    const auto& ids = game_data.skills.class_ids[cls];
    if (const auto found = std::ranges::find(ids, id); found != ids.end()) return character.stats.skills[std::size_t(found - ids.begin())];
    if (id == 0) return 1;
    return std::ranges::any_of(game_data.class_start[cls].skills, [&](const std::string& name) {
        const auto found = game_data.skills.by_name.find(name);
        return found != game_data.skills.by_name.end() && found->second == id;
    }) ? 1 : 0;   // the weapon is skill_refused's
}
int skill_level(const GameData& game_data, const Character& character, int id, const std::vector<d2d::d2s::ItemProp>& extra) {
    const auto* skill = game_data.skills.get(id);
    const int base = skill_base_level(game_data, character, id);
    if (!skill || skill->cls.empty()) return base;
    std::vector<d2d::d2s::ItemProp> props = extra;
    const auto gear = gear_props(game_data, character.items);
    props.insert(props.end(), gear.begin(), gear.end());
    const int cls = std::max(character.character_class, 0);
    if (base == 0 && !d2d::rules::item_grants_skill(*skill, cls, props)) return 0;
    return std::max(base + d2d::rules::item_skill_bonus(*skill, cls, props), 0);
}

}  // namespace d2d::game
