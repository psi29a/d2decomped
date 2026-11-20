# SPDX-License-Identifier: GPL-3.0-or-later
# The game rules (store, prices, grid placement, combat, ...) over excel
# tables. Function bodies live in the .cpp beside each header, so an edit to
# one recompiles that file, not every user of the header; constexpr,
# template and one-line functions stay in the headers.
add_library(rules STATIC
    # One a line, in name order: a new file goes where it sorts.
    ambient_dip.cpp
    ammo.cpp
    aura_filter.cpp
    automap_cells.cpp
    automap_miniature.cpp
    cairn_effect.cpp
    chat_commands.cpp
    click_spot.cpp
    combat.cpp
    cube.cpp
    drops.cpp
    hire_list.cpp
    hud_glide.cpp
    light.cpp
    melee_reach.cpp
    merc.cpp
    missiles.cpp
    mon_equip.cpp
    monsters.cpp
    object_sounds.cpp
    overlays.cpp
    pets.cpp
    quest_music.cpp
    quests.cpp
    quests_act4.cpp
    quests_act5.cpp
    replenish.cpp
    rules.cpp
    sequences.cpp
    shadows.cpp
    shake.cpp
    shatter.cpp
    shop_action.cpp
    show_items.cpp
    shrines.cpp
    skills.cpp
    sound_ids.cpp
    sound_mix.cpp
    sound_start.cpp
    stack_qol.cpp
    stone_scroll.cpp
    thinks.cpp
    town_npcs.cpp
    uniques.cpp
    weather.cpp)
target_include_directories(rules PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(rules PUBLIC d2s)
target_compile_features(rules PUBLIC cxx_std_${CMAKE_CXX_STANDARD})
