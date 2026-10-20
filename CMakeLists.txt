cmake_minimum_required(VERSION 3.24)
project(ChessGUIForUCI LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

# =============================================================================
# --- SFML 3 ---
# Default: build a pinned SFML statically from source on every platform, so the
# binaries are self-contained and every OS runs the same SFML.
#
# macOS: SFML 3's backend hard-codes highDpi:NO for windowed windows
# (SFWindowController.mm), so windowed GL surfaces are 1x and Retina Macs get
# a blurry compositor upscale no matter what Info.plist says. Until that's
# fixed upstream (SFML issue #353), the macOS build flips the flag. SFML's
# point-based public API is preserved, so app code is unaffected.
#
# -DCHESSGUI_USE_SYSTEM_SFML=ON uses an installed SFML 3 instead (no Retina
# patch on macOS).
# =============================================================================
option(CHESSGUI_USE_SYSTEM_SFML "Use an installed SFML 3 instead of building one" OFF)
option(CHESSGUI_BUILD_GUI "Build the SFML GUI (OFF = core, CLI and tests only)" ON)
set(CHESSGUI_SFML_TAG 3.0.2)

if(MSVC)
    # Static C++ runtime: the Windows build then needs no redistributable.
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

if(CHESSGUI_BUILD_GUI)
    if(CHESSGUI_USE_SYSTEM_SFML)
        find_package(SFML 3 CONFIG REQUIRED COMPONENTS Graphics Window System)
        message(STATUS "Using system SFML ${SFML_VERSION}")
    else()
        include(FetchContent)
        set(BUILD_SHARED_LIBS OFF)
        set(SFML_BUILD_AUDIO    OFF CACHE BOOL "" FORCE)
        set(SFML_BUILD_NETWORK  OFF CACHE BOOL "" FORCE)
        set(SFML_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
        set(SFML_BUILD_DOC      OFF CACHE BOOL "" FORCE)
        set(SFML_BUILD_TEST_SUITE OFF CACHE BOOL "" FORCE)
        if(MSVC)
            set(SFML_USE_STATIC_STD_LIBS ON CACHE BOOL "" FORCE)
        endif()
        set(SFML_PATCH)
        if(APPLE)
            set(SFML_PATCH PATCH_COMMAND sed -i.orig
                -e "/m_highDpi *= *NO/s/NO/YES/"
                -e "s/highDpi:NO/highDpi:YES/"
                src/SFML/Window/macOS/SFWindowController.mm)
        endif()
        FetchContent_Declare(SFML
            GIT_REPOSITORY https://github.com/SFML/SFML.git
            GIT_TAG        ${CHESSGUI_SFML_TAG}
            GIT_SHALLOW    TRUE
            ${SFML_PATCH}
            UPDATE_DISCONNECTED TRUE)
        FetchContent_MakeAvailable(SFML)
        if(APPLE)
            message(STATUS "Using vendored SFML ${CHESSGUI_SFML_TAG} with windowed-Retina patch")
        else()
            message(STATUS "Using vendored SFML ${CHESSGUI_SFML_TAG}")
        endif()
    endif()
    set(SFML_LIBS SFML::Graphics SFML::Window SFML::System)
endif()

find_package(Threads REQUIRED)

# nlohmann/json (MIT, header-only) for engines.json, settings and exports.
include(FetchContent)
FetchContent_Declare(nlohmann_json
    URL      https://github.com/nlohmann/json/releases/download/v3.12.0/json.tar.xz
    URL_HASH SHA256=42f6e95cad6ec532fd372391373363b62a14af6d771056dbfc86160e6dfff7aa
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
FetchContent_MakeAvailable(nlohmann_json)

# Core logic: rules, UCI process wrapper, matches, test tools. No SFML here;
# it is shared by the GUI, the tests and the headless CLI.
add_library(chesscore STATIC
    src/chess.cpp
    src/engine_config.cpp
    src/uci_engine.cpp
    src/testing.cpp
    src/tournament.cpp
)
target_include_directories(chesscore PUBLIC src)
target_link_libraries(chesscore PUBLIC Threads::Threads nlohmann_json::nlohmann_json)

if(CHESSGUI_BUILD_GUI)
set(CHESS_SOURCES
    src/main.cpp
    src/gui.cpp
)

add_executable(chess-gui ${CHESS_SOURCES})
target_link_libraries(chess-gui PRIVATE chesscore ${SFML_LIBS})

# Copy Resources next to the binary so ./chess-gui finds fonts, pieces,
# engines and the perft suite.
add_custom_command(TARGET chess-gui POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_SOURCE_DIR}/Resources $<TARGET_FILE_DIR:chess-gui>/Resources)

# =============================================================================
# macOS Retina (HiDPI)
# Without NSHighResolutionCapable, macOS renders the whole window at 1x and
# upscales it, so EVERYTHING looks blurry. A loose Info.plist file next to the
# binary does NOTHING - the plist must either be linked INTO the executable
# (path A) or live inside a real .app bundle (path B). Both are built here.
# =============================================================================
if(APPLE)
    # Path A: embed Info.plist into the plain ./chess-gui binary.
    # Verify after building:  otool -P ./chess-gui   -> must print the XML.
    target_link_options(chess-gui PRIVATE
        "LINKER:-sectcreate,__TEXT,__info_plist,${CMAKE_SOURCE_DIR}/Info.plist")

    # Path B (guaranteed): a real bundle -> "Chess GUI for UCI.app".
    add_executable(chess-gui-app MACOSX_BUNDLE ${CHESS_SOURCES})
    target_link_libraries(chess-gui-app PRIVATE chesscore ${SFML_LIBS})
    set_target_properties(chess-gui-app PROPERTIES
        OUTPUT_NAME "Chess GUI for UCI"
        MACOSX_BUNDLE_BUNDLE_NAME "Chess GUI for UCI"
        MACOSX_BUNDLE_GUI_IDENTIFIER "org.chessgui.uci"
        MACOSX_BUNDLE_INFO_PLIST "${CMAKE_SOURCE_DIR}/BundleInfo.plist.in")

    # The app chdirs to its own executable dir at startup, so copying
    # Resources next to the binary (Contents/MacOS/) makes it self-contained.
    add_custom_command(TARGET chess-gui-app POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${CMAKE_SOURCE_DIR}/Resources
            "$<TARGET_FILE_DIR:chess-gui-app>/Resources"
        COMMENT "Copying Resources into the app bundle...")
endif()
endif() # CHESSGUI_BUILD_GUI

# =============================================================================
# Tests (ctest --test-dir build). No network and no real engine needed: UCI
# behaviour is exercised against tests/mock_engine.cpp.
# =============================================================================
include(CTest)
if(BUILD_TESTING)
    add_subdirectory(tests)
endif()
