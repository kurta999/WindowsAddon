# Regenerates commitid.h from the repository's current git state.
#
# Runs in script mode (cmake -P) so a custom target can invoke it on every
# build: the header tracks HEAD, not whatever HEAD happened to be the last
# time CMake configured. configure_file() leaves the output untouched when
# the content is unchanged, so nothing recompiles on ordinary rebuilds.
# This is the CMake-side equivalent of commitid.bat, which the MSBuild
# project runs as a pre-build event.
#
# Required arguments (pass with -D before -P):
#   COMMITID_TEMPLATE     - path to commitid.h.in
#   COMMITID_OUTPUT       - path of the commitid.h to write
#   COMMITID_SOURCE_DIR   - repository root to ask git about
#   COMMITID_FALLBACK_TAG - tag used when git or the tag list is unavailable
#   COMMITID_PREFIX       - variable prefix the template expects (e.g. WINDOWSADDON)

foreach(arg COMMITID_TEMPLATE COMMITID_OUTPUT COMMITID_SOURCE_DIR COMMITID_FALLBACK_TAG COMMITID_PREFIX)
    if(NOT DEFINED ${arg})
        message(FATAL_ERROR "GenerateCommitId.cmake: ${arg} is not set")
    endif()
endforeach()

set(_commit_tag "")
set(_commit_id "")
set(_commit_dirty "")

find_package(Git QUIET)
if(Git_FOUND)
    execute_process(
        COMMAND "${GIT_EXECUTABLE}" describe --tags --abbrev=0
        WORKING_DIRECTORY "${COMMITID_SOURCE_DIR}"
        OUTPUT_VARIABLE _commit_tag
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
    )
    execute_process(
        COMMAND "${GIT_EXECUTABLE}" rev-parse --short HEAD
        WORKING_DIRECTORY "${COMMITID_SOURCE_DIR}"
        OUTPUT_VARIABLE _commit_id
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
    )
    execute_process(
        COMMAND "${GIT_EXECUTABLE}" diff --shortstat
        WORKING_DIRECTORY "${COMMITID_SOURCE_DIR}"
        OUTPUT_VARIABLE _commit_dirty
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
    )
endif()

if(NOT _commit_tag)
    set(_commit_tag "${COMMITID_FALLBACK_TAG}")
endif()
if(NOT _commit_id)
    set(_commit_id "unknown")
endif()

set(${COMMITID_PREFIX}_COMMIT_TAG "${_commit_tag}")
set(${COMMITID_PREFIX}_COMMIT_ID "${_commit_id}")
set(${COMMITID_PREFIX}_COMMIT_DIRTY "${_commit_dirty}")

configure_file("${COMMITID_TEMPLATE}" "${COMMITID_OUTPUT}" @ONLY)
