// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: rules
// The Cube's recipes (rules/cube.hpp): compiled, matched, made.
#include <cube.hpp>
#include <d2s_items.hpp>
#include <rules.hpp>

#include <cassert>
#include <cstdio>
#include <vector>

int main() {
    using namespace d2d::rules;
    Tables tables;
    for (const char* code : { "msf", "vip", "hst", "gcr", "gfr" }) {
        tables.item_base[code] = { .cost = 1 };
        tables.item_info[code] = { .type = code[0] == 'g' ? "gem" : "ques" };
    }
    tables.types["gem"] = {};
    tables.types["ques"] = {};

    // The staff: two items in, one out; a third item (the count) or a
    // wrong one doesn't take.
    CubeRecipe staff{ .enabled = true, .numinputs = 2 };
    staff.inputs[0] = compile_cube_input(tables, "msf");
    staff.inputs[1] = compile_cube_input(tables, "vip");
    staff.outputs[0] = compile_cube_output(tables, "hst");
    assert(staff.inputs[0].flags == cube_in::kItem && staff.inputs[0].code == "msf" && staff.outputs[0].kind == cube_out::kCode);
    d2d::d2s::Item shaft{ .code = "msf", .ilvl = 20 }, amulet{ .code = "vip", .ilvl = 20 }, ruby{ .code = "gcr", .ilvl = 5 };
    const std::vector<CubeRecipe> recipes{ staff };
    CubeMatch match;
    assert(cube_recipe(tables, recipes, { &shaft, &amulet }, 0, true, 0, match) == 0);
    assert(cube_recipe(tables, recipes, { &shaft, &amulet, &ruby }, 0, true, 0, match) < 0);
    assert(cube_recipe(tables, recipes, { &shaft, &ruby }, 0, true, 0, match) < 0);
    cube_recipe(tables, recipes, { &shaft, &amulet }, 0, true, 0, match);
    Rng game{ 1 };
    const auto made = cube_make(tables, staff, match, { &shaft, &amulet }, 10, true, 0, game, nullptr);
    assert(made.made && made.items.size() == 1 && made.items[0].code == "hst");

    // qty= and a type: three chipped rubies, exactly.
    CubeRecipe gems{ .enabled = true, .numinputs = 3 };
    gems.inputs[0] = compile_cube_input(tables, "\"gcr,qty=3\"");
    gems.outputs[0] = compile_cube_output(tables, "gfr");
    assert(gems.inputs[0].qty == 3);
    const std::vector<CubeRecipe> gem_recipes{ gems };
    assert(cube_recipe(tables, gem_recipes, { &ruby, &ruby, &ruby }, 0, true, 0, match) == 0);
    assert(cube_recipe(tables, gem_recipes, { &ruby, &ruby }, 0, true, 0, match) < 0);
    CubeRecipe typed{ .enabled = true, .numinputs = 1 };
    typed.inputs[0] = compile_cube_input(tables, "gem");
    assert(typed.inputs[0].flags == cube_in::kType);
    assert(cube_match(tables, typed, { &ruby }, 0, true) && !cube_match(tables, typed, { &shaft }, 0, true));
    // An output's words: useitem with sockets, a quality and mods' flags.
    const auto socketed = compile_cube_output(tables, "\"useitem,sock=1\"");
    assert(socketed.kind == cube_out::kUseItem && (socketed.flags & cube_out::kSock) && socketed.qty == 1);
    const auto crafted = compile_cube_output(tables, "\"usetype,crf,pre=5,rep\"");
    assert(crafted.kind == cube_out::kUseType && crafted.quality == 8 && crafted.prefixes.size() == 1 && (crafted.flags & cube_out::kRepair));
    std::puts("test_cube: ok");
}
