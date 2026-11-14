// SPDX-License-Identifier: GPL-3.0-or-later
// Fight (fight.hpp): ammo, wear, missiles, bursts and auras. The rest is in this directory.
#include "fight.hpp"

#include "ai.hpp"
#include "character.hpp"
#include "cues.hpp"
#include "gamedata.hpp"
#include "inventory.hpp"
#include "item_text.hpp"

#include <aura_filter.hpp>
#include <combat.hpp>
#include <d2s_items.hpp>
#include <level_ids.hpp>
#include <missiles.hpp>
#include <monsters.hpp>
#include <rules.hpp>
#include <skills.hpp>
#include <sound_ids.hpp>
#include <uniques.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace d2d::game {

namespace {

constexpr int kTerror = 77;                     // Skills.txt Terror: Howl's state (FUN_005ddd00, 0x4d)

}  // namespace

auto Fight::other_target() -> void {
        const auto reach = in_reach();
        if (reach.empty()) { actor->attack_mon = -1; return; }
        actor->attack_mon = d2d::rules::ring_pick(reach, actor->attack_mon);   // FUN_0056bd10
        actor->player.dir = direction16(monsters[std::size_t(actor->attack_mon)].unit.x - actor->player.x, monsters[std::size_t(actor->attack_mon)].unit.y - actor->player.y);
    }

auto Fight::ammo_index() const -> int {
        using namespace d2d::d2s;
        const auto& tables = game_data->rules;
        auto arm = [&](int slot) -> int {
            for (std::size_t i = 0; i < actor->character.items.size(); ++i)
                if (actor->character.items[i].location == item_location::kEquipped && actor->character.items[i].slot == slot) return int(i);
            return -1;
        };
        const int right = arm(body_location::kRightArm), left = arm(body_location::kLeftArm);
        auto type_of = [&](int index) -> std::string {
            const auto found = index >= 0 ? tables.item_info.find(actor->character.items[std::size_t(index)].code) : tables.item_info.end();
            return found != tables.item_info.end() ? found->second.type : std::string{};
        };
        auto bow = [&](int index) { return d2d::rules::type_is(tables, type_of(index), "bow") || d2d::rules::type_is(tables, type_of(index), "xbow"); };
        auto ammo = [&](int index) { return index >= 0 && (d2d::rules::type_is(tables, type_of(index), "misl") || throwable(*game_data, actor->character.items[std::size_t(index)].code)); };
        int found = -1;
        if (bow(right) || bow(left)) found = ammo(right) && !bow(right) ? right : ammo(left) && !bow(left) ? left : -1;
        else found = right >= 0 && throwable(*game_data, actor->character.items[std::size_t(right)].code) ? right
                   : left >= 0 && throwable(*game_data, actor->character.items[std::size_t(left)].code) ? left : -1;
        return found >= 0 && actor->character.items[std::size_t(found)].quantity > 0 ? found : -1;
    }

auto Fight::weapon_missile() const -> const GameData::MissileInfo* {
        using namespace d2d::d2s;
        const auto& tables = game_data->rules;
        bool crossbow = false, bow = false;
        for (const auto& item : actor->character.items)
            if (item.location == item_location::kEquipped && (item.slot == body_location::kRightArm || item.slot == body_location::kLeftArm))
                if (const auto found = tables.item_info.find(item.code); found != tables.item_info.end()) {
                    crossbow = crossbow || d2d::rules::type_is(tables, found->second.type, "xbow");
                    bow = bow || d2d::rules::type_is(tables, found->second.type, "bow");
                }
        if (!bow && !crossbow) return nullptr;
        int magic = 0, exploding = 0;
        for (const auto& prop : gear_props(*game_data, actor->character.items)) {
            if (prop.stat == kItemMagicArrow) magic += prop.value;
            if (prop.stat == kItemExplosiveArrow) exploding += prop.value;
        }
        const char* name = magic > 0 ? "magicarrow" : exploding > 0 ? "explodingarrow" : crossbow ? "bolt" : "arrow";
        const auto found = game_data->missiles.find(name);
        return found != game_data->missiles.end() ? &found->second : nullptr;
    }

auto Fight::needs_ammo(const d2d::rules::Skill& skill) const -> bool {
        if (skill.srvdofunc == ServerDoFunction::kAttack) {
            const auto* shot = weapon_missile();
            return shot && shot != &game_data->missiles.at("magicarrow");
        }
        return skill.decquant || skill.srvdofunc == ServerDoFunction::kThrow || skill.srvstfunc == ServerStartFunction::kCheckAmmo;
    }

auto Fight::spend_ammo(int index) -> void {
        if (index < 0) index = ammo_index();
        if (index < 0) return;
        if (--actor->character.items[std::size_t(index)].quantity <= 0) out_of_ammo(std::size_t(index));
    }

auto Fight::out_of_ammo(std::size_t index) -> void {
        using namespace d2d::d2s;
        auto& item = actor->character.items[index];
        item.quantity = 0;
        const auto info = game_data->rules.item_info.find(item.code);
        if (info != game_data->rules.item_info.end() && info->second.kind == 2 && item.quality >= 4 && item.quality <= 9) {
            d2d::rules::wield_leaves(game_data->rules, actor->character.items, item);   // FUN_0055f850 -> FUN_0063d2b0
            item.flags |= 0x100u;
            item.durability = 0;
            settle_requirements(*game_data, actor->character);   // FUN_0055f850 -> FUN_0055dbc0
            return;
        }
        const std::string code = item.code;
        const auto slot = item.slot;
        d2d::rules::wield_leaves(game_data->rules, actor->character.items, item);
        actor->character.items.erase(actor->character.items.begin() + std::ptrdiff_t(index));
        const auto next = std::ranges::find_if(actor->character.items, [&](const Item& other) {
            return other.code == code && other.location == item_location::kStored && other.panel == item_panel::kInventory && !d2d::rules::broken(other); });
        // Only the first is tried, and only if it can be worn (FUN_00562a30 -> FUN_0062eaf0).
        if (next != actor->character.items.end()
            && d2d::rules::can_wear(game_data->rules, *next, slot, wearer(*game_data, actor->character.header.cls, actor->character.items, actor->character.stats), required_level(*game_data, *next, actor->character.header.cls))) {
            next->location = item_location::kEquipped; next->slot = slot; next->panel = 0;
            d2d::rules::wield_enters(game_data->rules, actor->character.items, *next);   // FUN_00562a30 -> FUN_0063d1d0
        }
        settle_requirements(*game_data, actor->character);   // FUN_00562a30 -> FUN_0055dbc0
    }

auto Fight::wear(std::size_t index, std::uint32_t now_ms) -> void {
        using namespace d2d::d2s;
        auto& item = actor->character.items[index];
        const auto& tables = game_data->rules;
        const auto info = tables.item_info.find(item.code);
        if (info == tables.item_info.end()) return;
        const bool armor = d2d::rules::type_is(tables, info->second.type, "armo"), weapon = d2d::rules::type_is(tables, info->second.type, "weap");
        if ((!armor && !weapon) || item.max_durability <= 0 || d2d::rules::indestructible(game_data->rules, item) || d2d::rules::broken(item)) return;
        const int chance = armor || throwable(*game_data, item.code) ? 10 : 4;
        if (int(rng(100)) >= chance) return;
        if (item.durability - 1 >= 1) { item.durability = std::min(item.durability - 1, d2d::rules::max_durability(item)); return; }
        if (armor) {
            item.flags |= 0x100u; item.durability = 0;
            settle_requirements(*game_data, actor->character);   // FUN_0055f850 -> FUN_0055dbc0
            cues.cue(d2d::rules::sound_ids::kCursorDurabilityBreak, now_ms, actor->player.x, actor->player.y);
            return;
        }
        const auto base = tables.item_base.find(item.code);
        if (base == tables.item_base.end() || !base->second.stackable) {
            item.durability = 0;
            cues.cue(d2d::rules::sound_ids::kCursorDurabilityBreak, now_ms, actor->player.x, actor->player.y);
            return;
        }
        if (item.quantity < 2) { out_of_ammo(index); return; }
        --item.quantity;
        item.durability = d2d::rules::max_durability(item);
    }

auto Fight::wear_weapon(std::uint32_t now_ms) -> void {
        using namespace d2d::d2s;
        for (std::size_t i = 0; i < actor->character.items.size(); ++i)
            if (actor->character.items[i].location == item_location::kEquipped && (actor->character.items[i].slot == body_location::kRightArm || actor->character.items[i].slot == body_location::kLeftArm)
                && game_data->rules.item_info.contains(actor->character.items[i].code) && game_data->rules.item_info.at(actor->character.items[i].code).kind == 2) { wear(i, now_ms); return; }
    }

auto Fight::wear_armor(std::uint32_t now_ms) -> void {
        using namespace d2d::d2s;
        static constexpr std::array<std::pair<int, int>, 7> kSlots{ { { body_location::kHead, 3 }, { body_location::kTorso, 5 }, { body_location::kRightArm, 4 },
            { body_location::kLeftArm, 4 }, { body_location::kBelt, 2 }, { body_location::kFeet, 2 }, { body_location::kGloves, 2 } } };
        std::array<int, 7> worn{};
        int total = 0;
        for (std::size_t slot_index = 0; slot_index < kSlots.size(); ++slot_index) {
            worn[slot_index] = -1;
            for (std::size_t i = 0; i < actor->character.items.size(); ++i)
                if (const auto& item = actor->character.items[i]; item.location == item_location::kEquipped && item.slot == kSlots[slot_index].first && game_data->rules.item_info.contains(item.code)
                    && d2d::rules::type_is(game_data->rules, game_data->rules.item_info.at(item.code).type, "armo")) { worn[slot_index] = int(i); total += kSlots[slot_index].second; }
        }
        if (total <= 0) return;
        auto slot_index = std::size_t(rng(int(kSlots.size())));
        int pick = rng(total);
        for (;; slot_index = (slot_index + 1) % kSlots.size()) {
            if (worn[slot_index] < 0) continue;
            if (pick < kSlots[slot_index].second) { wear(std::size_t(worn[slot_index]), now_ms); return; }
            pick -= kSlots[slot_index].second;
        }
    }

auto Fight::impale_wear(const d2d::rules::Skill& skill, std::uint32_t now_ms) -> void {
        const int lvl = skill_level ? skill_level(skill.id) : 1;
        const auto env = calc_env();
        if (int(rng(100)) >= d2d::rules::eval_calc(game_data->skills, skill.calc[1], env, skill.id, lvl)) return;
        for (std::size_t i = 0; i < actor->character.items.size(); ++i)
            if (auto& item = actor->character.items[i]; item.location == d2d::d2s::item_location::kEquipped && (item.slot == d2d::d2s::body_location::kRightArm || item.slot == d2d::d2s::body_location::kLeftArm)
                && game_data->rules.item_info.contains(item.code) && game_data->rules.item_info.at(item.code).kind == 2) {
                const auto found = game_data->rules.item_base.find(item.code);
                if (found != game_data->rules.item_base.end() && found->second.stackable) { if (--item.quantity <= 0) out_of_ammo(i); }
                else if (item.max_durability > 0 && !d2d::rules::indestructible(game_data->rules, item)) {
                    item.durability -= d2d::rules::eval_calc(game_data->skills, skill.calc[2], env, skill.id, lvl);
                    if (item.durability <= 0) {
                        d2d::rules::wield_leaves(game_data->rules, actor->character.items, item);
                        item.durability = 0; item.flags |= 0x100u;             // FUN_0055f850: it breaks
                        settle_requirements(*game_data, actor->character);
                        cues.cue(d2d::rules::sound_ids::kCursorDurabilityBreak, now_ms, actor->player.x, actor->player.y);
                    }
                }
                return;
            }
    }

auto Fight::skill_missile(const d2d::rules::Skill& skill, bool any_owner ) const -> const GameData::MissileInfo* {
        static constexpr ServerDoFunction kDo[] = { ServerDoFunction::kMissileFan, ServerDoFunction::kScatteredBolts, ServerDoFunction::kNova, ServerDoFunction::kGuidedMissile,
            ServerDoFunction::kStrafe, ServerDoFunction::kChainLightning, ServerDoFunction::kMissileAtTarget, ServerDoFunction::kMissileWall,
            ServerDoFunction::kBreath, ServerDoFunction::kBlessedHammer, ServerDoFunction::kFistOfTheHeavens, ServerDoFunction::kFirestorm,
            ServerDoFunction::kWindMissile, ServerDoFunction::kVolcano, ServerDoFunction::kShockField, ServerDoFunction::kBladeFury,
            ServerDoFunction::kWarCry, ServerDoFunction::kGrimWard, ServerDoFunction::kBladeSentinel, ServerDoFunction::kWakeOfDestruction,
            ServerDoFunction::kMonsterBreath };
        static constexpr ServerStartFunction kSt[] = { ServerStartFunction::kNone, ServerStartFunction::kCheckAmmo, ServerStartFunction::kStrafe,
            ServerStartFunction::kCheckMana, ServerStartFunction::kBladeFury, ServerStartFunction::kTargetCorpse };
        if (skill.srvdofunc == ServerDoFunction::kAttack) return weapon_missile();
        const bool plain = (skill.srvstfunc == ServerStartFunction::kNone || skill.srvstfunc == ServerStartFunction::kCheckAmmo) && skill.srvdofunc == ServerDoFunction::kNone;
        const bool multi = std::ranges::contains(kSt, skill.srvstfunc) && std::ranges::contains(kDo, skill.srvdofunc);
        const auto& name = plain ? skill.srvmissile : skill.srvmissilea;
        if ((!plain && !multi) || name.empty()) return nullptr;
        const auto found = game_data->missiles.find(name);
        static constexpr int kHit[] = { 0, 1, 2, 3, 4, 7, 9, 10, 12, 13, 14, 17, 18, 20, 21, 22, 26, 29, 36, 37, 47, 48, 56 };
        if (found == game_data->missiles.end() || !std::ranges::contains(kHit, found->second.hit_func)) return nullptr;
        return any_owner || found->second.skill == skill.name || found->second.skill.empty() ? &found->second : nullptr;
    }

auto Fight::missile_skill(const d2d::rules::Skill& skill) const -> bool { return skill_missile(skill) != nullptr; }

auto Fight::cast_missile(int skill, float target_x, float target_y, std::uint32_t now_ms) -> bool {
        using namespace d2d::d2s;
        const auto* skill_row = game_data->skills.get(skill);
        if (!skill_row || dead() || actor->pmode >= 0 || (!missile_skill(*skill_row) && !spot_skill(*skill_row))) return false;
        // Throw is a spot skill: a held one (engage) whose last one's gone, or the weapons
        // swapped since the command, stops here (FUN_004610c0's status 2).
        if (ask(skill, now_ms) != skill) { actor->attack_mon = -1; return false; }
        const int lvl = cast_level(skill);
        if (needs_ammo(*skill_row) && ammo_index() < 0) {
            // FUN_005da8b0 (srvstfunc 4): refused, and a spent stack in either hand (right first;
            // stackable, FUN_006289f0) goes as one run out (FUN_00580310), so the next has ammo.
            // Then FUN_00580380: a worn weapon (ItemTypes weap) with durability (FUN_00629930) worn
            // to 0 and not broken yet breaks (FUN_0055f850).
            for (const int slot : { body_location::kRightArm, body_location::kLeftArm }) {
                const auto& items = actor->character.items;
                const auto spent = std::ranges::find_if(items, [&](const Item& item) {
                    const auto base = game_data->rules.item_base.find(item.code);
                    return item.location == item_location::kEquipped && item.slot == slot && item.quantity < 1 && base != game_data->rules.item_base.end() && base->second.stackable;
                });
                if (spent != items.end() && !d2d::rules::broken(*spent)) out_of_ammo(std::size_t(spent - items.begin()));
            }
            for (auto& item : actor->character.items) {
                const auto base = game_data->rules.item_base.find(item.code);
                const auto info = game_data->rules.item_info.find(item.code);
                if (item.location != item_location::kEquipped || base == game_data->rules.item_base.end() || info == game_data->rules.item_info.end()
                    || !d2d::rules::type_is(game_data->rules, info->second.type, "weap") || base->second.no_durability || base->second.durability <= 0
                    || d2d::rules::max_durability(item) == 0 || d2d::rules::indestructible(game_data->rules, item) || item.durability > 0 || d2d::rules::broken(item))
                    continue;
                d2d::rules::wield_leaves(game_data->rules, actor->character.items, item);   // FUN_0055f850 -> FUN_0063d2b0
                item.flags |= 0x100u;
                settle_requirements(*game_data, actor->character);
                cues.cue(d2d::rules::sound_ids::kCursorDurabilityBreak, now_ms, actor->player.x, actor->player.y);
            }
            actor->attack_mon = -1;
            return false;
        }
        owe(*skill_row, lvl);
        actor->swing_skill = skill;
        actor->cast_x = target_x; actor->cast_y = target_y;
        actor->player.dir = direction16(target_x - actor->player.x, target_y - actor->player.y);
        set_pmode(skill_row->anim == "A1" || skill_row->anim == "TH" ? kModeA1 : kModeSC, now_ms);
        actor->pstruck = false;
        return true;
    }

auto Fight::pierce_chance(bool for_merc) const -> int {
        using namespace d2d::d2s;
        if (!for_merc) return int(actor->psum[kSkillPierce] + actor->psum[kItemPierce]);
        const auto gear = merc_gear();
        return int(gear.sum[kSkillPierce] + gear.sum[kItemPierce]);
    }

auto Fight::pierce_armed(bool for_merc) const -> bool {
        using namespace d2d::d2s;
        if (!for_merc) return actor->psum[kSkillPierce] != 0 || actor->psum[kItemPierce] != 0;
        const auto gear = merc_gear();
        return gear.sum[kSkillPierce] != 0 || gear.sum[kItemPierce] != 0;
    }

// FUN_0059f940: a Pierce row's charges from the owner's skill_pierce + item_pierce,
// seeded by its stat 0x148. Chance 0 sets nothing (the count stays 0).
auto Fight::seed_pierce(Missile& missile, bool for_merc) -> void {
        if (!missile.info->pierce) return;
        const int chance = pierce_chance(for_merc);
        if (chance == 0) return;
        missile.pierce = d2d::rules::pierce_charges(chance, for_merc ? actor->merc_pierce_idx : actor->pierce_idx);
    }

auto Fight::launch(const GameData::MissileInfo& missile_info, const d2d::rules::Skill& skill, int lvl, float x, float y, float dx, float dy,
                    int range, std::uint32_t now_ms) -> Missile& {
        const float speed = missile_speed(missile_info, lvl), distance = std::max(std::hypot(dx, dy), 0.01f);
        Missile missile{ &missile_info, x, y, dx / distance * speed, dy / distance * speed, missile_facing(x, y, dx, dy), now_ms, now_ms + std::uint32_t(std::max(range, 1)) * 40, {} };
        missile.friendly = true; missile.caster = actor->id; missile.skill = skill.id; missile.level = lvl; missile.by_merc = casting_merc; missile.striker = casting_unit;
        seed_pierce(missile, casting_merc);
        pending.push_back(std::move(missile));
        return pending.back();
    }

auto Fight::calc(const d2d::rules::Skill& skill, const d2d::rules::Calc& calc_row, int lvl) -> int {
        return d2d::rules::eval_calc(game_data->skills, calc_row, calc_env(), skill.id, lvl);
    }

auto Fight::fire(const d2d::rules::Skill& skill, std::uint32_t now_ms, const Caster& caster) -> void {
        const auto& missile_info = *skill_missile(skill);
        const int lvl = caster.level;
        casting_merc = caster.merc; casting_unit = caster.unit_id;
        struct Reset { bool& flag; int& unit; ~Reset() { flag = false; unit = -1; } } reset{ casting_merc, casting_unit };
        const int calc1 = calc(skill, skill.calc[0], lvl);
        const auto shared = std::make_shared<std::vector<int>>();
        const float dx = caster.to_x - caster.x, dy = caster.to_y - caster.y;
        const int range = missile_info.range + missile_info.lev_range * lvl;
        const int target = caster.target >= 0 && monsters[std::size_t(caster.target)].alive() ? caster.target : -1;
        auto send = [&](float ddx, float ddy, int reach) -> Missile& {
            auto& missile = launch(missile_info, skill, lvl, caster.x, caster.y, ddx, ddy, reach, now_ms);
            missile.target_x = caster.to_x; missile.target_y = caster.to_y;                    // where it was sent (Molten Boulder's roll)
            missile.to = { caster.x + ddx, caster.y + ddy };                         // its flight's aim (FUN_0059fa30)
            return missile;
        };
        if (skill.srvdofunc == ServerDoFunction::kMissileFan) {
            const int count = std::max(calc1, 1);
            float side_x = dy * 5, side_y = -dx * 5;                 // subtiles, turned a right angle
            while (side_x * side_x + side_y * side_y > 3) { side_x = std::trunc(side_x / 2); side_y = std::trunc(side_y / 2); }
            if (side_x == 0 && side_y == 0) side_x = 1;
            for (int k = 0; k < count; ++k) {
                const float spread = float(k) - float(count) / 2;
                send(dx + spread * side_x / 5, dy + spread * side_y / 5, range);
            }
        } else if (skill.srvdofunc == ServerDoFunction::kScatteredBolts) {
            for (int k = 0; k < std::max(calc1, 1); ++k) send(dx, dy, range).bolt = k;   // each wandering (FUN_005c9290)
        } else if (skill.srvdofunc == ServerDoFunction::kNova || skill.srvdofunc == ServerDoFunction::kWarCry) {
            // A war cry (do 68, FUN_005d83e0): the nova of its missile (range
            // its row's), then its state on the caster (FUN_005d8290).
            for (int k = 0; k < 64; ++k) {
                const auto [aim_x, aim_y] = d2d::rules::kNovaRing[std::size_t(k)];
                auto& shot = send(float(aim_x), float(aim_y), skill.srvdofunc == ServerDoFunction::kWarCry ? missile_info.range : missile_info.range + calc1);
                shot.struck = shared; shot.to.reset();                              // a way, not a point
            }
            if (skill.srvdofunc == ServerDoFunction::kWarCry && !skill.aurastate.empty()) {
                start_state(skill, lvl, now_ms);
                // The nova reaching an ally (hit 18, FUN_005ab0b0: FUN_00554de0)
                // gives it the state too, the caster's auralen and level
                // (FUN_005d8290, aurastats FUN_005c6cc0): the merc, the pets.
                // ponytail: an ally within the missile's reach (speed x Range)
                // when it's cast, not as the nova passes.
                const float reach = missile_speed(missile_info, lvl) * float(missile_info.range) * 0.04f;
                const int len = calc(skill, skill.auralen, lvl);
                const std::uint32_t until = len > 0 ? now_ms + std::uint32_t(len) * 40 : ~0u;
                if (actor->merc && actor->merc->mode != "DT" && std::hypot(actor->merc->unit.x - actor->player.x, actor->merc->unit.y - actor->player.y) <= reach) {
                    const int before = merc_max_life();
                    actor->merc_cry = { skill.id, lvl, until };
                    actor->merc->hit_points += std::max(merc_max_life() - before, 0);
                }
                for (auto& pet : actor->pets)
                    if (pet.monster.alive() && std::hypot(pet.monster.unit.x - actor->player.x, pet.monster.unit.y - actor->player.y) <= reach) {
                        std::erase_if(pet.buffs, [&](const SelfState& buff) { return buff.skill == skill.id; });
                        pet.buffs.push_back({ skill.id, lvl, until });
                        pet_buffs(pet);
                    }
            }
        } else if (skill.srvdofunc == ServerDoFunction::kGuidedMissile) {
            auto& missile = send(dx, dy, range);
            missile.target = target; missile.ed_pct = calc1;
        } else if (skill.srvdofunc == ServerDoFunction::kStrafe) {
            const int radius = calc(skill, skill.aurarange, lvl);
            std::vector<int> nearby;
            if (target >= 0) nearby.push_back(target);
            for (std::size_t i = 0; i < monsters.size(); ++i)
                if (int(i) != target && monsters[i].targetable()
                    && std::hypot(monsters[i].unit.x - caster.x, monsters[i].unit.y - caster.y) * 5 <= float(radius)) nearby.push_back(int(i));
            const int high = calc1, low = std::min(calc(skill, skill.calc[2], lvl), high);
            const int count = std::clamp(int(nearby.size()), low, high);
            for (int k = 0; k < count; ++k)
                actor->strafe.push_back({ nearby.empty() ? -1 : nearby[std::size_t(k) % nearby.size()], now_ms + std::uint32_t(k) * 120 });
        } else if (skill.srvdofunc == ServerDoFunction::kChainLightning) {
            send(dx, dy, range).hops = calc1;
        } else if (skill.srvdofunc == ServerDoFunction::kMissileAtTarget) {
            launch(missile_info, skill, lvl, caster.to_x, caster.to_y, 0, 0, range, now_ms);
        } else if (skill.srvdofunc == ServerDoFunction::kMissileWall) {
            const float around_x = target >= 0 ? monsters[std::size_t(target)].unit.x : caster.to_x;
            const float around_y = target >= 0 ? monsters[std::size_t(target)].unit.y : caster.to_y;
            const float perp_x = around_y - caster.y, perp_y = caster.x - around_x;
            launch(missile_info, skill, lvl, around_x, around_y, perp_x, perp_y, range, now_ms);
            launch(missile_info, skill, lvl, around_x, around_y, -perp_x, -perp_y, range, now_ms);
            if (const auto found = game_data->missiles.find(skill.srvmissileb); found != game_data->missiles.end())
                launch(found->second, skill, lvl, around_x, around_y, 0, 0, found->second.range + found->second.lev_range * lvl, now_ms);
        } else if (skill.srvdofunc == ServerDoFunction::kBreath || skill.srvdofunc == ServerDoFunction::kBladeFury) {
            if (caster.merc || caster.pet) send(dx, dy, range);            // one a cast: the channel is the player's (missile_tick)
            else {
                actor->channel = skill.id;
                if (skill.srvdofunc == ServerDoFunction::kBreath) actor->burning = now_ms + 20 * 40;   // state inferno (FUN_005c8e30)
            }
        } else if (skill.srvdofunc == ServerDoFunction::kBlessedHammer) {                      // Blessed Hammer (FUN_005d0040): path 14, a spiral out
            auto& missile = send(dx, dy, range);
            missile.target_x = caster.x; missile.target_y = caster.y;
            missile.turn = int(std::atan2(dy, dx) * 1000);
            // FUN_006461d0: the caster's state 0x2a (Concentration), its damagepercent x Param1 / 8. ponytail: the state is
            // taken from the first friendly aura of that name over the caster; one set by two sources isn't merged.
            if (!caster.merc && !caster.pet)
                for (const auto& aura : friendly_auras(caster.x, caster.y)) {
                    if (aura.skill->aurastate != "concentration") continue;
                    for (std::size_t i = 0; i < aura.skill->aurastat.size(); ++i)
                        if (aura.skill->aurastat[i] == d2d::d2s::kDamagePercent)
                            missile.influence = d2d::rules::aura_influence(d2d::rules::eval_calc(game_data->skills, aura.skill->aura_calc[i], aura.env, aura.skill->id, aura.level), skill.par[0]);
                    break;
                }
        } else if (skill.srvdofunc == ServerDoFunction::kFistOfTheHeavens) {                      // Fist of the Heavens (FUN_005d0670): on the target
            if (target < 0) return;
            const auto& target_monster = monsters[std::size_t(target)];
            launch(missile_info, skill, lvl, target_monster.unit.x, target_monster.unit.y, 0, 0, range, now_ms).target = target;
        } else if (skill.srvdofunc == ServerDoFunction::kFirestorm || skill.srvdofunc == ServerDoFunction::kWindMissile) {   // Firestorm (FUN_005c7160), Twister / Tornado (FUN_005c72f0)
            for (int k = 0; k < std::max(calc1, 1); ++k) {
                const float angle = k == 0 ? 0.f : (float(rng(41)) - 20) * 3.14159265f / 180;
                send(dx * std::cos(angle) - dy * std::sin(angle), dx * std::sin(angle) + dy * std::cos(angle), range);
            }
        } else if (skill.srvdofunc == ServerDoFunction::kBladeSentinel) {                      // Blade Sentinel: out to the point and back, Param1 frames
            // ponytail: do 44 (FUN_005d6020) moves a trap monster between
            // the caster and the point; here its missile row does, hitting
            // as it passes (NextHit).
            auto& missile = send(dx, dy, std::max(range, skill.par[0]));
            missile.origin_x = caster.x; missile.origin_y = caster.y;
            const float distance = std::max(std::hypot(dx, dy), 0.01f), speed = cells_per_sec(10.f);   // bladecreeper's Velocity
            missile.velocity_x = dx / distance * speed; missile.velocity_y = dy / distance * speed;
        } else if (skill.srvdofunc == ServerDoFunction::kGrimWard) {                      // Grim Ward: its start row at the point
            launch(missile_info, skill, lvl, caster.to_x, caster.to_y, 0, 0, missile_info.range, now_ms);
        } else if (skill.srvdofunc == ServerDoFunction::kVolcano) {                     // Volcano (FUN_005c8080): at the target point
            launch(missile_info, skill, lvl, caster.to_x, caster.to_y, 0, 0, range, now_ms);
        } else if (skill.srvdofunc == ServerDoFunction::kShockField) {                      // Shock Web (FUN_005d5d70 -> FUN_005d5bf0): scattered round the target
            const int count = std::max(calc(skill, skill.prgcalc[0], lvl), 1), radius = std::max(calc(skill, skill.aurarange, lvl), 1);
            for (int k = 0; k < count; ++k) {
                const float target_x = caster.to_x + float(int(rng(2 * radius + 1)) - radius) / 5, target_y = caster.to_y + float(int(rng(2 * radius + 1)) - radius) / 5;
                send(target_x - caster.x, target_y - caster.y, land_range(missile_info, target_x - caster.x, target_y - caster.y));
            }
        } else {
            send(dx, dy, missile_info.hit_func == 36 ? land_range(missile_info, dx, dy) : range);
        }
    }

auto Fight::land_range(const GameData::MissileInfo& missile_info, float dx, float dy) -> int {
        const float per_frame = missile_speed(std::max(d2d::rules::missile_velocity(missile_info.vel, missile_info.vel_lev, 1), 1)) * 0.04f;
        return std::max(int(std::hypot(dx, dy) / per_frame), 1);
    }

auto Fight::player_shots(std::uint32_t now_ms) -> void {
        const int frame = int(now_ms / 40);
        if (frame == actor->last_frame) return;
        actor->last_frame = frame;
        std::erase_if(actor->strafe, [&](const std::pair<int, std::uint32_t>& strafed) {
            if (now_ms < strafed.second) return false;
            const auto* skill = game_data->skills.get(actor->swing_skill);
            if (!skill || skill->srvdofunc != ServerDoFunction::kStrafe) return true;
            const auto& missile_info = *skill_missile(*skill);
            const int lvl = skill_level ? skill_level(skill->id) : 1;
            float target_x = actor->cast_x, target_y = actor->cast_y;
            if (strafed.first >= 0 && monsters[std::size_t(strafed.first)].alive()) { target_x = monsters[std::size_t(strafed.first)].unit.x; target_y = monsters[std::size_t(strafed.first)].unit.y; }
            launch(missile_info, *skill, lvl, actor->player.x, actor->player.y, target_x - actor->player.x, target_y - actor->player.y, missile_info.range + missile_info.lev_range * lvl, now_ms).ed_pct
                = calc(*skill, skill->calc[1], lvl);
            return true;
        });
        if (actor->channel >= 0) {
            const auto* skill = game_data->skills.get(actor->channel);
            if (!skill || actor->pmode != kModeSC || actor->swing_skill != actor->channel) actor->channel = -1;
            else {
                const int lvl = skill_level ? skill_level(skill->id) : 1;
                const auto& missile_info = *skill_missile(*skill);
                if (skill->srvdofunc == ServerDoFunction::kBreath) {
                    actor->burning = std::max(actor->burning, now_ms + 6 * 40);   // FUN_005c8e30: each flame keeps it 6 frames on
                    launch(missile_info, *skill, lvl, actor->player.x, actor->player.y, actor->cast_x - actor->player.x, actor->cast_y - actor->player.y, std::max(calc(*skill, skill->calc[0], lvl), 1), now_ms);
                } else if (frame % 3 == 0)                     // Blade Fury (FUN_005d68a0): one per attack frame
                    launch(missile_info, *skill, lvl, actor->player.x, actor->player.y, actor->cast_x - actor->player.x, actor->cast_y - actor->player.y, missile_info.range, now_ms);
            }
        }
    }

auto Fight::missile_tick(std::uint32_t now_ms) -> void {
        const int frame = int(now_ms / 40);
        if (frame == missile_frame) return;
        missile_frame = frame;
        burned.clear();
        for (std::size_t k = 0; k < missiles.size(); ++k) {
            auto& missile = missiles[k];
            if (!missile.friendly || missile.skill < 0 || !missile.info) continue;   // the player's side's (a monster's carries its skill too)
            if (Hero* caster = hero_of(missile.caster)) act_as(*caster);              // its caster's skills, credit and leech
            merc_blow = casting_merc = missile.by_merc;                              // a merc's burst kills and fires as the merc's
            const auto* skill = game_data->skills.get(missile.skill);
            if (!skill) continue;
            const int age = int(now_ms - missile.born) / 40;
            const auto& missile_info = *missile.info;
            if (missile_info.srv_do == 7 && age % std::max(missile_info.param1, 1) == 0) {
                if (missile.target < 0 && skill->srvstfunc == ServerStartFunction::kNone) {
                    float best = 1e9f;
                    for (std::size_t i = 0; i < monsters.size(); ++i)
                        if (const float distance = std::hypot(monsters[i].unit.x - actor->cast_x, monsters[i].unit.y - actor->cast_y); monsters[i].targetable() && distance < best) {
                            best = distance; missile.target = int(i);
                        }
                }
                if (missile.target >= 0 && monsters[std::size_t(missile.target)].alive()) {
                    const auto& target = monsters[std::size_t(missile.target)];
                    const float speed = std::hypot(missile.velocity_x, missile.velocity_y), dx = target.unit.x - missile.x, dy = target.unit.y - missile.y, distance = std::max(std::hypot(dx, dy), 0.01f);
                    missile.velocity_x = dx / distance * speed; missile.velocity_y = dy / distance * speed; missile.dir = missile_facing(missile.x, missile.y, dx, dy);
                }
            }
            const auto sub = missile_info.sub.empty() ? game_data->missiles.end() : game_data->missiles.find(missile_info.sub);
            if (missile_info.srv_do == 6 && sub != game_data->missiles.end())
                launch(sub->second, *skill, missile.level, missile.x, missile.y, 0, 0, sub->second.range + sub->second.lev_range * missile.level, now_ms);
            if ((missile_info.srv_do == 10 || missile_info.srv_do == 25) && sub != game_data->missiles.end()) {
                const int every = std::max(calc(*skill, skill->calc[1], missile.level), 1), radius = std::max(calc(*skill, skill->calc[0], missile.level), 1);
                if (age % every == 0) {
                    // FUN_005a9820: rand(2r) - r with r = radius - 1 on each axis, x first: -r..r-1 subtiles.
                    const int reach = radius - 1;
                    const float offset_x = float(int(rng(2 * reach)) - reach) / 5, offset_y = float(int(rng(2 * reach)) - reach) / 5;
                    launch(sub->second, *skill, missile.level, missile.x + offset_x, missile.y + offset_y, 0, 0, sub->second.range, now_ms);
                }
            }
            if (missile_info.srv_do == 5) {
                // A row with no Skill (Meteor's, Immolation Arrow's fire) burns its own element with its synergy.
                const auto damage = missile.fixed >= 0 ? d2d::rules::MissileDamage{ .etype = 0, .elo = missile.fixed, .ehi = missile.fixed }
                                  : missile_info.skill.empty() ? row_damage(missile_info, missile.level, calc_env(), missile.skill)
                                                               : d2d::rules::missile_damage(game_data->skills, *skill, calc_env(), missile.level);
                for (std::size_t i = 0; i < monsters.size(); ++i)
                    if (monsters[i].targetable() && standing_struck(missile, monsters[i].unit.x, monsters[i].unit.y, game_data->monsters.types[std::size_t(monsters[i].type)].size)
                        && !std::ranges::contains(burned, std::pair{ int(i), missile.skill })) {
                        burned.emplace_back(int(i), missile.skill);
                        burn(i, damage, now_ms, soft_hit(missile_info, rng));
                    }
            }
            // 13 (FUN_005aeda0, Bone Wall's maker): while it has pieces left and the first piece
            // (FUN_00552f60 on its id) stands, one at its own subtile on a frame it entered a new one
            // (path +0x34 & 8), at the skill's level; its flight stops at a wall (CollideType 8).
            if (missile_info.srv_do == 13 && missile.hops > 0) {
                const int at_x = int(std::floor(missile.x * 5)), at_y = int(std::floor(missile.y * 5));
                const bool first_stands = std::ranges::any_of(actor->pets, [&](const auto& pet) { return pet.monster.id == missile.target_id && pet.monster.alive(); });
                if ((at_x != missile.subtile_x || at_y != missile.subtile_y) && first_stands) {
                    missile.subtile_x = at_x; missile.subtile_y = at_y;
                    if (const int type = game_data->monsters.row(skill->summon); type >= 0) {
                        summon_one(*skill, type, missile.level, calc_env(), (float(at_x) + 0.5f) / 5, (float(at_y) + 0.5f) / 5, now_ms);
                        --missile.hops;
                    }
                }
            }
            // The Range runs out: the hit function goes off with no unit.
            if (now_ms + 40 >= missile.dies && missile.frame < 0 && std::ranges::contains(kNoUnitHit, missile_info.hit_func)) burst(missile, now_ms);
            // 15 (FUN_005af030, Frozen Orb): every Param1 frames SubMissile1
            // toward direction `turn` of 64 (0x6e2b78 / 0x6e2a78), turning
            // Param2 on.
            if (missile_info.srv_do == 15 && sub != game_data->missiles.end() && age % std::max(missile_info.param1, 1) == 0) {
                const auto [aim_x, aim_y] = d2d::rules::kNovaRing[std::size_t(missile.turn & 63)];
                launch(sub->second, *skill, missile.level, missile.x, missile.y, float(aim_x), float(aim_y), sub->second.range, now_ms);
                missile.turn += missile_info.param2;
            }
            // 14 (Grim Ward's totem): every Param1 frames the monsters within
            // aurarange of it run for auralen frames.
            // 14 (FUN_005aef70, Grim Ward's totem): every Param1 frames skill
            // srvdofunc Param2 (FUN_0056d810: 30, the curse) at the missile's
            // skill and level, the missile its caster and its spot the point
            // (FUN_0056d2c0, no target unit): Terror round it, aurarangecalc
            // wide, auralencalc / AiCurseDivisor long, the AI swap with it.
            if (missile_info.srv_do == 14 && age % std::max(missile_info.param1, 1) == 0 && missile_info.param2 == int(ServerDoFunction::kCurse)) {
                auto curse = *skill;
                curse.srvdofunc = ServerDoFunction::kCurse;
                spot(curse, now_ms, Caster{ .x = missile.x, .y = missile.y, .level = missile.level, .to_x = missile.x, .to_y = missile.y });
            }
            // 20 (Blade Sentinel): back and forth between the caster's spot and
            // where it was sent.
            if (missile_info.srv_do == 20 && std::hypot(missile.target_x - missile.x, missile.target_y - missile.y) < 0.3f) {
                std::swap(missile.target_x, missile.origin_x); std::swap(missile.target_y, missile.origin_y);
                const float speed = std::hypot(missile.velocity_x, missile.velocity_y), dx = missile.target_x - missile.x, dy = missile.target_y - missile.y, distance = std::max(std::hypot(dx, dy), 0.01f);
                missile.velocity_x = dx / distance * speed; missile.velocity_y = dy / distance * speed; missile.dir = missile_facing(missile.x, missile.y, dx, dy);
                missile.last_hit = -1;
            }
            // 31 (Wake of Fire's maker): its fire where it goes.
            if (missile_info.srv_do == 31 && sub != game_data->missiles.end())
                launch(sub->second, *skill, missile.level, missile.x, missile.y, missile.velocity_x, missile.velocity_y, sub->second.range, now_ms);
            // 23 (FUN_005af790, Firestorm): SubMissile1 where it is, each frame.
            if (missile_info.srv_do == 23 && sub != game_data->missiles.end())
                launch(sub->second, *skill, missile.level, missile.x, missile.y, 0, 0, sub->second.range, now_ms);
            // 27 (FUN_005afa30, Tornado): every Param1 (else calc4) frames its
            // damage within Param2 (else aurarange) subtiles.
            if (missile_info.srv_do == 27 && age % std::max(missile_info.param1 > 0 ? missile_info.param1 : calc(*skill, skill->calc[3], missile.level), 1) == 0)
                area(missile, missile.x, missile.y, missile_info.param2 > 0 ? missile_info.param2 : calc(*skill, skill->aurarange, missile.level), now_ms);
            // 28 (FUN_005afb80, Volcano): every Param1 (else calc4) frames
            // SubMissile1 thrown at a point within Param2 (else aurarange),
            // between frames Param3 and Param4 (both out) of its age: FUN_0064a3b0 is its
            // range (+0xe) less the frames left (+0x10, ticked down by FUN_005ae1f0).
            if (missile_info.srv_do == 28 && sub != game_data->missiles.end() && age > missile_info.param3 && age < missile_info.param4
                && age % std::max(missile_info.param1 > 0 ? missile_info.param1 : calc(*skill, skill->calc[3], missile.level), 1) == 0) {
                const int radius = std::max(missile_info.param2 > 0 ? missile_info.param2 : calc(*skill, skill->aurarange, missile.level), 1);
                const float offset_x = float(int(rng(2 * radius + 1)) - radius) / 5, offset_y = float(int(rng(2 * radius + 1)) - radius) / 5;
                launch(sub->second, *skill, missile.level, missile.x, missile.y, offset_x, offset_y, land_range(sub->second, offset_x, offset_y), now_ms);
            }
            // Blessed Hammer (do 73's path 14): round its caster's spot and out.
            // ponytail: the client path's shape isn't traced: a turn each 1.6
            // s, out at a fifth of its speed.
            if (skill->srvdofunc == ServerDoFunction::kBlessedHammer && missile_info.srv_do <= 1) {
                const float seconds = float(now_ms - missile.born) / 1000, radius = seconds * missile_speed(missile_info, missile.level) * 0.2f;
                const float ang = float(missile.turn) / 1000 + seconds * 2 * 3.14159265f / 1.6f;
                missile.x = missile.target_x + radius * std::cos(ang); missile.y = missile.target_y + radius * std::sin(ang);
                missile.velocity_x = missile.velocity_y = 0;
            }
        }
        merc_blow = casting_merc = false;
    }

auto Fight::burn(std::size_t monster_index, const d2d::rules::MissileDamage& damage, std::uint32_t now_ms, bool soft) -> void {
        static constexpr int kRes[5] = { 2, 3, 4, 5, 1 };
        if (damage.etype < 0 || damage.etype > 4 || damage.ehi <= 0) return;
        auto& monster = monsters[monster_index];
        const auto target = target_of(monster_index);
        int res = target.res[std::size_t(kRes[damage.etype])];
        if (damage.etype < 4 && res < 100) res = std::max(res - pierce()[std::size_t(damage.etype)], -100);
        monster.dot_acc += double(d2d::rules::resisted(rng.range(damage.elo, damage.ehi), res)) / 256;
        if (soft) { monster.left_mode = "hit"; monster.soft_hit = monster.soft_hit || monster.mode != "GH"; }   // event 4 tests mode 3 at the hit
        const int whole = int(monster.dot_acc);
        if (whole <= 0) return;
        monster.dot_acc -= whole;
        monster.hit_points -= whole;
        monster.last_hp -= whole;                                       // a burn's own drop sets off no event 4
        monster.aware = true;
        if (!monster.alive()) { set_mode(*game_data, monster, "DT", now_ms); killed(monster_index, now_ms); }
    }

auto Fight::pierce() const -> std::array<int, 4> { return { int(actor->psum[d2d::d2s::kPassiveFirePierce]), int(actor->psum[d2d::d2s::kPassiveLightningPierce]), int(actor->psum[d2d::d2s::kPassiveColdPierce]), int(actor->psum[d2d::d2s::kPassivePoisonPierce]) }; }

auto Fight::skill_missile_hits(Missile& missile, std::size_t monster_index, std::uint32_t now_ms) -> bool {
        const auto* skill = game_data->skills.get(missile.skill);
        if (!skill || !missile.info) return true;
        const auto& missile_info = *missile.info;
        if (missile_info.srv_do == 5) return false;                    // a burner: missile_tick's
        // Hit function 10 (FUN_005aa650): a guided one passes by all but its target.
        if (missile_info.hit_func == 10 && missile.target >= 0 && missile.target != int(monster_index) && monsters[std::size_t(missile.target)].alive()) return false;
        // 36 (FUN_005abf70: Fire Blast, Shock Web in the air), 14 / 22 / 29 /
        // 48 (Meteor, Fist of the Heavens, Frozen Orb, Molten Boulder rising)
        // pass over units and act where they come down.
        if (missile_info.hit_func == 36 || missile_info.hit_func == 14 || missile_info.hit_func == 22 || missile_info.hit_func == 29 || missile_info.hit_func == 48) return false;
        // 7 (FUN_005a9fb0: Holy Bolt, the Fist's bolts): sHitPar1 (+0x4c) non-zero
        // and an owner, then TargetPet (flag bit 0x19, FUN_005542c0) or
        // TargetAlly (bit 0x1a, FUN_00554d20). TargetPet is a monster whose
        // owner is that player: here, one of the caster's pets. TargetAlly
        // needs the same party (FUN_00540710 is -1 with no party, and that
        // fails), so a solo caster is not healed. The amount is calc1 whole
        // points (skill +0x138) in 256ths plus a draw below (calc2 - calc1)
        // x 256 (FUN_00646ca0, FUN_0045c3e0), capped at max life. At 0 or
        // less, FUN_005a8910 of missile stats 0x16 and 0x15; those stats are
        // not on the missile, so that roll is 0. Returns 1: it ends and does
        // not strike. ponytail: the draw is the fight rng. The missile seed
        // (+0x20) and ProgOverlay (+0x36) are not kept.
        if (missile_info.hit_func == 7) {
            const bool players_bolt = missile.friendly && !missile.by_merc;
            const int struck_id = monsters[monster_index].id;
            const bool owned_pet = players_bolt && struck_id >= 0 && skill->target_pet && std::ranges::any_of(actor->pets, [&](const Pet& pet) {
                return pet.monster.alive() && pet.monster.id == struck_id;
            });
            if (missile_info.hit_par1 != 0 && owned_pet) {
                const int low_points = calc(*skill, skill->calc[0], missile.level);
                const int high_points = calc(*skill, skill->calc[1], missile.level);
                const int span = (high_points << 8) - (low_points << 8);
                const int amount = (span > 0 ? int(rng(span)) : 0) + (low_points << 8);
                const auto mend = [](Monster& monster, int added) {
                    if (added <= 0) return;
                    int life = monster.life_fine() + added;
                    const int cap = monster.max_life_fine();
                    if (life > cap) life = cap;
                    monster.set_life_fine(life);
                };
                if (amount > 0) {
                    mend(monsters[monster_index], amount);
                    for (auto& pet : actor->pets)
                        if (pet.monster.id == struck_id && &pet.monster != &monsters[monster_index]) mend(pet.monster, amount);
                }
                return true;
            }
            const auto& type_info = game_data->monsters.types[std::size_t(monsters[monster_index].type)];
            if ((missile_info.hit_par2 == 1 && !type_info.undead) || (missile_info.hit_par2 == 2 && !type_info.demon)) return false;
        }
        // 18 (FUN_005ab0b0: Shout, Battle Orders, Battle Command) is for
        // allies; 21 (FUN_005ab500, Battle Cry) puts its auratargetstate on
        // the monster for auralen frames; 17 (FUN_005aafb0, Howl) sends one
        // below the caster's level + Param2 (FUN_004efc20: skill +0x14c) +
        // the skill level running for Param5 + Param6 x (level - 1) frames
        // (FUN_004efcb0: the state block's frames; bugs.md 41); neither hurts.
        // Howl's run (FUN_005ddd00): on a monster swaps_ai lets, alive,
        // Terror's state (0x38, skill 77 level 1) for those frames with no
        // AI toggle at its end (FUN_0056e900), special AI 11 (FUN_005b0e00)
        // and a think next frame; Terror's think toggles back once it's
        // gone.
        if (missile_info.hit_func == 18) return false;
        if (missile_info.hit_func == 21) {
            if (monsters[monster_index].cry.skill != skill->id || now_ms >= monsters[monster_index].cry.until)
                put_state(*game_data, monsters[monster_index], monsters[monster_index].cry,
                          { skill->id, missile.level, now_ms + std::uint32_t(std::max(calc(*skill, skill->auralen, missile.level), 1)) * 40 }, now_ms);
            return false;
        }
        if (missile_info.hit_func == 17) {
            const int clvl = int(actor->character.stats.get(d2d::d2s::kLevel));
            if (auto& howled = monsters[monster_index]; howled.stats.level < clvl + skill->par[1] + missile.level && howled.alive() && swaps_ai(*game_data, howled)) {
                put_state(*game_data, howled, howled.curse, { kTerror, 1, now_ms + std::uint32_t(std::max(skill->par[4] + (std::max(missile.level, 1) - 1) * skill->par[5], 1)) * 40 }, now_ms);
                ai_toggle(*game_data, howled, 11);
                howled.next_act = now_ms + 40;
            }
            missile.struck->push_back(int(monster_index));
            return false;
        }
        if (missile_info.hit_func == 26) return false;                 // Grim Ward's start: it lands (burst)
        // A NextHit row's hit puts state 0x56 (justhit) on the monster
        // (FUN_005adb00), and every NextHit missile, of any cast, passes a
        // monster in it by (FUN_005a8730 / FUN_005adf10); a LastCollide row also
        // the monster it struck last (FUN_0064a470). Justhit ends after the
        // missiles of frame f + NextDelay: FUN_005414d0 runs the missile
        // timers (group 2) before the monsters' (group 1), whose expiry is
        // FUN_005a7f80 -> FUN_00627460 -> 0x5adaf0. Struck at f, blocked
        // f+1..f+NextDelay, next hit at f+NextDelay+1, as game.exe's own
        // filter and hit decide it (tools/emu/next_hit.py, test_next_hit).
        if (missile_info.next_hit) {
            auto& struck_unit = monsters[monster_index];
            if (now_ms <= struck_unit.just_hit_until || (missile_info.last_collide && missile.last_hit == int(monster_index))) return false;
            struck_unit.just_hit_until = now_ms + std::uint32_t(std::max(missile_info.next_delay, 1)) * 40;
            missile.last_hit = int(monster_index);
            missile.last_hit_ms = now_ms;
        } else if (missile_info.last_unit_rule()) {
            // Not NextHit but LastCollide (Lightning): FUN_005a8730 passes every unit but the one the missile
            // struck last (FUN_0064a470), and FUN_005adf10 makes it the last (FUN_0064a400, 0x5adfae).
            // ponytail: a Pierce row (arrows, javelins, Blessed Hammer) and a srvdofunc 3 row (standing clouds) keep the struck list.
            if (missile.last_hit == int(monster_index)) return false;
            missile.last_hit = int(monster_index);
            missile.last_hit_ms = now_ms;
        }
        // 1 (FUN_005a9a70) and 13 (FUN_005aa8b0) go off here, the area taking in the unit.
        if (missile_info.hit_func == 1 || missile_info.hit_func == 13) { burst(missile, now_ms); return true; }
        strike(missile, monster_index, now_ms);
        if (!missile_info.next_hit && !missile_info.last_unit_rule()) missile.struck->push_back(int(monster_index));
        // Hit function 12 (FUN_005aa730, Chain Lightning): with hops left, on
        // from here at another monster within sHitPar1 (else aurarange)
        // subtiles, a hop fewer. FUN_0056bd10 (flags 0x8a783) over the units
        // in the rooms round the caster's: hostile, targetable, out of town,
        // within the radius of the missile's subtile (squared), the bare line
        // from the caster clear of mask 4 (0x200); the next unit id above the
        // struck one's, else the lowest id up to it: the struck one itself
        // then hops nowhere.
        if (missile_info.hit_func == 12 && missile.hops > 1) {
            const int radius = missile_info.hit_par1 > 0 ? missile_info.hit_par1 : calc(*skill, skill->aurarange, missile.level);
            auto sub = [](float cells) { return int(std::floor(cells * 5)); };
            const auto wall = [&](int at_x, int at_y) { return level->blocked((float(at_x) + 0.5f) / 5, (float(at_y) + 0.5f) / 5, 0x04); };
            const auto rooms = near_rooms(*game_data, *level, actor->player.x, actor->player.y);
            const bool town = d2d::rules::level_ids::is_town(level->id);
            std::vector<int> found;
            for (std::size_t j = 0; j < monsters.size() && !town; ++j) {
                const auto& other = monsters[j];
                if (!other.targetable() || other.align == 2 || !std::ranges::contains(rooms, std::pair{ level, room_of(*level, other.unit.x, other.unit.y) })) continue;
                const int dx = sub(other.unit.x) - sub(missile.x), dy = sub(other.unit.y) - sub(missile.y);
                if (dx * dx + dy * dy > radius * radius
                    || d2d::rules::sight_blocked(sub(actor->player.x), sub(actor->player.y), 0, sub(other.unit.x), sub(other.unit.y), 0, wall)) continue;
                found.push_back(int(j));
            }
            const int pick = d2d::rules::ring_pick(found, int(monster_index)), best = pick == int(monster_index) ? -1 : pick;
            if (best >= 0) {
                const auto& next_target = monsters[std::size_t(best)];
                launch(missile_info, *skill, missile.level, missile.x, missile.y, next_target.unit.x - missile.x, next_target.unit.y - missile.y, missile_info.range, now_ms).hops = missile.hops - 1;
            }
        }
        if (missile_info.hit_func == 2 || missile_info.hit_func == 4 || missile_info.hit_func == 9 || missile_info.hit_func == 20 || missile_info.hit_func == 47) {
            missile.target = int(monster_index);
            burst(missile, now_ms, int(monster_index));
            if (missile_info.hit_func == 47) return false;             // the boulder rolls on (FUN_005ac550 returns 1)
            return true;
        }
        // FUN_005adf10 -> FUN_005ada80: a charge left flies on, one fewer, while the owner has
        // skill_pierce or item_pierce; no draw. A merc's owner is the merc.
        if (missile_info.pierce && missile.pierce > 0 && pierce_armed(missile.by_merc)) {
            --missile.pierce;
            return false;
        }
        return missile_info.collide_kill;
    }

auto Fight::strike(const Missile& missile, std::size_t monster_index, std::uint32_t now_ms, int freeze, d2d::rules::MissileRoll* roll) -> void {
        const auto* skill = game_data->skills.get(missile.skill);
        if (!skill || !monsters[monster_index].alive()) return;
        const auto target = target_of(monster_index);
        // A merc's skill missile (Fire Arrow): its weapon, attack rating and
        // level; its skill's calcs see no synergies (mercs have none).
        const int clvl = missile.by_merc ? actor->merc_st.level : int(actor->character.stats.get(d2d::d2s::kLevel));
        const auto merc = missile.by_merc ? merc_fighter() : d2d::rules::Fighter{};
        const auto& fighter = missile.by_merc ? merc : actor->player_combat;
        const auto env = missile.by_merc ? merc_calc_env() : calc_env();
        auto damage = missile.fixed >= 0 ? d2d::rules::MissileDamage{ .etype = missile.info->etype < 0 ? 0 : missile.info->etype, .elo = missile.fixed, .ehi = missile.fixed }
                : missile.info->skill.empty() ? row_damage(*missile.info, missile.level, env, missile.skill)
                                        : d2d::rules::missile_damage(game_data->skills, *skill, env, missile.level);
        damage = d2d::rules::influenced(damage, missile.influence);
        if (missile.info->dmg_func == 5) {                  // FUN_005ad2f0: Blessed Hammer's bonus against the undead and the demons
            damage.undead_pct = missile.info->dmg_par1;
            damage.demon_pct = missile.info->dmg_par2;
        }
        d2d::rules::Blow blow{ .hit = true };
        if (damage.srcdam > 0) {
            d2d::rules::Swing swing;
            swing.srcdam = damage.srcdam;
            swing.ed_pct = missile.ed_pct;
            swing.ar_pct = missile.ar_pct;
            swing.missile = true;
            blow = d2d::rules::player_blow(fighter, target, clvl, rng, swing);
        } else if (missile.info->to_hit) {
            blow.hit = int(rng(100)) < d2d::rules::hit_chance(fighter.attack_rating, target.armor_class, clvl, target.level);
            blow.prevent_heal = blow.hit && fighter.prevent_heal;
        }
        if (blow.hit) blow = d2d::rules::missile_blow(damage, target, pierce(), rng, blow, roll);
        blow.hit_class = missile.info->hit_class | skill->hit_class;
        const auto frozen = freeze_on(monster_index, target, blow, freeze);
        land(monster_index, blow, true, now_ms, missile.striker >= 0 ? missile.striker : missile.by_merc ? merc_foe_id() : player_foe_id());
        if (missile.striker < 0) monster_return_fire(monster_index, missile, missile.by_merc, now_ms);
        stand(monster_index, frozen, now_ms);
    }

auto Fight::freeze_on(std::size_t monster_index, const d2d::rules::Target& target, d2d::rules::Blow& blow, int freeze) -> d2d::rules::Freeze {
        d2d::rules::Freeze frozen;
        // State 0x36 (uninterruptable) takes no freeze and no chill for it (FUN_0057b230's first test).
        if (const auto& struck = monsters[monster_index]; blow.hit && freeze > 0 && struck.type >= 0 && !struck.uninterruptable()) {
            const auto& struck_type = game_data->monsters.types[std::size_t(struck.type)];
            const bool special = struck_type.boss_column || (struck.boss != d2d::rules::Boss::none && struck.boss != d2d::rules::Boss::minion);
            frozen = d2d::rules::monster_freeze(freeze, target, pierce()[2], special, struck_type.diff[std::size_t(struck.difficulty)].cold_effect,
                                                game_data->freeze_divisor[std::size_t(struck.difficulty)]);
            blow.chill_ticks = std::max(blow.chill_ticks, frozen.chill_ticks);
        }
        return frozen;
    }

auto Fight::stand(std::size_t monster_index, const d2d::rules::Freeze& frozen, std::uint32_t now_ms) -> void {
        // A freeze stands it as a stun does, without the stun's guards. A
        // length over the divisor of 0 is state 1 for no frame: nothing shows.
        // MonsterFreezeDivisor 0 is game.exe's assert (0x254); 1.14d has none.
        // Its think event at the end + 1 (FUN_005417d0 2) is dropped by the
        // thaw (0x57b170), which files the end + aidel one (mon_think).
        if (frozen.freeze_ticks > 0) freeze(monsters[monster_index], now_ms + std::uint32_t(frozen.freeze_ticks) * 40);
    }

auto Fight::area(const Missile& missile, float x, float y, int radius, std::uint32_t now_ms, int freeze, bool one_roll) -> void {
        d2d::rules::MissileRoll roll;
        for (std::size_t j = 0; j < monsters.size(); ++j)
            if (monsters[j].targetable() && std::hypot(monsters[j].unit.x - x, monsters[j].unit.y - y) * 5 <= float(std::max(radius, 1))) strike(missile, j, now_ms, freeze, one_roll ? &roll : nullptr);
    }

auto Fight::burst(Missile& missile, std::uint32_t now_ms, int struck) -> void {
        const auto* skill = game_data->skills.get(missile.skill);
        if (!skill || missile.frame >= 0) return;
        missile.frame = int(now_ms / 40);
        const auto& missile_info = *missile.info;
        const auto hit_sub = missile_info.hit_sub.empty() ? game_data->missiles.end() : game_data->missiles.find(missile_info.hit_sub);
        const bool sub = hit_sub != game_data->missiles.end();
        // `frames` > 0: the sub-missile's Range set outright (create flag 0x8000, FUN_0059fa30).
        auto sub_at = [&](float x, float y, float dx, float dy, int frames = 0) -> Missile& {
            return launch(hit_sub->second, *skill, missile.level, x, y, dx, dy, frames > 0 ? frames : hit_sub->second.range + hit_sub->second.lev_range * missile.level, now_ms);
        };
        auto radius = [&](int par, const d2d::rules::Calc& radius_calc) { return par > 0 ? par : std::max(calc(*skill, radius_calc, missile.level), 1); };
        static constexpr int kSpreadX[18] = { 2, -2, 0, 0, -3, 0, 3, -1, 1, -1, 2, -4, -3, -1, 0, 1, 3, 4 };
        static constexpr int kSpreadY[18] = { -2, -2, 2, 5, 3, 3, 3, 2, 1, -1, -1, -2, -2, -3, -4, -3, -3, -2 };
        switch (missile_info.hit_func) {
            case 1: area(missile, missile.x, missile.y, radius(missile_info.hit_par1, skill->calc[0]), now_ms, 0, true); break;
            case 3: area(missile, missile.x, missile.y, radius(missile_info.hit_par1, skill->aurarange), now_ms); break;
            case 13: area(missile, missile.x, missile.y, radius(missile_info.hit_par1, skill->aurarange), now_ms, radius(missile_info.hit_par2, skill->auralen), true); break;
            case 4: case 36: case 51: if (sub) sub_at(missile.x, missile.y, 0, 0); break;
            case 26:
                // Grim Ward's start (FUN_005ab8d0): one sub-missile on the spot, flags 0x8001, its
                // Range sHitPar1, else calc1 at least 5 (tools/emu/hit_grim_ward.py).
                if (sub) sub_at(missile.x, missile.y, 0, 0, missile_info.hit_par1 > 0 ? missile_info.hit_par1 : std::max(calc(*skill, skill->calc[0], missile.level), 5));
                break;
            case 2: {
                // FUN_005a9d80 -> FUN_005a9370, from the unit struck (else the missile): HitSubMissile1 every
                // sHitPar2'th of the 16 ways (at least 1) at its Param1 << 7, then with sHitPar1 every
                // sHitPar1'th of ways 1..15 at its Param2 << 7 (flags 0x17: x 75 / 100). sHitPar3 (flag 8)
                // runs each one's Range on by (SubStop - SubStart) x it when it loops: Plague Javelin's 23
                // clouds, 87 frames each.
                if (!sub) break;
                const auto& ring_sub = hit_sub->second;
                const bool on_unit = struck >= 0 && std::size_t(struck) < monsters.size();
                const auto centre = [](float cells) { return (std::floor(cells * 5) + 0.5f) / 5; };
                const float from_x = centre(on_unit ? monsters[std::size_t(struck)].unit.x : missile.x);
                const float from_y = centre(on_unit ? monsters[std::size_t(struck)].unit.y : missile.y);
                const int loops = missile_info.hit_par3 > 0 && ring_sub.sub_loop ? (ring_sub.sub_stop - ring_sub.sub_start) * missile_info.hit_par3 : 0;
                const int frames = ring_sub.range + ring_sub.lev_range * missile.level + loops;
                const auto ring = [&](int first, int step, int velocity) {
                    for (int way = first; way < 16; way += step) {
                        const float dx = float(kRingWayX[std::size_t(way)]), dy = float(kRingWayY[std::size_t(way)]), span = std::hypot(dx, dy);
                        const float speed = missile_speed((velocity << 7) * 75 / 100);
                        auto& made = sub_at(from_x, from_y, dx, dy, frames);
                        made.velocity_x = dx / span * speed; made.velocity_y = dy / span * speed;
                    }
                };
                ring(0, std::max(missile_info.hit_par2, 1), ring_sub.param1);
                if (missile_info.hit_par1 > 0) ring(1, missile_info.hit_par1, ring_sub.param2);
                break;
            }
            case 9: {
                // Immolation Arrow (FUN_005aa250 -> FUN_005a9530): its fire lasts SHitCalc1 frames.
                const int burst_radius = radius(missile_info.hit_par1, skill->calc[0]);
                const int frames = missile_info.hit_calc.empty() ? 0 : calc(*skill, missile_info.hit_calc, missile.level);
                area(missile, missile.x, missile.y, burst_radius, now_ms);
                if (sub)
                    for (int offset_x = -burst_radius; offset_x <= burst_radius; ++offset_x)
                        for (int offset_y = -burst_radius; offset_y <= burst_radius; ++offset_y)
                            if (offset_x * offset_x + offset_y * offset_y <= burst_radius * burst_radius) sub_at(missile.x + float(offset_x) / 5, missile.y + float(offset_y) / 5, 0, 0, frames);
                break;
            }
            case 56: {
                // Armageddon's / Diablogeddon's (FUN_005acc50, table 0x73c840): the burst,
                // then one sub-missile on its spot (FUN_0056ede0), its row's length.
                area(missile, missile.x, missile.y, radius(missile_info.hit_par1, skill->aurarange), now_ms);
                if (sub) sub_at(missile.x, missile.y, 0, 0).fixed = hit_sub->second.emin > 0 ? hit_sub->second.emin : -1;
                break;
            }
            case 14: case 47: {
                const int burst = radius(missile_info.hit_par1, skill->aurarange);
                area(missile, missile.x, missile.y, burst, now_ms);
                // FUN_005aaa90 lays the fire every sHitPar2th spot. Meteor's (FUN_005aabb0) lasts
                // FUN_004cc7c0's Param3 + (lvl - 1) x Param4 frames; Molten Boulder's
                // (FUN_005ac550) is handed the burst's radius as its length (flag 0x8001).
                const int frames = missile_info.hit_func == 14 ? skill->par[2] + (missile.level - 1) * skill->par[3] : burst;
                if (sub)
                    for (std::size_t j = 0; j < 18; j += std::size_t(std::max(missile_info.hit_par2, 1))) {
                        auto& fire = sub_at(missile.x + float(kSpreadX[j]) / 5, missile.y + float(kSpreadY[j]) / 5, 0, 0, frames);
                        if (missile_info.hit_func != 14) fire.fixed = hit_sub->second.emin > 0 ? hit_sub->second.emin : -1;
                    }
                break;
            }
            case 20: {
                if (!sub) break;
                // FUN_00552fd0: the missile's owner (its +0xc8 bit 0x400, then FUN_00552f60 on its +0x94
                // type and +0x98 id); with none FUN_005ab370 returns 1 and nothing is sent.
                const Hero* owner = hero_of(missile.caster);
                if (!owner) break;
                const UnitState& from = missile.by_merc && owner->merc ? owner->merc->unit : owner->player;
                const int burst_radius = radius(missile_info.hit_par1, skill->aurarange);
                int count = radius(missile_info.hit_par2, skill->calc[0]);
                // FUN_005ab370 -> FUN_0056b7e0 -> FUN_005ab2a0: the walk is handed the owner, so it skips only
                // the owner and the unit struck gets one too, and FUN_0056b3e0's line (0x200, in the default
                // 0xa783) runs from the owner, the reach from the missile; its filter is the aurafilter, else
                // 0xa783 (tools/emu/hit_fury.py). Handed 1 as here, the walk passes a monster with MonStats
                // noaura (flag 0x1b: 0x56b979).
                // Its rooms are the owner's room and that room's near list (FUN_00620bb0 of the owner at
                // 0x56b815, FUN_00619790), in the list's order, each room's units newest first (room +0x74,
                // next +0xe8): a monster in reach of a bolt thrown far, but in no room near the thrower's,
                // gets none. FUN_0056b770's per-room reach test lets every room by (bugs.md).
                std::vector<int> rooms;
                std::vector<d2d::rules::RoomUnit> units;
                if (level) {
                    for (const auto& [room_level, near_room] : near_rooms(*game_data, *level, from.x, from.y))
                        if (room_level == level) rooms.push_back(near_room);
                    units.reserve(monsters.size());
                    for (const auto& monster : monsters) units.push_back({ room_of(*level, monster.unit.x, monster.unit.y), monster.targetable() });
                }
                const std::uint32_t filter = skill->aura_filter != 0 ? skill->aura_filter : 0xa783;
                for (const int index : d2d::rules::room_order(units, rooms)) {
                    if (count <= 0) break;
                    const auto& monster = monsters[std::size_t(index)];
                    if (std::hypot(monster.unit.x - missile.x, monster.unit.y - missile.y) * 5 <= float(burst_radius)
                        && filter_takes(filter, monster, from.x, from.y) && !game_data->monsters.types[std::size_t(monster.type)].no_aura) {
                        sub_at(missile.x, missile.y, monster.unit.x - missile.x, monster.unit.y - missile.y);
                        --count;
                    }
                }
                break;
            }
            case 22: {
                const float x = missile.target >= 0 ? monsters[std::size_t(missile.target)].unit.x : missile.x;
                const float y = missile.target >= 0 ? monsters[std::size_t(missile.target)].unit.y : missile.y;
                const int burst_radius = radius(missile_info.hit_par1, skill->aurarange);
                area(missile, x, y, burst_radius, now_ms);
                if (sub)
                    for (std::size_t j = 0; j < monsters.size(); ++j)
                        if (monsters[j].targetable() && game_data->monsters.types[std::size_t(monsters[j].type)].undead
                            && std::hypot(monsters[j].unit.x - x, monsters[j].unit.y - y) * 5 <= float(burst_radius))
                            sub_at(x, y, monsters[j].unit.x - x, monsters[j].unit.y - y);
                break;
            }
            case 29:
                if (sub)
                    for (int k = 0; k < 64; k += std::max(missile_info.hit_par1, 1)) {
                        const auto [aim_x, aim_y] = d2d::rules::kNovaRing[std::size_t(k)];
                        sub_at(missile.x, missile.y, float(aim_x), float(aim_y));
                    }
                break;
            case 48: if (sub) sub_at(missile.x, missile.y, missile.target_x - missile.x, missile.target_y - missile.y); break;
            default: break;
        }
    }

auto Fight::prayer_pulse(const d2d::rules::Skill& skill, int lvl, const d2d::rules::CalcEnv& env, float from_x, float from_y, bool player_cast) -> void {
        using namespace d2d::d2s;
        auto& values = actor->character.stats.values;
        // FUN_005cf010: a player's aura costs FUN_00644b10 (mana_formula, no
        // minmana floor); with less mana its stats stay 0.
        const int cost = player_cast ? d2d::rules::mana_formula(skill, lvl) : 0;
        // Short of it, nothing heals, so the pulse clears state 0x55 (0x5cf262).
        if (values[kMana] < cost) { actor->no_mana_regen = false; return; }
        const float reach = float(d2d::rules::eval_calc(game_data->skills, skill.aurarange, env, skill.id, lvl));
        int changed = 0;   // FUN_005cedc0's count (+0x40): a unit whose life it moved
        for (std::size_t i = 0; i < skill.aurastat.size(); ++i) {
            if (skill.aurastat[i] != kLife) continue;
            const std::int64_t heal = d2d::rules::eval_calc(game_data->skills, skill.aura_calc[i], env, skill.id, lvl);   // 256ths
            if (std::hypot(actor->player.x - from_x, actor->player.y - from_y) * 5 <= reach) {
                const auto life = std::min(values[kMaxLife], values[kLife] + heal);
                if (life != values[kLife]) { values[kLife] = life; ++changed; }
            }
            if (actor->merc && actor->merc->alive() && std::hypot(actor->merc->unit.x - from_x, actor->merc->unit.y - from_y) * 5 <= reach) {
                const int hit_points = std::min(merc_max_life(), actor->merc->hit_points + int(heal >> 8));
                if (hit_points != actor->merc->hit_points) { actor->merc->hit_points = hit_points; ++changed; }
            }
        }
        // Something healed: FUN_0056c110(caster, cost) takes the cost off its
        // mana (there's enough: checked above).
        // With the cost, state 0x55 (nomanaregen) goes on with it
        // (FUN_00639db0(caster, 0x55, 1), 0x5cf244), else off (0x5cf265).
        if (cost > 0 && changed > 0) values[kMana] -= cost;
        if (cost > 0) actor->no_mana_regen = changed > 0;
    }

auto Fight::aura_pulse(std::uint32_t now_ms) -> void {
        // The merc's own (an Act 2 merc's Prayer), on its own clock.
        if (const auto* merc_skill = actor->merc && actor->merc->alive() && actor->merc_aura > 0 ? game_data->skills.get(actor->merc_aura) : nullptr;
            merc_skill && merc_skill->aura && now_ms >= actor->merc_aura_next) {
            actor->merc_aura_next = now_ms + std::uint32_t(std::max(merc_skill->perdelay, 25)) * 40;
            for (std::size_t k = 0; k < actor->merc_ids.size(); ++k)
                if (actor->merc_ids[k] == actor->merc_aura && actor->merc_levels[k] > 0) {
                    const auto merc_env = merc_calc_env();
                    if (merc_skill->srvdofunc == ServerDoFunction::kFriendlyAura) prayer_pulse(*merc_skill, actor->merc_levels[k], merc_env, actor->merc->unit.x, actor->merc->unit.y);
                    else enemy_pulse(*merc_skill, actor->merc_levels[k], merc_env, actor->merc->unit.x, actor->merc->unit.y, {}, merc_foe_id(), now_ms);   // its Holy Freeze
                }
        }
        const auto* skill = game_data->skills.get(actor->aura);
        if (!skill || !skill->aura || now_ms < actor->aura_next || dead()) return;
        const int lvl = skill_level ? skill_level(actor->aura) : 1;
        if (lvl < 1) return;
        const auto env = calc_env();
        actor->aura_next = now_ms + std::uint32_t(std::max(skill->perdelay, 25)) * 40;
        if (skill->srvdofunc == ServerDoFunction::kFriendlyAura) {
            prayer_pulse(*skill, lvl, env, actor->player.x, actor->player.y, true);
            return;
        }
        // Redemption (do 82, FUN_005d0e90): FUN_0056b7e0 runs FUN_005d0d70 on
        // every unit within aurarange, so each corpse there rolls calc1 % on its
        // own and each one redeemed gives calc2 life and calc3 mana back.
        if (skill->srvdofunc == ServerDoFunction::kRedemption) {
            using namespace d2d::d2s;
            const int radius = calc(*skill, skill->aurarange, lvl);
            for (auto& monster : monsters)
                if (!monster.alive() && !monster.corpse_used && monster.mode == "DD" && std::hypot(monster.unit.x - actor->player.x, monster.unit.y - actor->player.y) * 5 <= float(radius)) {
                    if (int(rng(100)) < calc(*skill, skill->calc[0], lvl)) {
                        monster.corpse_used = true;
                        const std::int64_t life = std::int64_t(calc(*skill, skill->calc[1], lvl)) << 8, mana = std::int64_t(calc(*skill, skill->calc[2], lvl)) << 8;
                        actor->character.stats.values[kLife] = std::min(actor->character.stats.values[kMaxLife], actor->character.stats.values[kLife] + life);
                        actor->character.stats.values[kMana] = std::min(actor->character.stats.values[kMaxMana], actor->character.stats.values[kMana] + mana);
                    }
                }
            return;
        }
        const std::array<int, 4> pierce{ int(actor->psum[d2d::d2s::kPassiveFirePierce]), int(actor->psum[d2d::d2s::kPassiveLightningPierce]), int(actor->psum[d2d::d2s::kPassiveColdPierce]), int(actor->psum[d2d::d2s::kPassivePoisonPierce]) };
        enemy_pulse(*skill, lvl, env, actor->player.x, actor->player.y, pierce, player_foe_id(), now_ms);
    }

auto Fight::enemy_pulse(const d2d::rules::Skill& skill, int lvl, const d2d::rules::CalcEnv& env, float x, float y, const std::array<int, 4>& pierce, int striker, std::uint32_t now_ms) -> void {
        if ((skill.srvdofunc != ServerDoFunction::kEnemyAura && skill.srvdofunc != ServerDoFunction::kHolyFreeze) || skill.etype < 0 || skill.etype > 4) return;
        const auto damage = d2d::rules::MissileDamage{ .etype = skill.etype,
                                                   .elo = d2d::rules::elem_damage(game_data->skills, skill, env, lvl, false, 0, true),
                                                   .ehi = d2d::rules::elem_damage(game_data->skills, skill, env, lvl, true, 0, true),
                                                   .elen = d2d::rules::elem_length(game_data->skills, skill, env, lvl) };
        const float reach = float(d2d::rules::eval_calc(game_data->skills, skill.aurarange, env, skill.id, lvl));
        // Holy Freeze's callback (FUN_005d07c0) then rolls shatter (state 107) itself on
        // each one's seed, rand % 100 < 20, deadCol or not (the chill's roll came first).
        for (std::size_t i = 0; i < monsters.size(); ++i)
            if (std::hypot(monsters[i].unit.x - x, monsters[i].unit.y - y) * 5 <= reach && aura_takes(skill, monsters[i], x, y)) {
                land(i, d2d::rules::missile_blow(damage, target_of(i), pierce, rng), true, now_ms, striker);
                if (skill.srvdofunc == ServerDoFunction::kHolyFreeze) monsters[i].shatter = monsters[i].seed.next() % 100 < 20;
            }
    }

auto Fight::aura_takes(const d2d::rules::Skill& skill, const Monster& monster, float from_x, float from_y) const -> bool {
        return filter_takes(skill.aura_filter, monster, from_x, from_y);
    }

auto Fight::filter_takes(std::uint32_t filter, const Monster& monster, float from_x, float from_y) const -> bool {
        if (monster.type < 0 || std::size_t(monster.type) >= game_data->monsters.types.size()) return false;
        const auto& type_info = game_data->monsters.types[std::size_t(monster.type)];
        const bool npc = std::size_t(monster.type) < game_data->mon_is_npc.size() && game_data->mon_is_npc[std::size_t(monster.type)];
        // The line (0x200) only when asked: FUN_0064e260 from the walk's unit to it, mask 4, as in_reach's.
        auto clear_line = [&] {
            if ((filter & d2d::rules::aura_filter::kClearLine) == 0 || !level) return true;
            auto sub = [](float cells) { return int(std::floor(cells * 5)); };
            const auto wall = [this](int at_x, int at_y) { return level->blocked((float(at_x) + 0.5f) / 5, (float(at_y) + 0.5f) / 5, 0x04); };
            return !d2d::rules::sight_blocked(sub(from_x), sub(from_y), 0, sub(monster.unit.x), sub(monster.unit.y), 0, wall);
        };
        return d2d::rules::aura_walk_takes(filter, { .dead = !monster.alive() && monster.mode == "DD", .dying = !monster.alive() && monster.mode != "DD",
                                                     .undead = type_info.undead, .boss = type_info.boss_column, .prime_evil = type_info.prime_evil,
                                                     .ally = false, .npc = npc, .unit_flag_4 = monster.targetable(), .unit_flag_8 = monster.targetable(),
                                                     .in_town = in_town(level), .clear_line = clear_line() });
    }

auto Fight::in_aura(const Monster& monster) -> bool {
        const auto* skill = game_data->skills.get(actor->aura);
        if (!skill || !skill->aura) return false;
        const int radius = d2d::rules::eval_calc(game_data->skills, skill->aurarange, calc_env(), skill->id, skill_level ? skill_level(actor->aura) : 1);
        return std::hypot(monster.unit.x - actor->player.x, monster.unit.y - actor->player.y) * 5 <= float(radius)
            && aura_takes(*skill, monster, actor->player.x, actor->player.y);
    }

auto Fight::target_of(std::size_t monster_index) -> d2d::rules::Target {
        auto target = monsters[monster_index].target(*game_data);
        const auto base = target.res;
        // Its curse and cry (Amplify Damage, Lower Resist, Decrepify, Battle
        // Cry, Inner Sight, Cloak of Shadows) and self state (Chilling
        // Armor's skill_armor_percent): their aurastats as the aura's.
        for (const auto* effect : { &monsters[monster_index].curse, &monsters[monster_index].cry, &monsters[monster_index].self_state })
            if (const auto* skill = effect->skill >= 0 ? game_data->skills.get(effect->skill) : nullptr)
                apply_target_stats(*skill, effect->level, target, base);
        // The enemy auras over it (the player's, the merc's).
        for (const auto& aura : enemy_auras(monsters[monster_index])) apply_target_stats(*aura.skill, aura.level, target, base, &aura.env);
        return target;
    }

auto Fight::apply_target_stats(const d2d::rules::Skill& skill_row, int lvl, d2d::rules::Target& target, const std::array<int, 6>& base, const d2d::rules::CalcEnv* aura_env) -> void {
        const auto* skill = &skill_row;
        const auto env = aura_env ? *aura_env : calc_env();
        // FUN_005c3540: a lowering of a resistance whose base is 100 or more
        // (before any curse) counts a fifth, truncated toward 0.
        const auto lowered = [&base](std::size_t type, int value) { return value < 0 && base[type] >= 100 ? value / 5 : value; };
        for (std::size_t k = 0; k < skill->aurastat.size(); ++k) {
            const int value = d2d::rules::eval_calc(game_data->skills, skill->aura_calc[k], env, skill->id, lvl);
            switch (skill->aurastat[k]) {
                case 36: target.res[0] += lowered(0, value); break;  case 37: target.res[1] += lowered(1, value); break;
                case 39: target.res[2] += lowered(2, value); break;  case 41: target.res[3] += lowered(3, value); break;
                case 43: target.res[4] += lowered(4, value); break;  case 45: target.res[5] += lowered(5, value); break;
                case 171: target.armor_class += target.armor_class * value / 100; break;
                case 31: target.armor_class = std::max(target.armor_class + value, 0); break;   // Inner Sight's armorclass
                default: break;
            }
        }
    }

}  // namespace d2d::game
