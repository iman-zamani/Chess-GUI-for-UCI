cmake_minimum_required(VERSION 3.16)
project(ChessGUIForUCI LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

# =============================================================================
# --- SFML ---
# SFML 3's macOS backend hard-codes highDpi:NO for windowed windows
# (SFWindowController.mm), so windowed GL surfaces are 1x and Retina Macs get
# a blurry compositor upscale no matter what Info.plist says. Until that's
# fixed upstream (SFML issue #353), we build SFML from source on macOS with
# the flag flipped. SFML's point-based public API is preserved, so app code
# is unaffected.
# =============================================================================
option(CHESSGUI_VENDOR_SFML "Build a Retina-patched SFML from source (macOS)" ${APPLE})

if(CHESSGUI_VENDOR_SFML)
    include(FetchContent)
    # Build static libs so the macOS .app bundle is fully self-contained
    set(BUILD_SHARED_LIBS OFF)
    set(SFML_BUILD_AUDIO    OFF CACHE BOOL "" FORCE)
    set(SFML_BUILD_NETWORK  OFF CACHE BOOL "" FORCE)
    set(SFML_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(SFML_BUILD_DOC      OFF CACHE BOOL "" FORCE)

    FetchContent_Declare(SFML
        GIT_REPOSITORY https://github.com/SFML/SFML.git
        GIT_TAG        3.0.1
        GIT_SHALLOW    TRUE
        PATCH_COMMAND  sed -i.orig
            -e "/m_highDpi *= *NO/s/NO/YES/"
            -e "s/highDpi:NO/highDpi:YES/"
            src/SFML/Window/macOS/SFWindowController.mm
        UPDATE_DISCONNECTED TRUE)

    FetchContent_MakeAvailable(SFML)
    set(SFML_LIBS SFML::Graphics SFML::Window SFML::System)
    message(STATUS "Using vendored SFML 3.0.1 with windowed-Retina patch")
else()
    # Fallback for Linux/Windows: use system-installed SFML
    find_package(SFML 3 CONFIG COMPONENTS Graphics Window System QUIET)
    if(SFML_FOUND)
        message(STATUS "SFML 3 found")
        set(SFML_LIBS SFML::Graphics SFML::Window SFML::System)
    else()
        if(APPLE)
            list(APPEND CMAKE_PREFIX_PATH "/opt/homebrew/opt/sfml@2" "/usr/local/opt/sfml@2")
        endif()
        find_package(SFML 2.5 COMPONENTS graphics window system REQUIRED)
        set(SFML_LIBS sfml-graphics sfml-window sfml-system)
        if(APPLE)
            message(WARNING "Building with SFML 2: output WILL be blurry on Retina "
                            "displays (SFML 2 limitation).")
        else()
            message(STATUS "SFML 2 found (fine on Linux/Windows; SFML 3 also supported)")
        endif()
    endif()
endif()

find_package(Threads REQUIRED)

set(CHESS_SOURCES
    src/main.cpp
    src/chess.cpp
    src/uci_engine.cpp
    src/testing.cpp
    src/tournament.cpp
    src/gui.cpp
)

add_executable(chess-gui ${CHESS_SOURCES})
target_include_directories(chess-gui PRIVATE src)
target_link_libraries(chess-gui PRIVATE ${SFML_LIBS} Threads::Threads)

if(WIN32)
    target_link_libraries(chess-gui PRIVATE ws2_32)
endif()

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
    target_include_directories(chess-gui-app PRIVATE src)
    target_link_libraries(chess-gui-app PRIVATE ${SFML_LIBS} Threads::Threads)
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
