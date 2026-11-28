// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: game
// test-includes: apps/d2d
// Optional touch/controller intent versus the unchanged pointer command defaults.
#include "skills/direct.hpp"

#include <ai/units.hpp>
#include <d2s_items.hpp>
#include <ds1.hpp>
#include <fight.hpp>
#include <gamedata.hpp>
#include <level.hpp>
#include <level_ids.hpp>
#include <protocol.hpp>
#include <skills.hpp>
#include <world.hpp>

#include <array>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <exception>
#include <limits>

namespace {
using namespace d2d::game;
using d2d::rules::ServerDoFunction;
using d2d::rules::ServerStartFunction;
constexpr int kBash = 1, kMissile = 2, kHowl = 3, kCharge = 4, kLeap = 5;

struct Fixture {
    GameData data;
    World world{ &data, 5, 5 };
    Level field;
    Fixture() {
        data.skills.rows = {
            { .id = 0, .name = "Attack", .srvdofunc = ServerDoFunction::kAttack, .anim = "A1", .range = "h2h" },
            { .id = kBash, .name = "Bash", .srvstfunc = ServerStartFunction::kBuildHit,
              .srvdofunc = ServerDoFunction::kResolveHit, .anim = "A1", .range = "h2h", .mana = 2 },
            { .id = kMissile, .name = "Test missile", .anim = "SC", .srvmissile = "test missile" },
            { .id = kHowl, .name = "Howl", .srvdofunc = ServerDoFunction::kNova, .anim = "SC", .srvmissilea = "test howl" },
            { .id = kCharge, .name = "Charge", .srvstfunc = ServerStartFunction::kCharge,
              .srvdofunc = ServerDoFunction::kCharge, .anim = "SQ", .par = { 150 }, .seqnum = 4 },
            { .id = kLeap, .name = "Leap Attack", .srvstfunc = ServerStartFunction::kLeapAttack,
              .srvdofunc = ServerDoFunction::kLeapAttack, .anim = "SQ", .seqnum = 14 },
        };
        data.skills.rows[kBash].delay = d2d::rules::compile_calc("8", data.skills.names);
        data.missiles["test missile"] = { .name = "test missile", .vel = 10, .range = 12 };
        data.missiles["test howl"] = { .name = "test howl", .vel = 10, .range = 12 };
        data.walk_velocity[0] = 6;
        data.run_velocity[0] = 9;
        for (const int mode : { kModeA1, kModeSC }) {
            const std::array<std::uint8_t, 18> key{ 0, std::uint8_t(mode) };
            data.composite_timings.emplace(key, GameData::AnimTiming{ .speed = 256, .frames = 4, .action = 2 });
        }
        field.id = d2d::rules::level_ids::kBloodMoor;
        field.ds1 = d2d::ds1::Map(16, 16, 1, 1);
        field.walk.assign(80 * 80, 0);
        assert(!world.direct_casts);
        world.direct_casts = true;
        world.level = &field;
        world.fight.skill_level = [](int) { return 1; };
        world.actor->character.stats.values[d2d::d2s::kMana] = 20 * 256;
    }
    void cast(int skill, float x, float y, bool direct, std::uint32_t now = 1000, int unit = -1) {
        const auto command = direct ? d2d::client::skillpad::direct_cast(skill, x, y, unit, true)
            : cmd::UseSkill{ skill, x, y, unit, true };
        // Exercise the same intent after the native-LAN codec as well as local dispatch.
        const auto received = decode(encode(command));
        assert(received && std::get<cmd::UseSkill>(*received).direct == direct);
        world.apply(*received, now);
    }
    void finish(std::uint32_t now = 1000) {
        for (const auto end = now + 4000; world.actor->pmode >= 0 && now < end; now += kTickMs)
            world.fight.player_modes(false, now, float(kTickMs) / 1000.f);
        assert(world.actor->pmode < 0);
    }
};
} // namespace

int main() try {
    using namespace d2d::game;
    for (const bool direct : { false, true }) {
        for (const int skill : { kCharge, kLeap }) {
            Fixture test;
            auto& actor = *test.world.actor;
            const float from_x = actor.player.x, from_y = actor.player.y;
            test.cast(skill, from_x + 3, from_y + 2, direct);
            assert((actor.pmode >= 0) == direct);
            test.finish();
            assert(std::abs(actor.player.x - (from_x + (direct ? 3 : 0))) < .05f);
            assert(std::abs(actor.player.y - (from_y + (direct ? 2 : 0))) < .05f);
            assert(!actor.smove.active && !actor.direct_swing);
        }
        Fixture melee;
        auto& actor = *melee.world.actor;
        const auto mana = actor.character.stats.get(d2d::d2s::kMana);
        melee.cast(kBash, actor.player.x + 3, actor.player.y, direct);
        assert((actor.pmode == kModeA1) == direct);
        assert(actor.character.stats.get(d2d::d2s::kMana) == mana - (direct ? 2 * 256 : 0));
        melee.finish();
        assert((actor.skill_delay > 1000) == direct);
        if (direct) {
            const auto remaining = actor.character.stats.get(d2d::d2s::kMana);
            melee.cast(kBash, actor.player.x + 3, actor.player.y, true, actor.skill_delay - 1);
            assert(actor.pmode < 0 && actor.character.stats.get(d2d::d2s::kMana) == remaining);
            // Starting an ordinary pointer swing resets direct-only action-frame policy.
            actor.attack_skill = 0;
            assert(melee.world.fight.start_swing(actor.skill_delay));
            assert(!actor.direct_swing);
        }
        Fixture centered;
        auto& caster = *centered.world.actor;
        caster.player.dir = 7;
        centered.cast(kHowl, caster.player.x, caster.player.y, direct);
        assert(caster.player.dir == (direct ? 7 : direction16(0, 0)));
        centered.finish();
        assert(centered.world.fight.pending.size() == 64);
        Fixture targeted;
        auto& hero = *targeted.world.actor;
        Monster foe;
        foe.id = 42; foe.hit_points = 100;
        foe.unit.x = hero.player.x + 3; foe.unit.y = hero.player.y;
        targeted.world.fight.monsters.push_back(foe);
        targeted.cast(0, foe.unit.x, foe.unit.y, direct, 1000, foe.id);
        assert(hero.attack_mon == 0 && hero.player.walking && !hero.direct_swing);
        Fixture stale;
        auto& shooter = *stale.world.actor;
        stale.world.fight.monsters.push_back(foe);
        shooter.attack_mon = 0;
        stale.cast(kMissile, shooter.player.x + 3, shooter.player.y, direct);
        assert(shooter.attack_mon == (direct ? -1 : 0));
    }
    // The classic controller's centered cast must not approach a selected foe.
    Fixture shout;
    auto& caster = *shout.world.actor;
    Monster distant;
    distant.id = 51; distant.hit_points = 100;
    distant.unit.x = caster.player.x + 6; distant.unit.y = caster.player.y;
    shout.world.fight.monsters.push_back(distant);
    caster.player.dir = 7;
    shout.cast(kHowl, distant.unit.x, distant.unit.y, true, 1000, distant.id);
    assert(caster.attack_mon == -1 && !caster.player.walking && caster.player.dir == 7);
    assert(caster.cast_x == caster.player.x && caster.cast_y == caster.player.y);
    Fixture invalid;
    invalid.cast(kCharge, std::numeric_limits<float>::infinity(), 5, true);
    assert(invalid.world.actor->pmode < 0 && !invalid.world.actor->smove.active);
    Fixture disabled;
    disabled.world.direct_casts = false;
    disabled.cast(kCharge, disabled.world.actor->player.x + 3, disabled.world.actor->player.y, true);
    assert(disabled.world.actor->pmode < 0 && !disabled.world.actor->smove.active);
    Fixture blocked;
    auto& hero = *blocked.world.actor;
    const auto mana = hero.character.stats.get(d2d::d2s::kMana);
    blocked.cast(kLeap, -1, hero.player.y, true);
    assert(hero.pmode < 0 && hero.character.stats.get(d2d::d2s::kMana) == mana);
    blocked.cast(kCharge, -1, hero.player.y, true);
    blocked.finish();
    assert(!blocked.field.unit_blocked(hero.player.x, hero.player.y));
    hero.character.stats.values[d2d::d2s::kMana] = 0;
    blocked.cast(kBash, hero.player.x + 2, hero.player.y, true, 10000);
    assert(hero.pmode < 0 && hero.character.stats.get(d2d::d2s::kMana) == 0);
    return 0;
} catch (const std::exception& error) {
    std::fprintf(stderr, "%s\n", error.what());
    return 1;
}
