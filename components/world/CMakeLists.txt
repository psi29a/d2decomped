# The World: d2d's game server side (levels, units, the fight, the 25 Hz
# tick, the protocol). No SDL, sound, pixels or fonts: the client
# (apps/d2d) draws what it replicates. docs/PLAN.md "Where code goes".
add_library(world STATIC world.cpp gamedata.cpp)
target_include_directories(world PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(world PUBLIC mpq d2s rules drlg txt userdir cof compcode ds1 dt1 tbl)
target_compile_features(world PUBLIC cxx_std_${CMAKE_CXX_STANDARD})
