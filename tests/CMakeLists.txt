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
    test_match.cpp
    test_main.cpp
)
target_link_libraries(core_tests PRIVATE chesscore)
target_compile_definitions(core_tests PRIVATE
    CHESSGUI_SOURCE_DIR="${PROJECT_SOURCE_DIR}"
    MOCK_ENGINE_PATH="$<TARGET_FILE:mock_engine>")
add_dependencies(core_tests mock_engine)

# One CTest entry per test group (name prefix understood by the runner).
foreach(group perft fen san pgn game uci engine bisect ucicheck stats openings match)
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

# chess-cli against the mock engine: exit codes and output.
if(TARGET chess-cli)
    set(cli $<TARGET_FILE:chess-cli>)
    set(mock cmd=$<TARGET_FILE:mock_engine>)
    function(cli_test name code regex cmd)
        add_test(NAME ${name} COMMAND ${CMAKE_COMMAND} "-DCMD=${cmd}" -DEXPECT_CODE=${code}
                 "-DEXPECT_REGEX=${regex}" -P ${CMAKE_CURRENT_SOURCE_DIR}/run_cli.cmake)
        set_tests_properties(${name} PROPERTIES TIMEOUT 120)
    endfunction()
    cli_test(cli_help 0 "Exit codes" "${cli}|help")
    cli_test(cli_usage 2 "unknown option" "${cli}|match|--engine|${mock}|--engine|${mock}|--bogus")
    cli_test(cli_match 0 "\"gamesPlayed\": 8"
             "${cli}|match|--engine|${mock}|name=A|--engine|${mock}|name=B|--tc|1+0.01|--games|8|--concurrency|2|--json")
    cli_test(cli_rr 0 "Standings"
             "${cli}|match|--mode|roundrobin|--engine|${mock}|name=A|--engine|${mock}|name=B|--engine|${mock}|name=C|--tc|1+0.01|--games|2|--concurrency|3")
    # an engine that forfeits every game: the SPRT accepts H0 (exit code 1)
    cli_test(cli_sprt_h0 1 "H0 accepted"
             "${cli}|sprt|--engine|${mock}|name=Bad|args=--illegal-bestmove|--engine|${mock}|name=B|--tc|1+0.01|--games|200|--concurrency|4|--elo1|50|--model|logistic")
    add_dependencies(core_tests chess-cli)
endif()
