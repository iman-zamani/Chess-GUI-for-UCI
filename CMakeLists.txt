cmake_minimum_required(VERSION 3.15)
project(chess-gui LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# =============================================================================
# 1. PLATFORM SPECIFIC SETUP
# =============================================================================
if(APPLE)
    set(POSSIBLE_SFML2_PATHS
        "/opt/homebrew/opt/sfml@2"  # Apple Silicon default
        "/usr/local/opt/sfml@2"     # Intel Mac default
    )
    foreach(PATH ${POSSIBLE_SFML2_PATHS})
        if(EXISTS "${PATH}")
            message(STATUS "Detected Homebrew SFML 2.x path: ${PATH}")
            list(PREPEND CMAKE_PREFIX_PATH "${PATH}")
        endif()
    endforeach()
endif()

# =============================================================================
# 2. FIND DEPENDENCIES
# =============================================================================
find_package(SFML 2.5 CONFIG REQUIRED
    COMPONENTS graphics window system
)
find_package(Threads REQUIRED)          # <-- NEW: engine/tournament/test threads

# =============================================================================
# 3. BUILD TARGETS
# =============================================================================
add_executable(chess-gui
    src/main.cpp
    src/chess.cpp                       # <-- NEW source list
    src/uci_engine.cpp
    src/testing.cpp
    src/tournament.cpp
    src/gui.cpp
)
# board.cpp / piece.cpp intentionally removed — superseded by gui.cpp/chess.cpp

if(TARGET SFML::Graphics)
    target_link_libraries(chess-gui PRIVATE SFML::Graphics SFML::Window SFML::System)
elseif(TARGET sfml-graphics)
    target_link_libraries(chess-gui PRIVATE sfml-graphics sfml-window sfml-system)
else()
    message(FATAL_ERROR "SFML found but targets are missing. Check installation.")
endif()

target_link_libraries(chess-gui PRIVATE Threads::Threads)   # <-- NEW

# =============================================================================
# 4. RESOURCE HANDLING (unchanged)
# =============================================================================
file(GLOB_RECURSE RESOURCE_FILES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/Resources/*")

add_custom_command(
    OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/Resources.stamp"
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_CURRENT_SOURCE_DIR}/Resources"
        "${CMAKE_CURRENT_BINARY_DIR}/Resources"
    COMMAND ${CMAKE_COMMAND} -E touch "${CMAKE_CURRENT_BINARY_DIR}/Resources.stamp"
    DEPENDS ${RESOURCE_FILES}
    COMMENT "Syncing Resources directory to build folder..."
)
add_custom_target(SyncResources DEPENDS "${CMAKE_CURRENT_BINARY_DIR}/Resources.stamp")
add_dependencies(chess-gui SyncResources)
