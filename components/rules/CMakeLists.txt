# SPDX-License-Identifier: GPL-3.0-or-later
# The game rules (store, prices, grid placement, combat, ...) over excel
# tables. Function bodies live in the .cpp beside each header, so an edit to
# one recompiles that file, not every user of the header; constexpr,
# template and one-line functions stay in the headers.
add_library(rules STATIC
    # One a line, in name order: a new file goes where it sorts.
    ai/merc.cpp
    ai/pets.cpp
    ai/shadows.cpp
    ai/thinks.cpp
    ai/town_npcs.cpp
    combat/aura_filter.cpp
    combat/combat.cpp
    combat/melee_reach.cpp
    combat/missiles.cpp
    combat/sequences.cpp
    combat/shatter.cpp
    combat/skills.cpp
    combat/state_death.cpp
    items/ammo.cpp
    items/classic_mod.cpp
    items/cube.cpp
    items/drops.cpp
    items/replenish.cpp
    items/set_list.cpp
    items/shop_action.cpp
    items/stack_qol.cpp
    monsters/mon_equip.cpp
    monsters/monsters.cpp
    monsters/uniques.cpp
    quests/cairn_effect.cpp
    quests/quest_music.cpp
    quests/quests.cpp
    quests/quests_act4.cpp
    quests/quests_act5.cpp
    quests/stone_scroll.cpp
    rules.cpp
    sound/object_sounds.cpp
    sound/sound_ids.cpp
    sound/sound_mix.cpp
    sound/sound_start.cpp
    ui/automap_cells.cpp
    ui/automap_miniature.cpp
    ui/chat_commands.cpp
    ui/hire_list.cpp
    ui/hud_glide.cpp
    ui/overlays.cpp
    ui/show_items.cpp
    world/ambient_dip.cpp
    world/click_spot.cpp
    world/light.cpp
    world/shake.cpp
    world/shrines.cpp
    world/weather.cpp)
# By subject, each directory on the include path: a file includes <skills.hpp>, not its path.
target_include_directories(rules PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/ai
    ${CMAKE_CURRENT_SOURCE_DIR}/combat
    ${CMAKE_CURRENT_SOURCE_DIR}/items
    ${CMAKE_CURRENT_SOURCE_DIR}/monsters
    ${CMAKE_CURRENT_SOURCE_DIR}/quests
    ${CMAKE_CURRENT_SOURCE_DIR}/sound
    ${CMAKE_CURRENT_SOURCE_DIR}/ui
    ${CMAKE_CURRENT_SOURCE_DIR}/world)
target_link_libraries(rules PUBLIC d2s)
target_compile_features(rules PUBLIC cxx_std_${CMAKE_CXX_STANDARD})
