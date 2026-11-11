# SPDX-License-Identifier: GPL-3.0-or-later
# The game: d2d's server side (levels, units, the fight, the 25 Hz
# tick, the protocol). No SDL, sound, pixels or fonts: the client
# (apps/d2d) draws what it replicates. docs/PLAN.md "Where code goes".
# Each source a line, in name order: a new one goes where it sorts. A
# subsystem split over several files keeps them in its own directory
# (ai/, fight/, world/), beside its public header (fight.hpp, world.hpp). A
# family of headers (character/, gamedata/, npc/) is on the include path, so
# its headers are included by name.
add_library(game STATIC
    ai.cpp
    ai/act1.cpp
    ai/act2.cpp
    ai/act3.cpp
    ai/act4.cpp
    ai/act5.cpp
    fight/deaths.cpp
    fight/merc.cpp
    fight/missiles.cpp
    fight/pets.cpp
    fight/setup.cpp
    fight/states.cpp
    fight/strike.cpp
    fight/summons.cpp
    fight/tick.cpp
    gamedata/gamedata.cpp
    gamedata/gamedata_load.cpp
    objgroups.cpp
    world/commands.cpp
    world/objects.cpp
    world/quests/act1.cpp
    world/quests/act2.cpp
    world/quests/act3.cpp
    world/quests/act4.cpp
    world/quests/act5.cpp
    world/quests/shared.cpp
    world/setup.cpp
    world/tick.cpp
    world/travel.cpp
    world/view.cpp
)
# GCC 14 reads ev::Event's copies (a std::variant with ev::Message's strings)
# in these as maybe uninitialized: a false alarm, a warning here, not an error.
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    set_source_files_properties(world/quests/act1.cpp world/quests/act4.cpp world/quests/act5.cpp world/quests/shared.cpp world/tick.cpp
                                PROPERTIES COMPILE_OPTIONS -Wno-error=maybe-uninitialized)
endif()
target_include_directories(game PUBLIC ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/character
                                       ${CMAKE_CURRENT_SOURCE_DIR}/gamedata ${CMAKE_CURRENT_SOURCE_DIR}/npc)
target_link_libraries(game PUBLIC log mpq d2s rules drlg txt userdir install_detect cof compcode ds1 dt1 tbl)
target_compile_features(game PUBLIC cxx_std_${CMAKE_CXX_STANDARD})
d2d_check_headers(game_headers SUBDIRS character fight gamedata npc world LINK game)
