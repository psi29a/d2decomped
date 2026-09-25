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
    // The fighter: weapon damage with enhanced damage and the strength
    // bonus, attack rating, shield block, the rest straight from the sums.
    Tables t;
    t.item_base["hax"] = { .mindam = 3, .maxdam = 6, .str_bonus = 100, .speed = -10 };
    t.item_base["buc"] = { .block = 25 };
    d2d::d2s::Item hax; hax.code = "hax";
    d2d::d2s::Item buc; buc.code = "buc";
    d2d::d2s::Stats st;
    st.v[d2d::d2s::kStr] = 20; st.v[d2d::d2s::kDex] = 20; st.v[d2d::d2s::kLevel] = 1;
    StatSum sum{}, wsum{};
    auto f = make_fighter(t, &hax, nullptr, sum, wsum, st, { .to_hit = 15, .block = 25 }, 50, { 1, 2, 3, 4 });
    assert(f.min == 3 && f.max == 7 && f.ar == 80 && f.block == 0 && f.wsm == -10 && f.defense == 50 && f.res[3] == 4);
    sum[17] = sum[18] = 100;                                          // enhanced damage off the weapon: no effect (op 13)
    assert(make_fighter(t, &hax, nullptr, sum, wsum, st, {}, 0, {}).max == 7);
    sum[17] = sum[18] = 0;
    wsum[17] = wsum[18] = 100;                                        // +100% on the weapon
    sum[20] = 10; sum[136] = 40; sum[36] = 80; sum[34] = 3; sum[60] = 10; sum[54] = 3; sum[55] = 14; sum[56] = 50;
    sum[57] = 256; sum[58] = 512; sum[59] = 75;                         // poison: 1-2 a tick for 75 ticks
    f = make_fighter(t, &hax, &buc, sum, wsum, st, { .to_hit = 15, .block = 25 }, 50, {});
    assert(f.min == 7 && f.max == 14);                                // 6-12, +20% from strength
    assert(f.block == 75);                                            // 60 x 5 / 2 = 150: capped
    st.v[d2d::d2s::kLevel] = 10;
    assert(make_fighter(t, &hax, &buc, sum, wsum, st, { .block = 25 }, 0, {}).block == 60 * 5 / 20);   // (block) x (dex - 15) / (clvl x 2)
    st.v[d2d::d2s::kLevel] = 1;
    assert(f.crushing == 40 && f.dr_pct == 50 && f.dr_flat == 3 && f.life_steal == 10);   // DR% caps at 50
    assert(f.elem[2] == std::pair(3, 14) && f.cold_len == 50 && f.elem[3] == std::pair(75, 150));
    StatSum s25{};
    s25[25] = 50;                                                     // damagepercent joins the strength bonus: +70%
    f = make_fighter(t, &hax, nullptr, s25, StatSum{}, st, {}, 0, {});
    assert(f.min == 5 && f.max == 10);
    s25[25] = 0; s25[111] = 2;                                        // "+2 damage": both ends, before the bonus
    f = make_fighter(t, &hax, nullptr, s25, StatSum{}, st, {}, 0, {});
    assert(f.min == 6 && f.max == 9);
    f = make_fighter(t, nullptr, nullptr, StatSum{}, StatSum{}, st, {}, 0, {});
    assert(f.min == 1 && f.max == 2);                                 // fists, no strength bonus
    // Hit chance rounds the percent first (FUN_0057d9b0); negative defense helps.
    assert(hit_chance(1, 2, 3, 1) == 49 && hit_chance(10, -10, 1, 1) == 95);

    // Speed breakpoints: 1.10's attack frames.
    assert(effective_speed(20) == 17 && effective_speed(0) == 0 && effective_speed(30, 150) == 25);
    assert(attack_ticks(16, 256, 0, 0) == 16);                        // no IAS: a frame a tick
    assert(attack_ticks(16, 256, 20, 0) == 14);                       // EIAS 17: ceil(4096 / 299)
    assert(attack_ticks(16, 256, 0, 20) == 21);                       // a slow weapon: ceil(4096 / 204)
    assert(open_wounds_per_sec(1) == 3 && open_wounds_per_sec(81) == 227);

    // Player blows: sure hits (AR vastly over defense), crushing blow takes a
    // quarter of what's left, resistances cut, leech per Drain.
    Fighter hit;
    hit.min = hit.max = 100; hit.ar = 1000000; hit.crushing = 100; hit.life_steal = 10;
    hit.elem[0] = { 10, 10 };
    Target tg{ .hp = 400, .max_hp = 400, .ac = 1, .level = 1, .res = { 50, 0, 100, 0, 0, 0 }, .drain = 50 };
    Rng br{ 5 };
    auto blow = player_blow(hit, tg, 99, br);
    for (int i = 0; i < 20 && !blow.hit; ++i) blow = player_blow(hit, tg, 99, br);   // 95 % to hit
    assert(blow.hit && blow.crushing && blow.damage == 50 + 0 + 50);  // 100 phys at 50%, fire immune, CB 400/4 at 50%
    assert(blow.life == 50 * 10 * 50 / 10000);
    // Critical strike doubles like deadly strike.
    Fighter crit;
    crit.min = crit.max = 10; crit.ar = 1000000; crit.critical = 100;
    Target plain{ .hp = 400, .max_hp = 400, .ac = 1, .level = 1 };
    auto cb = player_blow(crit, plain, 99, br);
    for (int i = 0; i < 20 && !cb.hit; ++i) cb = player_blow(crit, plain, 99, br);
    assert(cb.hit && cb.deadly && cb.damage == 20);
    tg.block = 100;
    int blocked = 0;
    for (int i = 0; i < 20; ++i) blocked += player_blow(hit, tg, 99, br).blocked;
    assert(blocked >= 15);                                            // every blow that connects
    // Monster blows: block, damage reduced % then flat, elemental less resistance.
    MonStats mon;
    mon.level = 99; mon.th = 1000000; mon.a1_min = mon.a1_max = 100;
    mon.el[0] = { 0, 100, 40, 40, 0, "A1" };                           // fire, always
    Fighter me;
    me.dr_pct = 20; me.dr_flat = 5; me.res[0] = 75; me.mdr = 2;
    auto k = monster_blow(me, 1, false, mon, false, br);
    for (int i = 0; i < 20 && !k.hit; ++i) k = monster_blow(me, 1, false, mon, false, br);
    assert(k.hit && k.damage == 75 + 8);                               // 100 x 80% - 5, 40 x 25% - 2
    // Dodge a swing standing, avoid a missile, evade on the move.
    Fighter agile;
    agile.dodge = 100;
    int dodged = 0, evaded = 0;
    for (int i = 0; i < 100; ++i) {
        dodged += monster_blow(agile, 1, false, mon, false, br).dodged;
        evaded += monster_blow(agile, 1, true, mon, false, br).dodged;
    }
    assert(dodged > 85 && evaded == 0);
    agile.def_missile = 1000000000;                                    // vs missiles only
    int spikes = 0;
    for (int i = 0; i < 100; ++i) spikes += monster_blow(agile, 99, false, mon, true, br).hit;
    assert(spikes < 15);
    me.block = 75;
    int blocks = 0, moving_blocks = 0;
    for (int i = 0; i < 1000; ++i) { blocks += monster_blow(me, 1, false, mon, false, br).blocked; moving_blocks += monster_blow(me, 1, true, mon, false, br).blocked; }
    assert(blocks > 700 && blocks < 800 && moving_blocks > 200 && moving_blocks < 300);
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
    // Belt potions: the bottom one goes, the column drops a row.
    Tables pt;
    pt.potions["hp1"] = { .life = 30, .ticks = 192 };
    auto belt = [](const char* code, int box) { d2d::d2s::Item i; i.code = code; i.location = 2; i.column = box; return i; };
    std::vector<d2d::d2s::Item> bi{ belt("hp1", 1), belt("hp1", 5), belt("isc", 2) };
    assert(drink_belt(pt, bi, 1) == "hp1" && bi.size() == 2 && bi[0].column == 1);
    assert(drink_belt(pt, bi, 2).empty() && drink_belt(pt, bi, 0).empty() && bi.size() == 2);   // a scroll, nothing
    std::printf("half drops: %d of 1000, %d magic or better\n", dropped, magic);
    assert(dropped > 400 && dropped < 600 && magic > 0 && magic < dropped / 2);
    std::puts("ok");
}
