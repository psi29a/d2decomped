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
    std::puts("ok");
}
