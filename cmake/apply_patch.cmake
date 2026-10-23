# Applies a unified diff to the current directory with `git apply`, once.
#
# Used as a FetchContent PATCH_COMMAND, which runs in the populated source
# directory. FetchContent re-runs the patch step when it re-populates a source
# tree, so an already-applied patch is detected (via a reverse dry run) and
# skipped instead of failing.
#
#   cmake -DPATCH_FILE=<file> -P apply_patch.cmake

if(NOT DEFINED PATCH_FILE)
    message(FATAL_ERROR "apply_patch.cmake: PATCH_FILE is not set")
endif()

find_package(Git REQUIRED)

execute_process(
    COMMAND "${GIT_EXECUTABLE}" apply --check --reverse --ignore-whitespace "${PATCH_FILE}"
    RESULT_VARIABLE already_applied
    OUTPUT_QUIET
    ERROR_QUIET
)
if(already_applied EQUAL 0)
    message(STATUS "apply_patch: ${PATCH_FILE} is already applied")
    return()
endif()

execute_process(
    COMMAND "${GIT_EXECUTABLE}" apply --ignore-whitespace "${PATCH_FILE}"
    RESULT_VARIABLE apply_result
)
if(NOT apply_result EQUAL 0)
    message(FATAL_ERROR "apply_patch: failed to apply ${PATCH_FILE}")
endif()
message(STATUS "apply_patch: applied ${PATCH_FILE}")
