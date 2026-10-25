# SPDX-License-Identifier: GPL-3.0-or-later
# Qt6 launcher app. Handles installation (from ISOs and/or an existing D2
# folder) today; will handle profile selection + game launch once the engine
# can run. find_package picks up Homebrew qt6, Ubuntu qt6-base-dev, and the
# official Qt SDK on Windows.

find_package(Qt6 6.5 COMPONENTS Widgets Network REQUIRED)

set(CMAKE_AUTOMOC ON)

# Point at a GitHub Releases API URL (or similar) when we ship releases;
# empty = launcher does no update check.
set(D2LAUNCHER_UPDATE_URL "" CACHE STRING
    "Optional URL returning JSON with 'tag_name' for update check")
set(D2_PATCH_URL "" CACHE STRING
    "Optional URL to fetch a Blizzard patch installer from")
set(D2D_SENTRY_DSN
    "https://f1260fbca28bc7dbe3d72401699586dc@o4512265415098368.ingest.de.sentry.io/4512265453240400"
    CACHE STRING "Public Sentry client DSN")

configure_file(config.hpp.in ${CMAKE_CURRENT_BINARY_DIR}/config.hpp @ONLY)

set(icon_dir "${PROJECT_SOURCE_DIR}/assets/icon")
set_source_files_properties("${icon_dir}/d2d.icns" PROPERTIES MACOSX_PACKAGE_LOCATION Resources)
add_executable(d2d-launcher WIN32 MACOSX_BUNDLE
    main.cpp
    "${icon_dir}/d2d.icns"
    $<$<PLATFORM_ID:Windows>:${icon_dir}/d2d.rc>
)
set_target_properties(d2d-launcher PROPERTIES MACOSX_BUNDLE_ICON_FILE d2d.icns)
qt_add_resources(d2d-launcher icon PREFIX "/" BASE "${icon_dir}" FILES "${icon_dir}/d2d.png" "${icon_dir}/hero.png"
    "${icon_dir}/steam-capsule.png" "${icon_dir}/steam-wide.png" "${icon_dir}/steam-hero.png" "${icon_dir}/steam-logo.png")

target_include_directories(d2d-launcher PRIVATE ${CMAKE_CURRENT_BINARY_DIR})

target_link_libraries(d2d-launcher PRIVATE
    Qt6::Widgets
    Qt6::Network
    iso9660
    mpq
    install_detect
    userdir
    steam_shortcuts
)
