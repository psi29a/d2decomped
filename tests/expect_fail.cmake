# SPDX-License-Identifier: GPL-3.0-or-later
# cmake -DCMD=exe "-DARGS=a|b" -DPATTERN=regex -DSKIP_CODE=77 -P expect_fail.cmake
# Passes when CMD ARGS exits nonzero and its stderr matches PATTERN. Exit SKIP_CODE prints
# "SKIP:" (the test's SKIP_REGULAR_EXPRESSION), because ctest ignores this script's own code for that.
string(REPLACE "|" ";" args "${ARGS}")
execute_process(COMMAND ${CMD} ${args} RESULT_VARIABLE rc OUTPUT_VARIABLE out ERROR_VARIABLE err)
if (rc STREQUAL SKIP_CODE)
    message("SKIP: ${out}")
    return()
endif()
if (rc STREQUAL "0")
    message(FATAL_ERROR "expected a nonzero exit, got 0: ${err}")
endif()
if (NOT err MATCHES "${PATTERN}")
    message(FATAL_ERROR "exit ${rc}, stderr did not match '${PATTERN}': ${err}")
endif()
