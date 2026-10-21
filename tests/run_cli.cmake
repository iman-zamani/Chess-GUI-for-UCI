# Runs one chess-cli command and checks its exit code and output.
#   cmake -DCMD="exe|arg|arg" -DEXPECT_CODE=N [-DEXPECT_REGEX=re] -P run_cli.cmake
# ("|" separates the arguments, since CTest would split a list.)
string(REPLACE "|" ";" cmd "${CMD}")
execute_process(COMMAND ${cmd} RESULT_VARIABLE code OUTPUT_VARIABLE out ERROR_VARIABLE err)
message("${out}${err}")
if(NOT code STREQUAL "${EXPECT_CODE}")
    message(FATAL_ERROR "exit code ${code}, expected ${EXPECT_CODE}")
endif()
if(DEFINED EXPECT_REGEX AND NOT "${out}${err}" MATCHES "${EXPECT_REGEX}")
    message(FATAL_ERROR "output does not match: ${EXPECT_REGEX}")
endif()
