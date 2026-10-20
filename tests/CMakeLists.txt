add_executable(mock_engine mock_engine.cpp)
target_link_libraries(mock_engine PRIVATE chesscore)

add_executable(core_tests
    test_chess.cpp
    test_uci.cpp
    test_main.cpp
)
target_link_libraries(core_tests PRIVATE chesscore)
target_compile_definitions(core_tests PRIVATE
    CHESSGUI_SOURCE_DIR="${PROJECT_SOURCE_DIR}"
    MOCK_ENGINE_PATH="$<TARGET_FILE:mock_engine>")
add_dependencies(core_tests mock_engine)

# One CTest entry per test group (name prefix understood by the runner).
foreach(group perft fen san pgn game uci)
    add_test(NAME ${group} COMMAND core_tests ${group}/)
endforeach()
set_tests_properties(perft PROPERTIES TIMEOUT 600)
