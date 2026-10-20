add_executable(mock_engine mock_engine.cpp)
target_link_libraries(mock_engine PRIVATE chesscore)

add_executable(core_tests
    test_chess.cpp
    test_uci.cpp
    test_engine.cpp
    test_perft_bisect.cpp
    test_uci_check.cpp
    test_stats.cpp
    test_openings.cpp
    test_main.cpp
)
target_link_libraries(core_tests PRIVATE chesscore)
target_compile_definitions(core_tests PRIVATE
    CHESSGUI_SOURCE_DIR="${PROJECT_SOURCE_DIR}"
    MOCK_ENGINE_PATH="$<TARGET_FILE:mock_engine>")
add_dependencies(core_tests mock_engine)

# One CTest entry per test group (name prefix understood by the runner).
foreach(group perft fen san pgn game uci engine bisect ucicheck stats openings)
    add_test(NAME ${group} COMMAND core_tests ${group}/)
endforeach()
set_tests_properties(perft PROPERTIES TIMEOUT 600)

# Scripted GUI smoke test. Needs a display (CI runs it under Xvfb on Linux).
option(CHESSGUI_GUI_TESTS "Register the scripted GUI smoke test with CTest" OFF)
if(CHESSGUI_GUI_TESTS AND TARGET chess-gui)
    add_test(NAME gui_smoke COMMAND chess-gui --script ${CMAKE_CURRENT_SOURCE_DIR}/gui_smoke.txt
             WORKING_DIRECTORY ${CMAKE_BINARY_DIR})
    set_tests_properties(gui_smoke PROPERTIES TIMEOUT 60)
endif()
