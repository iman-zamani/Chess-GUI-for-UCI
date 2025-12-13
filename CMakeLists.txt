cmake_minimum_required(VERSION 3.10)
project(chess-gui LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(APPLE)
  list(PREPEND CMAKE_PREFIX_PATH "/opt/homebrew/opt/sfml@2")
endif()

find_package(SFML 2.5 CONFIG REQUIRED
    COMPONENTS graphics window system
)

add_executable(chess-gui
    src/main.cpp
    src/board.cpp
    src/piece.cpp
)
if(TARGET SFML::Graphics)
  target_link_libraries(chess-gui PRIVATE SFML::Graphics SFML::Window SFML::System)
elseif(TARGET sfml-graphics)
  target_link_libraries(chess-gui PRIVATE sfml-graphics sfml-window sfml-system)
else()
  message(FATAL_ERROR "SFML targets not found. Check SFML CMake config.")
endif()
