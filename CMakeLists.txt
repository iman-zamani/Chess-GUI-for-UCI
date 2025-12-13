cmake_minimum_required(VERSION 3.15) # Bumped slightly for better path handling features
project(chess-gui LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# =============================================================================
# 1. PLATFORM SPECIFIC SETUP
# =============================================================================

# --- MACOS FIX: Prioritize SFML 2.x (Homebrew) ---
if(APPLE)
    # Homebrew installs SFML 3 by default now. SFML 2 is "keg-only" (hidden).
    # We must explicitly add the sfml@2 path to CMAKE_PREFIX_PATH so CMake finds it first.
    
    set(POSSIBLE_SFML2_PATHS
        "/opt/homebrew/opt/sfml@2"  # Apple Silicon default
        "/usr/local/opt/sfml@2"     # Intel Mac default
    )

    foreach(PATH ${POSSIBLE_SFML2_PATHS})
        if(EXISTS "${PATH}")
            message(STATUS "Detected Homebrew SFML 2.x path: ${PATH}")
            # Prepend to ensuring we look here BEFORE system paths (where SFML 3 might be)
            list(PREPEND CMAKE_PREFIX_PATH "${PATH}")
        endif()
    endforeach()
endif()

# =============================================================================
# 2. FIND DEPENDENCIES
# =============================================================================

# We request version 2.5. 
# Since SFML 3 is not backward compatible, CMake will automatically reject 
# SFML 3.x installations even if found, provided we help it find the 2.x version above.
find_package(SFML 2.5 CONFIG REQUIRED
    COMPONENTS graphics window system
)

# =============================================================================
# 3. BUILD TARGETS
# =============================================================================

add_executable(chess-gui
    src/main.cpp
    src/board.cpp
    src/piece.cpp
)

# Link against the found SFML libraries
if(TARGET SFML::Graphics)
    # Standard Modern CMake target names (SFML 2.5+)
    target_link_libraries(chess-gui PRIVATE SFML::Graphics SFML::Window SFML::System)
elseif(TARGET sfml-graphics)
    # Legacy target names (sometimes used in older Linux packages)
    target_link_libraries(chess-gui PRIVATE sfml-graphics sfml-window sfml-system)
else()
    message(FATAL_ERROR "SFML found but targets are missing. Check installation.")
endif()