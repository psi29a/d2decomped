# SPDX-License-Identifier: GPL-3.0-or-later
# d2d_check_headers(<name> [SUBDIRS <dirs...>] LINK <libs...>): every .hpp
# in the calling directory, and in each of SUBDIRS (fight/, town/, ...),
# compiles on its own (one generated .cpp each), so a header includes what
# it uses. Built with the rest when D2D_CHECK_HEADERS is on.
option(D2D_CHECK_HEADERS "Compile each d2d/game header on its own" ON)
function(d2d_check_headers name)
    if(NOT D2D_CHECK_HEADERS)
        return()
    endif()
    cmake_parse_arguments(ARG "" "" "SUBDIRS;LINK" ${ARGN})
    set(globs "${CMAKE_CURRENT_SOURCE_DIR}/*.hpp")
    foreach(subdir IN LISTS ARG_SUBDIRS)
        list(APPEND globs "${CMAKE_CURRENT_SOURCE_DIR}/${subdir}/*.hpp")
    endforeach()
    file(GLOB headers CONFIGURE_DEPENDS ${globs})
    set(sources "")
    foreach(header IN LISTS headers)
        # fight/internal.hpp -> fight_internal.cpp: two subdirs' same-named headers stay apart
        file(RELATIVE_PATH stem "${CMAKE_CURRENT_SOURCE_DIR}" "${header}")
        string(REGEX REPLACE "\\.hpp$" "" stem "${stem}")
        string(REPLACE "/" "_" stem "${stem}")
        set(source "${CMAKE_CURRENT_BINARY_DIR}/header_check/${stem}.cpp")
        file(CONFIGURE OUTPUT "${source}" CONTENT "#include \"${header}\"\n")
        list(APPEND sources "${source}")
    endforeach()
    add_library(${name} OBJECT ${sources})
    target_include_directories(${name} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    target_link_libraries(${name} PRIVATE ${ARG_LINK})
endfunction()
