# The game: d2d's server side (levels, units, the fight, the 25 Hz
# tick, the protocol). No SDL, sound, pixels or fonts: the client
# (apps/d2d) draws what it replicates. docs/PLAN.md "Where code goes".
add_library(game STATIC world.cpp gamedata.cpp)
target_include_directories(game PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(game PUBLIC mpq d2s rules drlg txt userdir cof compcode ds1 dt1 tbl)
target_compile_features(game PUBLIC cxx_std_${CMAKE_CXX_STANDARD})
