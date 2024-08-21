cmake_minimum_required(VERSION 3.10)
project(chess-gui)

# Specify the C++ standard
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED True)

# Find SFML
find_package(SFML 2.5 COMPONENTS graphics window system REQUIRED)

# Add the executable with the new name
add_executable(chess-gui src/board.cpp src/main.cpp src/piece.cpp)

# Link SFML libraries
target_link_libraries(chess-gui sfml-graphics sfml-window sfml-system)
