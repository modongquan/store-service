cmake_minimum_required(VERSION 2.8.12)
set(CTEST_SOURCE_DIRECTORY "/mnt/teamway/store-service/tools/cmake-3.18.2/Tests/CTestCoverageCollectGCOV/TestProject")
set(CTEST_BINARY_DIRECTORY "/mnt/teamway/store-service/tools/cmake-3.18.2/Tests/CTestCoverageCollectGCOV/TestProject")
set(CTEST_CMAKE_GENERATOR "Unix Makefiles")

ctest_start(Experimental)
ctest_configure()
ctest_build()
ctest_test()

#------------------------------------------------------------------------------#
# Common setup for all tests.
#------------------------------------------------------------------------------#

list(APPEND CTEST_CUSTOM_COVERAGE_EXCLUDE
  "/foo/something"
  "/3rdparty/"
  "/bar/somethingelse"
  "/CMakeFiles/"
)
list(APPEND CTEST_EXTRA_COVERAGE_GLOB "*.cpp")
include(CTestCoverageCollectGCOV)
set(expected_out
  CMakeFiles/myexecutable.dir/Labels.json
  Testing/CoverageInfo/data.json
  Testing/CoverageInfo/extra.cpp.gcov
  Testing/CoverageInfo/main.cpp.gcov
  uncovered/extra/uncovered1.cpp
  uncovered/uncovered2.cpp
)

#------------------------------------------------------------------------------#
# Test 1: with standard arguments
#------------------------------------------------------------------------------#

set(tar_file ${CTEST_BINARY_DIRECTORY}/gcov.tbz)
ctest_coverage_collect_gcov(
  TARBALL "${tar_file}"
  SOURCE "${CTEST_SOURCE_DIRECTORY}"
  BUILD "${CTEST_BINARY_DIRECTORY}"
  GCOV_COMMAND "${CMAKE_COMMAND}"
  GCOV_OPTIONS -P "/mnt/teamway/store-service/tools/cmake-3.18.2/Tests/CTestCoverageCollectGCOV/fakegcov.cmake")
file(REMOVE_RECURSE "${CTEST_BINARY_DIRECTORY}/uncovered")

execute_process(COMMAND
  ${CMAKE_COMMAND} -E tar tf ${tar_file}
  OUTPUT_VARIABLE out
  WORKING_DIRECTORY ${CMAKE_CURRENT_LIST_DIR}
  OUTPUT_STRIP_TRAILING_WHITESPACE
)

string(REPLACE "\n" ";" out "${out}")
list(SORT out)

if("${out}" STREQUAL "${expected_out}")
  message("PASSED with correct output: ${out}")
else()
  message(FATAL_ERROR "FAILED: expected:\n${expected_out}\nGot:\n${out}")
endif()

#------------------------------------------------------------------------------#
# Test 2: with optional argument: TARBALL_COMPRESSION "GZIP"
#------------------------------------------------------------------------------#

set(tar_file ${CTEST_BINARY_DIRECTORY}/gcov.tgz)
ctest_coverage_collect_gcov(
  TARBALL "${tar_file}"
  TARBALL_COMPRESSION "GZIP"
  SOURCE "${CTEST_SOURCE_DIRECTORY}"
  BUILD "${CTEST_BINARY_DIRECTORY}"
  GCOV_COMMAND "${CMAKE_COMMAND}"
  GCOV_OPTIONS -P "/mnt/teamway/store-service/tools/cmake-3.18.2/Tests/CTestCoverageCollectGCOV/fakegcov.cmake")
file(REMOVE_RECURSE "${CTEST_BINARY_DIRECTORY}/uncovered")

execute_process(COMMAND
  ${CMAKE_COMMAND} -E tar tf ${tar_file}
  OUTPUT_VARIABLE out
  WORKING_DIRECTORY ${CMAKE_CURRENT_LIST_DIR}
  OUTPUT_STRIP_TRAILING_WHITESPACE
)

string(REPLACE "\n" ";" out "${out}")
list(SORT out)

if("${out}" STREQUAL "${expected_out}")
  message("PASSED with correct output: ${out}")
else()
  message(FATAL_ERROR "FAILED: expected:\n${expected_out}\nGot:\n${out}")
endif()

#------------------------------------------------------------------------------#
# Test 3: with optional argument: TARBALL_COMPRESSION "FROM_EXT"
#------------------------------------------------------------------------------#

set(tar_file ${CTEST_BINARY_DIRECTORY}/gcov.txz)
ctest_coverage_collect_gcov(
  TARBALL "${tar_file}"
  TARBALL_COMPRESSION "FROM_EXT"
  SOURCE "${CTEST_SOURCE_DIRECTORY}"
  BUILD "${CTEST_BINARY_DIRECTORY}"
  GCOV_COMMAND "${CMAKE_COMMAND}"
  GCOV_OPTIONS -P "/mnt/teamway/store-service/tools/cmake-3.18.2/Tests/CTestCoverageCollectGCOV/fakegcov.cmake")
file(REMOVE_RECURSE "${CTEST_BINARY_DIRECTORY}/uncovered")

execute_process(COMMAND
  ${CMAKE_COMMAND} -E tar tf ${tar_file}
  OUTPUT_VARIABLE out
  WORKING_DIRECTORY ${CMAKE_CURRENT_LIST_DIR}
  OUTPUT_STRIP_TRAILING_WHITESPACE
)

string(REPLACE "\n" ";" out "${out}")
list(SORT out)

if("${out}" STREQUAL "${expected_out}")
  message("PASSED with correct output: ${out}")
else()
  message(FATAL_ERROR "FAILED: expected:\n${expected_out}\nGot:\n${out}")
endif()

#------------------------------------------------------------------------------#
# Test 4: with optional argument: TARBALL_COMPRESSION "FALSE"
#------------------------------------------------------------------------------#

set(tar_file ${CTEST_BINARY_DIRECTORY}/gcov.tar)
ctest_coverage_collect_gcov(
  TARBALL "${tar_file}"
  TARBALL_COMPRESSION "FALSE"
  SOURCE "${CTEST_SOURCE_DIRECTORY}"
  BUILD "${CTEST_BINARY_DIRECTORY}"
  GCOV_COMMAND "${CMAKE_COMMAND}"
  GCOV_OPTIONS -P "/mnt/teamway/store-service/tools/cmake-3.18.2/Tests/CTestCoverageCollectGCOV/fakegcov.cmake")
file(REMOVE_RECURSE "${CTEST_BINARY_DIRECTORY}/uncovered")

execute_process(COMMAND
  ${CMAKE_COMMAND} -E tar tf ${tar_file}
  OUTPUT_VARIABLE out
  WORKING_DIRECTORY ${CMAKE_CURRENT_LIST_DIR}
  OUTPUT_STRIP_TRAILING_WHITESPACE
)

string(REPLACE "\n" ";" out "${out}")
list(SORT out)

if("${out}" STREQUAL "${expected_out}")
  message("PASSED with correct output: ${out}")
else()
  message(FATAL_ERROR "FAILED: expected:\n${expected_out}\nGot:\n${out}")
endif()

#------------------------------------------------------------------------------#
# Test 5: with optional argument: TARBALL_COMPRESSION "ZSTD"
#------------------------------------------------------------------------------#

set(tar_file ${CTEST_BINARY_DIRECTORY}/gcov.zstd)
ctest_coverage_collect_gcov(
  TARBALL "${tar_file}"
  TARBALL_COMPRESSION "ZSTD"
  SOURCE "${CTEST_SOURCE_DIRECTORY}"
  BUILD "${CTEST_BINARY_DIRECTORY}"
  GCOV_COMMAND "${CMAKE_COMMAND}"
  GCOV_OPTIONS -P "/mnt/teamway/store-service/tools/cmake-3.18.2/Tests/CTestCoverageCollectGCOV/fakegcov.cmake")
file(REMOVE_RECURSE "${CTEST_BINARY_DIRECTORY}/uncovered")

execute_process(COMMAND
  ${CMAKE_COMMAND} -E tar tf ${tar_file}
  OUTPUT_VARIABLE out
  WORKING_DIRECTORY ${CMAKE_CURRENT_LIST_DIR}
  OUTPUT_STRIP_TRAILING_WHITESPACE
)

string(REPLACE "\n" ";" out "${out}")
list(SORT out)

if("${out}" STREQUAL "${expected_out}")
  message("PASSED with correct output: ${out}")
else()
  message(FATAL_ERROR "FAILED: expected:\n${expected_out}\nGot:\n${out}")
endif()