# Qt6 launcher app. Handles installation (from ISOs and/or an existing D2
# folder) today; will handle profile selection + game launch once the engine
# can run. find_package picks up Homebrew qt6, Ubuntu qt6-base-dev, and the
# official Qt SDK on Windows.

find_package(Qt6 6.5 COMPONENTS Widgets Network REQUIRED)

set(CMAKE_AUTOMOC ON)

# --- version stamp (git describe → config.hpp) ------------------------------
find_package(Git QUIET)
set(D2LAUNCHER_VERSION "0.1.0-dev")
set(D2LAUNCHER_COMMIT  "unknown")
if(GIT_FOUND)
    execute_process(
        COMMAND ${GIT_EXECUTABLE} describe --tags --always --dirty
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        OUTPUT_VARIABLE _tmp
        OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
    if(_tmp)
        set(D2LAUNCHER_VERSION "${_tmp}")
    endif()
    execute_process(
        COMMAND ${GIT_EXECUTABLE} rev-parse --short HEAD
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        OUTPUT_VARIABLE _tmp
        OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
    if(_tmp)
        set(D2LAUNCHER_COMMIT "${_tmp}")
    endif()
endif()

# Point at a GitHub Releases API URL (or similar) when we ship releases;
# empty = launcher does no update check.
set(D2LAUNCHER_UPDATE_URL "" CACHE STRING
    "Optional URL returning JSON with 'tag_name' for update check")
set(D2_PATCH_URL "" CACHE STRING
    "Optional URL to fetch a Blizzard patch installer from")

configure_file(config.hpp.in ${CMAKE_CURRENT_BINARY_DIR}/config.hpp @ONLY)

add_executable(d2d-launcher WIN32 MACOSX_BUNDLE
    main.cpp
)

target_include_directories(d2d-launcher PRIVATE ${CMAKE_CURRENT_BINARY_DIR})

target_link_libraries(d2d-launcher PRIVATE
    Qt6::Widgets
    Qt6::Network
    iso9660
    mpq
    install_detect
    userdir
)
