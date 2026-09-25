// Drops over hand-made tables: auto weapN classes, gold (and its
// multiplier), NoDrop odds, quality rolls (rings at least magic, potions
// plain).
#include <drops.hpp>

#include <cassert>
#include <cstdio>

using namespace d2d::rules;

int main() {
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
    std::printf("half drops: %d of 1000, %d magic or better\n", dropped, magic);
    std::puts("ok");
}
