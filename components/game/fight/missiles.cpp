// SPDX-License-Identifier: GPL-3.0-or-later
// Fight (fight.hpp): ammo, wear, missiles, bursts and auras. The rest is in this directory.
#include "fight.hpp"

#include "ai.hpp"
#include "character.hpp"
#include "cues.hpp"
#include "gamedata.hpp"

#include <combat.hpp>
#include <d2s_items.hpp>
#include <level_ids.hpp>
#include <missiles.hpp>
#include <monsters.hpp>
#include <rules.hpp>
#include <skills.hpp>
#include <sound_ids.hpp>

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

auto Fight::spend_ammo() -> void {
        const int index = ammo_index();
        if (index < 0) return;
        if (--actor->character.items[std::size_t(index)].quantity <= 0) out_of_ammo(std::size_t(index));
    }

auto Fight::out_of_ammo(std::size_t index) -> void {
        using namespace d2d::d2s;
        auto& item = actor->character.items[index];
        item.quantity = 0;
        const auto info = game_data->rules.item_info.find(item.code);
        if (info != game_data->rules.item_info.end() && info->second.kind == 2 && item.quality >= 4 && item.quality <= 9) {
            item.flags |= 0x100u;
            item.durability = 0;
            return;
        }
        const std::string code = item.code;
        const auto slot = item.slot;
        actor->character.items.erase(actor->character.items.begin() + std::ptrdiff_t(index));
        const auto next = std::ranges::find_if(actor->character.items, [&](const Item& other) {
            return other.code == code && other.location == item_location::kStored && other.panel == item_panel::kInventory && !d2d::rules::broken(other); });
        if (next != actor->character.items.end()) { next->location = item_location::kEquipped; next->slot = slot; next->panel = 0; }
    }

auto Fight::wear(std::size_t index, std::uint32_t now_ms) -> void {
        using namespace d2d::d2s;
        auto& item = actor->character.items[index];
        const auto& tables = game_data->rules;
        const auto info = tables.item_info.find(item.code);
        if (info == tables.item_info.end()) return;
        const bool armor = d2d::rules::type_is(tables, info->second.type, "armo"), weapon = d2d::rules::type_is(tables, info->second.type, "weap");
        if ((!armor && !weapon) || item.max_durability <= 0 || d2d::rules::indestructible(item) || d2d::rules::broken(item)) return;
        const int chance = armor || throwable(*game_data, item.code) ? 10 : 4;
        if (int(rng(100)) >= chance) return;
        if (item.durability - 1 >= 1) { item.durability = std::min(item.durability - 1, d2d::rules::max_durability(item)); return; }
        if (armor) { item.flags |= 0x100u; item.durability = 0; cues.cue(d2d::rules::sound_ids::kCursorDurabilityBreak, now_ms, actor->player.x, actor->player.y); return; }
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
                else if (item.max_durability > 0 && !d2d::rules::indestructible(item)) {
                    item.durability -= d2d::rules::eval_calc(game_data->skills, skill.calc[2], env, skill.id, lvl);
                    if (item.durability <= 0) {
                        item.durability = 0; item.flags |= 0x100u;             // FUN_0055f850: it breaks
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
        if (skill_refused(*game_data, actor->character, skill)) { actor->attack_mon = -1; impossible(now_ms); return false; }
        const int lvl = cast_level(skill);
        const int cost = cost_of(*skill_row, lvl);
        if (lvl > 0 && actor->character.stats.values[kMana] < cost) need_mana(now_ms);
        if (lvl <= 0 || actor->character.stats.values[kMana] < cost || (needs_ammo(*skill_row) && ammo_index() < 0)) { actor->attack_mon = -1; return false; }
        pay(*skill_row, cost);
        actor->swing_skill = skill;
        actor->cast_x = target_x; actor->cast_y = target_y;
        actor->player.dir = direction16(target_x - actor->player.x, target_y - actor->player.y);
        set_pmode(skill_row->anim == "A1" || skill_row->anim == "TH" ? kModeA1 : kModeSC, now_ms);
        actor->pstruck = false;
        return true;
    }

auto Fight::launch(const GameData::MissileInfo& missile_info, const d2d::rules::Skill& skill, int lvl, float x, float y, float dx, float dy,
                    int range, std::uint32_t now_ms) -> Missile& {
        const float speed = missile_speed(missile_info, lvl), distance = std::max(std::hypot(dx, dy), 0.01f);
        Missile missile{ &missile_info, x, y, dx / distance * speed, dy / distance * speed, missile_facing(x, y, dx, dy), now_ms, now_ms + std::uint32_t(std::max(range, 1)) * 40, {} };
        missile.friendly = true; missile.caster = actor->id; missile.skill = skill.id; missile.level = lvl; missile.by_merc = casting_merc; missile.striker = casting_unit;
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
                const float angle = float(k) * 2 * 3.14159265f / 64;
                auto& shot = send(std::cos(angle), std::sin(angle), skill.srvdofunc == ServerDoFunction::kWarCry ? missile_info.range : missile_info.range + calc1);
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
            else actor->channel = skill.id;
        } else if (skill.srvdofunc == ServerDoFunction::kBlessedHammer) {                      // Blessed Hammer (FUN_005d0040): path 14, a spiral out
            auto& missile = send(dx, dy, range);
            missile.target_x = caster.x; missile.target_y = caster.y;
            missile.turn = int(std::atan2(dy, dx) * 1000);
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
                if (skill->srvdofunc == ServerDoFunction::kBreath)
                    launch(missile_info, *skill, lvl, actor->player.x, actor->player.y, actor->cast_x - actor->player.x, actor->cast_y - actor->player.y, std::max(calc(*skill, skill->calc[0], lvl), 1), now_ms);
                else if (frame % 3 == 0)                     // Blade Fury (FUN_005d68a0): one per attack frame
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
                    const float offset_x = float(int(rng(2 * radius - 1)) - (radius - 1)) / 5, offset_y = float(int(rng(2 * radius - 1)) - (radius - 1)) / 5;
                    launch(sub->second, *skill, missile.level, missile.x + offset_x, missile.y + offset_y, 0, 0, sub->second.range, now_ms);
                }
            }
            if (missile_info.srv_do == 5) {
                // A row with no Skill (Meteor's, Immolation Arrow's fire) burns its own element with its synergy.
                const auto damage = missile.fixed >= 0 ? d2d::rules::MissileDamage{ .etype = 0, .elo = missile.fixed, .ehi = missile.fixed }
                                  : missile_info.skill.empty() ? row_damage(missile_info, missile.level, calc_env(), missile.skill)
                                                               : d2d::rules::missile_damage(game_data->skills, *skill, calc_env(), missile.level);
                for (std::size_t i = 0; i < monsters.size(); ++i)
                    if (monsters[i].targetable() && std::hypot(monsters[i].unit.x - missile.x, monsters[i].unit.y - missile.y) <= 0.5f
                        && !std::ranges::contains(burned, std::pair{ int(i), missile.skill })) {
                        burned.emplace_back(int(i), missile.skill);
                        burn(i, damage, now_ms);
                    }
            }
            // The Range runs out: the hit function goes off with no unit.
            static constexpr int kEnd[] = { 1, 3, 4, 9, 13, 14, 20, 22, 26, 29, 36, 48, 56 };
            if (now_ms + 40 >= missile.dies && missile.frame < 0 && std::ranges::contains(kEnd, missile_info.hit_func)) burst(missile, now_ms);
            // 15 (FUN_005af030, Frozen Orb): every Param1 frames SubMissile1
            // toward direction `turn` of 64 (0x6e2b78 / 0x6e2a78), turning
            // Param2 on.
            if (missile_info.srv_do == 15 && sub != game_data->missiles.end() && age % std::max(missile_info.param1, 1) == 0) {
                const float angle = float(missile.turn & 63) * 2 * 3.14159265f / 64;
                launch(sub->second, *skill, missile.level, missile.x, missile.y, std::cos(angle), std::sin(angle), sub->second.range, now_ms);
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
                missile.hit_at.clear();
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
            // SubMissile1 thrown at a point within Param2 (else aurarange).
            // ponytail: its Param3 / Param4 frame window isn't applied (Volcano,
            // level 24: past Act 1).
            if (missile_info.srv_do == 28 && sub != game_data->missiles.end()
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

auto Fight::burn(std::size_t monster_index, const d2d::rules::MissileDamage& damage, std::uint32_t now_ms) -> void {
        static constexpr int kRes[5] = { 2, 3, 4, 5, 1 };
        if (damage.etype < 0 || damage.etype > 4 || damage.ehi <= 0) return;
        auto& monster = monsters[monster_index];
        const auto target = target_of(monster_index);
        int res = target.res[std::size_t(kRes[damage.etype])];
        if (damage.etype < 4 && res < 100) res = std::max(res - pierce()[std::size_t(damage.etype)], -100);
        monster.dot_acc += double(d2d::rules::resisted(rng.range(damage.elo, damage.ehi), res)) / 256;
        const int whole = int(monster.dot_acc);
        if (whole <= 0) return;
        monster.dot_acc -= whole;
        monster.hit_points -= whole;
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
        // 7 (FUN_005a9fb0: Holy Bolt, the Fist's bolts): sHitPar2 1 strikes
        // the undead only (FUN_0063e990), 2 demons (FUN_0063e940); the rest
        // it passes (4). ponytail: it doesn't heal the player's side (calc1).
        if (missile_info.hit_func == 7) {
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
        // NextHit rows strike a monster again NextDelay frames on.
        if (missile_info.next_hit) {
            auto found = std::ranges::find(missile.hit_at, int(monster_index), &std::pair<int, std::uint32_t>::first);
            if (found != missile.hit_at.end() && now_ms < found->second + std::uint32_t(std::max(missile_info.next_delay, 1)) * 40) return false;
            if (found == missile.hit_at.end()) missile.hit_at.emplace_back(int(monster_index), now_ms); else found->second = now_ms;
        }
        // 1 (FUN_005a9a70) and 13 (FUN_005aa8b0) go off here, the area taking in the unit.
        if (missile_info.hit_func == 1 || missile_info.hit_func == 13) { burst(missile, now_ms); return true; }
        strike(missile, monster_index, now_ms);
        if (!missile_info.next_hit) missile.struck->push_back(int(monster_index));
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
            burst(missile, now_ms);
            if (missile_info.hit_func == 47) return false;             // the boulder rolls on (FUN_005ac550 returns 1)
            return true;
        }
        return missile_info.collide_kill && !(missile_info.pierce && int(rng(100)) < int(actor->psum[d2d::d2s::kPierceIndex]));
    }

auto Fight::strike(const Missile& missile, std::size_t monster_index, std::uint32_t now_ms, int freeze ) -> void {
        const auto* skill = game_data->skills.get(missile.skill);
        if (!skill || !monsters[monster_index].alive()) return;
        const auto target = target_of(monster_index);
        // A merc's skill missile (Fire Arrow): its weapon, attack rating and
        // level; its skill's calcs see no synergies (mercs have none).
        const int clvl = missile.by_merc ? actor->merc_st.level : int(actor->character.stats.get(d2d::d2s::kLevel));
        const auto fighter = missile.by_merc ? merc_fighter() : actor->player_combat;
        const auto env = missile.by_merc ? merc_calc_env() : calc_env();
        auto damage = missile.fixed >= 0 ? d2d::rules::MissileDamage{ .etype = missile.info->etype < 0 ? 0 : missile.info->etype, .elo = missile.fixed, .ehi = missile.fixed }
                : missile.info->skill.empty() ? row_damage(*missile.info, missile.level, env, missile.skill)
                                        : d2d::rules::missile_damage(game_data->skills, *skill, env, missile.level);
        d2d::rules::Blow blow{ .hit = true };
        if (damage.srcdam > 0) {
            d2d::rules::Swing swing;
            swing.srcdam = damage.srcdam;
            swing.ed_pct = missile.ed_pct;
            blow = d2d::rules::player_blow(fighter, target, clvl, rng, swing);
        } else if (missile.info->to_hit) {
            blow.hit = int(rng(100)) < d2d::rules::hit_chance(fighter.attack_rating, target.armor_class, clvl, target.level);
        }
        if (blow.hit) blow = d2d::rules::missile_blow(damage, target, pierce(), rng, blow);
        blow.hit_class = missile.info->hit_class | skill->hit_class;
        if (monsters[monster_index].half_freeze) freeze /= 2;          // stat 0x76 (the Countess)
        if (blow.hit && freeze > 0) blow.chill_ticks = std::max(blow.chill_ticks, freeze);
        land(monster_index, blow, true, now_ms, missile.striker >= 0 ? missile.striker : missile.by_merc ? merc_foe_id() : player_foe_id());
        // A freeze stands it as a stun does, without the stun's guards.
        if (blow.hit && freeze > 0) monsters[monster_index].stun_until = std::max(monsters[monster_index].stun_until, now_ms + std::uint32_t(freeze) * 40);
    }

auto Fight::area(const Missile& missile, float x, float y, int radius, std::uint32_t now_ms, int freeze ) -> void {
        for (std::size_t j = 0; j < monsters.size(); ++j)
            if (monsters[j].targetable() && std::hypot(monsters[j].unit.x - x, monsters[j].unit.y - y) * 5 <= float(std::max(radius, 1))) strike(missile, j, now_ms, freeze);
    }

auto Fight::burst(Missile& missile, std::uint32_t now_ms) -> void {
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
            case 1: area(missile, missile.x, missile.y, radius(missile_info.hit_par1, skill->calc[0]), now_ms); break;
            case 3: area(missile, missile.x, missile.y, radius(missile_info.hit_par1, skill->aurarange), now_ms); break;
            case 13: area(missile, missile.x, missile.y, radius(missile_info.hit_par1, skill->aurarange), now_ms, radius(missile_info.hit_par2, skill->auralen)); break;
            case 2: case 4: case 26: case 36: case 51: if (sub) sub_at(missile.x, missile.y, 0, 0); break;
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
            case 14: case 47: case 56: {
                area(missile, missile.x, missile.y, radius(missile_info.hit_par1, skill->aurarange), now_ms);
                // Meteor's (FUN_005aabb0 -> FUN_005aaa90): its fire lasts FUN_004cc7c0's Param3 + (lvl - 1) x Param4 frames.
                // ponytail: 47 / 56 (untraced) keep the sub-row's EMin a frame.
                const int frames = missile_info.hit_func == 14 ? skill->par[2] + (missile.level - 1) * skill->par[3] : 0;
                if (sub)
                    for (std::size_t j = 0; j < 18; j += std::size_t(std::max(missile_info.hit_par2, 1))) {
                        auto& fire = sub_at(missile.x + float(kSpreadX[j]) / 5, missile.y + float(kSpreadY[j]) / 5, 0, 0, frames);
                        if (missile_info.hit_func != 14) fire.fixed = hit_sub->second.emin > 0 ? hit_sub->second.emin : -1;
                    }
                break;
            }
            case 20: {
                if (!sub) break;
                const int burst_radius = radius(missile_info.hit_par1, skill->aurarange);
                int count = radius(missile_info.hit_par2, skill->calc[0]);
                for (std::size_t j = 0; j < monsters.size() && count > 0; ++j)
                    if (int(j) != missile.target && monsters[j].targetable() && std::hypot(monsters[j].unit.x - missile.x, monsters[j].unit.y - missile.y) * 5 <= float(burst_radius)) {
                        sub_at(missile.x, missile.y, monsters[j].unit.x - missile.x, monsters[j].unit.y - missile.y);
                        --count;
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
                        const float angle = float(k) * 2 * 3.14159265f / 64;
                        sub_at(missile.x, missile.y, std::cos(angle), std::sin(angle));
                    }
                break;
            case 48: if (sub) sub_at(missile.x, missile.y, missile.target_x - missile.x, missile.target_y - missile.y); break;
            default: break;
        }
    }

auto Fight::aura_pulse(std::uint32_t now_ms) -> void {
        const auto* skill = game_data->skills.get(actor->aura);
        if (!skill || !skill->aura || now_ms < actor->aura_next || dead()) return;
        const int lvl = skill_level ? skill_level(actor->aura) : 1;
        if (lvl < 1) return;
        const auto env = calc_env();
        actor->aura_next = now_ms + std::uint32_t(std::max(skill->perdelay, 25)) * 40;
        if (skill->srvdofunc == ServerDoFunction::kFriendlyAura) {
            for (std::size_t i = 0; i < skill->aurastat.size(); ++i)
                if (skill->aurastat[i] == 6) {
                    using namespace d2d::d2s;
                    const auto heal = d2d::rules::eval_calc(game_data->skills, skill->aura_calc[i], env, skill->id, lvl);
                    actor->character.stats.values[kLife] = std::min(actor->character.stats.values[kMaxLife], actor->character.stats.values[kLife] + heal);
                }
            return;
        }
        // Redemption (do 82): a corpse within aurarange redeemed at calc1 %,
        // calc2 life and mana back.
        // ponytail: its callback isn't traced; one corpse a pulse (Redemption,
        // level 30: past Act 1).
        if (skill->srvdofunc == ServerDoFunction::kRedemption) {
            using namespace d2d::d2s;
            const int radius = calc(*skill, skill->aurarange, lvl);
            for (auto& monster : monsters)
                if (!monster.alive() && !monster.corpse_used && monster.mode == "DD" && std::hypot(monster.unit.x - actor->player.x, monster.unit.y - actor->player.y) * 5 <= float(radius)) {
                    if (int(rng(100)) < calc(*skill, skill->calc[0], lvl)) {
                        monster.corpse_used = true;
                        const std::int64_t gain = std::int64_t(calc(*skill, skill->calc[1], lvl)) << 8;
                        actor->character.stats.values[kLife] = std::min(actor->character.stats.values[kMaxLife], actor->character.stats.values[kLife] + gain);
                        actor->character.stats.values[kMana] = std::min(actor->character.stats.values[kMaxMana], actor->character.stats.values[kMana] + gain);
                    }
                    break;
                }
            return;
        }
        if ((skill->srvdofunc != ServerDoFunction::kEnemyAura && skill->srvdofunc != ServerDoFunction::kHolyFreeze) || skill->etype < 0 || skill->etype > 4) return;
        const auto damage = d2d::rules::MissileDamage{ .etype = skill->etype,
                                                   .elo = d2d::rules::elem_damage(game_data->skills, *skill, env, lvl, false, 0, true),
                                                   .ehi = d2d::rules::elem_damage(game_data->skills, *skill, env, lvl, true, 0, true),
                                                   .elen = d2d::rules::elem_length(game_data->skills, *skill, env, lvl) };
        const std::array<int, 4> pierce{ int(actor->psum[d2d::d2s::kPassiveFirePierce]), int(actor->psum[d2d::d2s::kPassiveLightningPierce]), int(actor->psum[d2d::d2s::kPassiveColdPierce]), int(actor->psum[d2d::d2s::kPassivePoisonPierce]) };
        for (std::size_t i = 0; i < monsters.size(); ++i)
            if (monsters[i].alive() && in_aura(monsters[i]))
                land(i, d2d::rules::missile_blow(damage, target_of(i), pierce, rng), true, now_ms, player_foe_id());
    }

auto Fight::in_aura(const Monster& monster) -> bool {
        const auto* skill = game_data->skills.get(actor->aura);
        if (!skill || !skill->aura) return false;
        const int radius = d2d::rules::eval_calc(game_data->skills, skill->aurarange, calc_env(), skill->id, skill_level ? skill_level(actor->aura) : 1);
        return std::hypot(monster.unit.x - actor->player.x, monster.unit.y - actor->player.y) * 5 <= float(radius);
    }

auto Fight::target_of(std::size_t monster_index) -> d2d::rules::Target {
        auto target = monsters[monster_index].target(*game_data);
        // Its curse and cry (Amplify Damage, Lower Resist, Decrepify, Battle
        // Cry, Inner Sight, Cloak of Shadows): their aurastats as the aura's.
        for (const auto* effect : { &monsters[monster_index].curse, &monsters[monster_index].cry })
            if (const auto* skill = effect->skill >= 0 ? game_data->skills.get(effect->skill) : nullptr)
                apply_target_stats(*skill, effect->level, target);
        const auto* skill = game_data->skills.get(actor->aura);
        if (!skill || (skill->srvdofunc != ServerDoFunction::kEnemyAura && skill->srvdofunc != ServerDoFunction::kHolyFreeze) || !in_aura(monsters[monster_index])) return target;
        apply_target_stats(*skill, skill_level ? skill_level(actor->aura) : 1, target);
        return target;
    }

auto Fight::apply_target_stats(const d2d::rules::Skill& skill_row, int lvl, d2d::rules::Target& target) -> void {
        const auto* skill = &skill_row;
        const auto env = calc_env();
        for (std::size_t k = 0; k < skill->aurastat.size(); ++k) {
            const int value = d2d::rules::eval_calc(game_data->skills, skill->aura_calc[k], env, skill->id, lvl);
            switch (skill->aurastat[k]) {
                case 36: target.res[0] += value; break;  case 37: target.res[1] += value; break;
                case 39: target.res[2] += value; break;  case 41: target.res[3] += value; break;
                case 43: target.res[4] += value; break;  case 45: target.res[5] += value; break;
                case 171: target.armor_class += target.armor_class * value / 100; break;
                case 31: target.armor_class = std::max(target.armor_class + value, 0); break;   // Inner Sight's armorclass
                default: break;
            }
        }
    }

}  // namespace d2d::game
