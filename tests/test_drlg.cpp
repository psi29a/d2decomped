// Act 1's level layout: for many seeds, every level clear of the others
// and touching its link, the town where game.exe allows it, reproducible.
#include <drlg.hpp>

#include <cassert>
#include <cstdio>

using namespace d2d::drlg;

int main() {
    const LevelDefs defs = {
        { 1, { 56, 40 } }, { 2, { 80, 80, 0, 0, true } }, { 3, { 80, 80, 0, 0, true } },
        { 4, { 80, 80, 1000, 1000, true } }, { 17, { 40, 48, 0, 0, true } },
        { 39, { 80, 80, 0, 0, true } }, { 26, { 64, 18, 1000, 1000 } },
        { 5, { 80, 80, 0, 0, true } }, { 6, { 80, 80, 0, 0, true } }, { 7, { 80, 80, 0, 0, true } } };
    auto touching = [](const Placed& first, const Placed& second) { return apart(first, second, 0) && !apart(first, second, 1); };
    int files[4] = {};
    for (std::uint32_t seed = 1; seed < 400; ++seed) {
        const auto chain = place_chain(kAct1Outdoors, defs, d2d::rules::Rng{ seed }, true);
        assert(chain.size() == kAct1Outdoors.size());
        for (std::size_t i = 0; i < chain.size(); ++i) {
            const int link = kAct1Outdoors[i].link;
            for (std::size_t j = 0; j < i; ++j)
                if (int(j) != link) assert(apart(chain[i], chain[j]));
            if (link >= 0) assert(touching(chain[i], chain[std::size_t(link)]));
        }
        const auto& blood_moor = chain[2];
        const auto& town = chain[3];
        assert(blood_moor.level == 2 && (blood_moor.width == 56 || blood_moor.width == 96) && blood_moor.width + blood_moor.height == 152);
        assert(kTownAllowed[std::size_t(town.dir + 4 * town.flip + 8 * blood_moor.dir + 32 * blood_moor.flip)]);
        const int town_file_index = town_file(chain);
        assert(town_file_index >= 0 && town_file_index < 4);
        ++files[town_file_index];
        // Same seed, same act.
        const auto again = place_chain(kAct1Outdoors, defs, d2d::rules::Rng{ seed }, true);
        for (std::size_t i = 0; i < chain.size(); ++i) assert(again[i].x == chain[i].x && again[i].y == chain[i].y);
    }
    std::printf("town files N/E/S/W: %d %d %d %d\n", files[0], files[1], files[2], files[3]);
    assert(files[0] + files[1] + files[2] + files[3] == 399);
    std::puts("test_drlg: ok");
}
