
message(STATUS "entering in extlib_pybind11.cmake")

include(ExternalProject)
include(CMakeParseArguments)

set(PYBIND11_URL https://github.com/pybind/pybind11.git)
# set(PYBIND11_TAG v2.13.5)
set(PYBIND11_TAG v2.13.6)

ExternalProject_Add(extlib_pybind11
    PREFIX extlib_pybind11

    GIT_REPOSITORY ${PYBIND11_URL}
    GIT_TAG ${PYBIND11_TAG}

    UPDATE_COMMAND ""
    CONFIGURE_COMMAND ""

    BUILD_COMMAND ""
    BUILD_ALWAYS OFF

    INSTALL_DIR     ${EXTERNALS_PREFIX_PATH}
    INSTALL_COMMAND ${CMAKE_COMMAND} -E copy_directory <SOURCE_DIR>/include/ ${EXTERNALS_PREFIX_PATH}/include/
    )

add_library(pybind11 INTERFACE)
add_custom_target(install_extlib_pybind11 DEPENDS extlib_pybind11)
add_dependencies(pybind11 install_extlib_pybind11)
