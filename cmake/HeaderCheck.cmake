# SPDX-License-Identifier: GPL-3.0-or-later
# d2d_check_headers(<name> LINK <libs...>): every .hpp in the calling
# directory compiles on its own (one generated .cpp each), so a header
# includes what it uses. Built with the rest when D2D_CHECK_HEADERS is on.
option(D2D_CHECK_HEADERS "Compile each d2d/game header on its own" ON)
function(d2d_check_headers name)
    if(NOT D2D_CHECK_HEADERS)
        return()
    endif()
    cmake_parse_arguments(ARG "" "" "LINK" ${ARGN})
    file(GLOB headers CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/*.hpp")
    set(sources "")
    foreach(header IN LISTS headers)
        get_filename_component(stem "${header}" NAME_WE)
        set(source "${CMAKE_CURRENT_BINARY_DIR}/header_check/${stem}.cpp")
        file(CONFIGURE OUTPUT "${source}" CONTENT "#include \"${header}\"\n")
        list(APPEND sources "${source}")
    endforeach()
    add_library(${name} OBJECT ${sources})
    target_include_directories(${name} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    target_link_libraries(${name} PRIVATE ${ARG_LINK})
endfunction()
