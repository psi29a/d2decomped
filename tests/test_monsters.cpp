// Monster spawning and stats over hand-made tables: the region takes
// every listed type once, rooms fill by density, Fallen come as a leader
// with a party, nothing lands on a blocked subtile or by the entrance.
#include <monsters.hpp>

#include <cassert>
#include <cstdio>

using namespace d2d::rules;

int main() {
    Monsters m;
    MonType zombie;
    zombie.id = "zombie1"; zombie.enabled = true; zombie.base = 5;
    zombie.min_grp = 1; zombie.max_grp = 2; zombie.rarity = 2; zombie.level = { 1, 36, 67 };
    zombie.diff[0] = { .min_hp = 101, .max_hp = 181, .ac = 84, .exp = 111, .a1_min = 51, .a1_max = 151, .a1_th = 101 };
    MonType fallen;
    fallen.id = "fallen1"; fallen.enabled = true; fallen.base = 19;
    fallen.min_grp = 2; fallen.max_grp = 3; fallen.party_min = 2; fallen.party_max = 3; fallen.rarity = 2;
    fallen.minion = { 1, -1 };
    MonType off;
    off.id = "off"; off.rarity = 5;                               // not enabled: never in a region
    m.types = { zombie, fallen, off };
    for (std::size_t i = 0; i < m.types.size(); ++i) m.by_id[m.types[i].id] = int(i);
    m.lvl.resize(2);
    m.lvl[1] = { .ac = { 6, 0, 0 }, .th = { 8, 0, 0 }, .hp = { 7, 0, 0 }, .dm = { 2, 0, 0 }, .xp = { 30, 0, 0 } };

    // Region: all three listed, the disabled one dropped.
    LevelMon L{ .density = { 10000, 0, 0 }, .num_mon = 3, .mon = { 0, 1, 2 } };
    Rng rs{ 42 };
    const auto reg = monster_region(m, L, 0, rs);
    assert(reg.types.size() == 2 && reg.total == 4);

    // A 40x40-subtile room (one 8x8-tile room) with a blocked column.
    auto fits = [](int x, int y) { return x != 20 && x >= 0 && y >= 0; };
    auto near = [](int x, int y) { return x < 5 && y < 5; };
    std::vector<Spawn> a, b;
    Rng g1{ 7 }, g2{ 7 };
    for (int i = 0; i < 20; ++i) {
        populate_room(m, reg, 10000, { 0, 0, 40, 40, Rng{ std::uint32_t(100 + i) } }, g1, fits, near, a);
        populate_room(m, reg, 10000, { 0, 0, 40, 40, Rng{ std::uint32_t(100 + i) } }, g2, fits, near, b);
    }
    assert(a.size() == b.size() && !a.empty());                  // same seeds, same monsters
    int leaders = 0, fallen_groups = 0;
    for (std::size_t i = 0; i < a.size(); ++i) {
        const auto& s = a[i];
        assert(s.x == b[i].x && s.y == b[i].y && s.type == b[i].type);
        assert(s.x >= 0 && s.y >= 0 && s.x < 40 && s.y < 40 && s.x != 20);
        if (s.leader != int(i)) continue;
        ++leaders;
        assert(!near(s.x, s.y));
        std::size_t n = 1;
        while (i + n < a.size() && a[i + n].leader == int(i)) ++n;
        if (s.type == 1) { ++fallen_groups; assert(n >= 1 && n <= 4); }   // leader + party 2..3 (fewer if crowded)
        else assert(n <= 2);                                               // zombies: 1..2
    }
    std::printf("%zu monsters in %d groups (%d fallen) over 20 rooms\n", a.size(), leaders, fallen_groups);
    assert(leaders >= 5 && fallen_groups > 0);

    // Density 0: nobody.
    std::vector<Spawn> none;
    populate_room(m, reg, 0, { 0, 0, 40, 40, Rng{ 1 } }, g1, fits, near, none);
    assert(none.empty());

    // Stats: zombie level 1 in normal = 7 HP x 101..181 %.
    Rng r{ 3 };
    for (int i = 0; i < 50; ++i) {
        const auto s = monster_stats(m, 0, 0, r);
        assert(s.level == 1 && s.hp >= 7 && s.hp <= 12);
        assert(s.ac == 5 && s.th == 8 && s.a1_min == 1 && s.a1_max == 3 && s.exp == 33);
    }
    // Combat.
    assert(hit_chance(100, 100, 1, 1) == 50);
    assert(hit_chance(1000, 1, 5, 1) == 95 && hit_chance(10, 1000, 1, 1) == 5);
    assert(kill_exp(100, 1, 1) == 100 && kill_exp(100, 10, 1) == 24 && kill_exp(100, 20, 1) == 5);
    assert(kill_exp(100, 1, 10) == 10);
    Tables t;
    t.item_base["hax"] = { .mindam = 3, .maxdam = 6, .str_bonus = 100 };
    d2d::d2s::Item hax; hax.code = "hax"; hax.location = 1; hax.slot = 4;
    d2d::d2s::Stats st;
    st.v[d2d::d2s::kStr] = 20; st.v[d2d::d2s::kDex] = 20; st.v[d2d::d2s::kLevel] = 1;
    auto at = player_attack(t, { hax }, st, 15);
    assert(at.min == 3 && at.max == 7 && at.ar == 80);                // x1.2 from strength; 20 dex = 65 + 15
    hax.props.push_back({ .stat = 17, .value = 100 });               // +100% enhanced damage
    at = player_attack(t, { hax }, st, 15);
    assert(at.min == 7 && at.max == 14);
    at = player_attack(t, {}, st, 15);                                 // fists: 1-2, strength x1.2
    assert(at.min == 1 && at.max == 2);
    ClassGains g{ .life_per_level = 8, .stamina_per_level = 4, .mana_per_level = 6, .stat_per_level = 5 };
    st.v[d2d::d2s::kMaxLife] = st.v[d2d::d2s::kLife] = 50 << 8;
    const std::vector<std::int64_t> next{ 0, 500, 1500, 3750 };
    assert(gain_exp(st, 400, next, g) == 0 && st.get(d2d::d2s::kLevel) == 1);
    assert(gain_exp(st, 1200, next, g) == 2);                          // 1600: past 500 and 1500
    assert(st.get(d2d::d2s::kLevel) == 3 && st.get(d2d::d2s::kStatPts) == 10 && st.get(d2d::d2s::kSkillPts) == 2);
    assert(st.fixed(d2d::d2s::kMaxLife) == 54 && st.fixed(d2d::d2s::kLife) == 54);   // 2 levels x 8 quarters
    // Drops.
    Tables d;
    d.item_info["hax"] = { .kind = 2, .type = "axe" };
    d.item_info["rin"] = { .kind = 0, .type = "ring" };
    d.item_info["hp1"] = { .kind = 0, .type = "hpot" };
    d.types["ring"].always_magic = true;
    d.item_base["hax"] = { .level = 1, .normcode = "hax" };
    d.item_rarity = { { "hax", 3 }, { "big", 1 } };
    d.quality_ratio[0] = { { { 400, 1, 6400 }, { 160, 2, 5600 }, { 100, 2, 3200 }, { 34, 3, 192 }, { 12, 8, 0 }, { 2, 2, 0 } } };
    d2d::rules::add_auto_treasure(d, { { "hax", 1 }, { "big", 5 } }, {});
    assert(d.treasure.at("weap3").items.size() == 1 && d.treasure.at("weap6").items[0].first == "big");
    d.treasure["Gold"] = { .items = { { "gld", 1 } } };
    d.treasure["Rich"] = { .items = { { "gld,mul=1280", 1 } } };
    d.treasure["Half"] = { .nodrop = 1, .items = { { "weap3", 1 } } };
    d.treasure["Two"] = { .picks = 2, .items = { { "rin", 1 }, { "hp1", 1 } } };
    Rng dr{ 11 };
    int dropped = 0, magic = 0;
    for (int i = 0; i < 1000; ++i) {
        std::vector<Drop> out;
        roll_drops(d, "Gold", 1, dr, out);
        assert(out.size() == 1 && out[0].code == "gld" && out[0].gold >= 1 && out[0].gold <= 8);
        out.clear();
        roll_drops(d, "Rich", 2, dr, out);
        assert(out[0].gold >= 10 && out[0].gold <= 85);                  // (2 + 0..15) x 5
        out.clear();
        roll_drops(d, "Half", 1, dr, out);
        for (const auto& x : out) { assert(x.code == "hax" && x.quality >= 2 && x.quality <= 7); ++dropped; magic += x.quality >= 4; }
        out.clear();
        roll_drops(d, "Two", 1, dr, out);
        assert(out.size() == 2);
        for (const auto& x : out) assert(x.code == "rin" ? x.quality >= 4 : x.quality == 2);   // rings magic+, potions plain
    }
    // The merc: level from experience, stats from its band.
    Tables mt;
    mt.hirelings = { { .id = 1, .level = 3, .exp_per_level = 100, .hp = 100, .hp_per_level = 10, .def = 10, .def_per_level = 2,
                       .dmg_min = 2, .dmg_max = 5, .dmg_per_level = 8, .ar = 20, .ar_per_level = 5 },
                     { .id = 1, .level = 20, .exp_per_level = 100, .hp = 500, .dmg_min = 10, .dmg_max = 20 } };
    auto ms = merc_stats(mt, 1, 5 * 100 * 4 * 4);                    // level 4: 1600; level 5 needs 3000
    assert(ms.level == 4 && ms.life == 110 && ms.def == 12 && ms.dmg_min == 3 && ms.dmg_max == 6 && ms.ar == 25);
    ms = merc_stats(mt, 1, 21u * 100 * 20 * 20);                      // level 20: the second band
    assert(ms.level == 20 && ms.life == 500 && ms.dmg_min == 10);
    std::printf("half drops: %d of 1000, %d magic or better\n", dropped, magic);
    assert(dropped > 400 && dropped < 600 && magic > 0 && magic < dropped / 2);
    std::puts("ok");
}
