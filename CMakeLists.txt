cmake_minimum_required(VERSION 3.16)
project(ChessGUIForUCI LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

# --- SFML 2.x ---
# On macOS with Homebrew SFML 3 is default; point CMake at sfml@2 if needed:
#   brew install sfml@2
if(APPLE)
    list(APPEND CMAKE_PREFIX_PATH "/opt/homebrew/opt/sfml@2" "/usr/local/opt/sfml@2")
endif()
find_package(SFML 2.5 COMPONENTS graphics window system REQUIRED)
find_package(Threads REQUIRED)

add_executable(chess-gui
    src/main.cpp
    src/chess.cpp
    src/uci_engine.cpp
    src/testing.cpp
    src/tournament.cpp
    src/gui.cpp
)
target_include_directories(chess-gui PRIVATE src)
target_link_libraries(chess-gui PRIVATE
    sfml-graphics sfml-window sfml-system Threads::Threads)

if(WIN32)
    target_link_libraries(chess-gui PRIVATE ws2_32)
endif()

# Copy Resources next to the binary so ./chess-gui finds fonts, pieces,
# engines and the perft suite.
add_custom_command(TARGET chess-gui POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_SOURCE_DIR}/Resources $<TARGET_FILE_DIR:chess-gui>/Resources)
