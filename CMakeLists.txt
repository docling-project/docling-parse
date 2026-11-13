# cmake_minimum_required(VERSION 3.12..3.26)
cmake_minimum_required(VERSION 3.12)

project(docling_parse VERSION 1.0.0 LANGUAGES CXX C)
#set(CMAKE_VERBOSE_MAKEFILE off)

message(STATUS "cmake osx-deployment: " ${CMAKE_OSX_DEPLOYMENT_TARGET})

# minimum macosx, ignored on other platforms
if(APPLE)
    if(NOT CMAKE_OSX_DEPLOYMENT_TARGET)
        message(STATUS "cmake is not set")            
    elseif(CMAKE_OSX_DEPLOYMENT_TARGET VERSION_LESS 10.15)
        set(CMAKE_OSX_DEPLOYMENT_TARGET "10.15" CACHE STRING "Minimum macOS version" FORCE)
    endif()
endif()

message(STATUS "cmake system-version: " ${CMAKE_SYSTEM_VERSION})
message(STATUS "cmake osx-deployment: " ${CMAKE_OSX_DEPLOYMENT_TARGET})

# ************************
# ***     paths        ***
# ************************

if(NOT DEFINED TOPLEVEL_PREFIX_PATH)
    set(TOPLEVEL_PREFIX_PATH ${CMAKE_CURRENT_SOURCE_DIR})
endif()

if(NOT DEFINED CMAKE_INSTALL_PREFIX)
    set(CMAKE_INSTALL_PREFIX ${TOPLEVEL_PREFIX_PATH}/install_dir)
endif()

if(NOT DEFINED EXTERNALS_PREFIX_PATH)
    set(EXTERNALS_PREFIX_PATH "${TOPLEVEL_PREFIX_PATH}/externals" CACHE INTERNAL "")
endif()

if(NOT DEFINED RESOURCES_PREFIX_PATH)
    set(RESOURCES_PREFIX_PATH "${TOPLEVEL_PREFIX_PATH}/resources" CACHE INTERNAL "")
endif()

if(NOT "${TOPLEVEL_PREFIX_PATH}/cmake" IN_LIST CMAKE_MODULE_PATH)
    set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} "${TOPLEVEL_PREFIX_PATH}/cmake")
endif()

if(NOT DEFINED CMAKE_PDF_DATA_DIR)
    set(CMAKE_PDF_DATA_DIR "${TOPLEVEL_PREFIX_PATH}/docling_parse/pdf_resources")
endif()

set(ENV_ARCH $ENV{CIBW_ARCHS})
message(STATUS "cibw arch: " ${ENV_ARCH})

set(CMAKE_OSX_ARCHITECTURES $ENV{CIBW_ARCHS})
#message(STATUS "cibw arch: " ${ENV_ARCH})

set(ENV_ARCHFLAGS $ENV{ARCHFLAGS})
message(STATUS "arch flags: " ${ENV_ARCHFLAGS})

message(STATUS "      top path: " ${TOPLEVEL_PREFIX_PATH})
message(STATUS "      lib path: " ${EXTERNALS_PREFIX_PATH})
message(STATUS "  install path: " ${CMAKE_INSTALL_PREFIX})
message(STATUS "    cmake path: " ${CMAKE_MODULE_PATH})
message(STATUS "  cmake system: " ${CMAKE_SYSTEM_PROCESSOR})
message(STATUS "cmake osx arch: " ${CMAKE_OSX_ARCHITECTURES})

# ************************
# ***  cxx-flags       ***
# ************************

set(CMAKE_CXX_STANDARD          20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

#set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wno-sign-compare -g3 -DROOT_PATH='\"${TOPLEVEL_PREFIX_PATH}\"' ${ENV_ARCHFLAGS}")
#set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wno-sign-compare -O3 -DROOT_PATH='\"${TOPLEVEL_PREFIX_PATH}\"' ${ENV_ARCHFLAGS}")

if (WIN32)
    set(TEST_PATH "\\\"${TOPLEVEL_PREFIX_PATH}\\\"")
    add_definitions(-DROOT_PATH="\\\"${TOPLEVEL_PREFIX_PATH}\\\"")
    if(MSVC)
        # cl rejects GCC/Clang-style flags: -Wall / -Wno-sign-compare / -O3 are
        # parsed as invalid /W... and /O... arguments (error D8021), and these
        # flags are inherited by the in-tree subprojects (loguru, blend2d,
        # pdfium_jbig2). Optimization comes from a Release build instead: MSVC
        # Release uses /O2 (the -O3 equivalent) and links the redistributable
        # release CRT (/MD). Restrict the multi-config Visual Studio generator to
        # Release so a config-less `cmake --build` cannot fall back to a Debug
        # build depending on the non-redistributable debug CRT. The build is
        # invoked with --config Release (see local_build.py).
        set(CMAKE_CONFIGURATION_TYPES "Release" CACHE STRING "MSVC build configurations" FORCE)

        # Source files are UTF-8. Without /utf-8, MSVC reads multi-byte characters
        # (e.g. the curly quotes and en/em dashes in src/parse/utils/string.h) as
        # multi-character constants -> error C2015. GCC/Clang assume UTF-8, so this
        # only bites the MSVC/arm64 build.
        add_compile_options(/utf-8 /bigobj)
        # MSVC's <cmath> only exposes M_PI (used in src/parse/utils/values.h) when
        # _USE_MATH_DEFINES is defined before the include -> without it, C2065.
        # NOMINMAX stops <windows.h> from defining min()/max() function-like
        # macros, which otherwise collide with std::numeric_limits<T>::max() in
        # qpdf's QIntC.hh (and std::min/std::max in general) -> error C2589.
        add_compile_definitions(_USE_MATH_DEFINES NOMINMAX)
    else()
        # MinGW/GCC toolchain (Windows amd64)
        set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wno-sign-compare -O3 ${ENV_ARCHFLAGS}")
    endif()
else()
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wno-sign-compare -O3 -DROOT_PATH='\"${TOPLEVEL_PREFIX_PATH}\"' ${ENV_ARCHFLAGS}")
  # set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wno-sign-compare -g3 -O0 -fno-omit-frame-pointer -DROOT_PATH='\"${TOPLEVEL_PREFIX_PATH}\"' ${ENV_ARCHFLAGS}")
endif()

message(STATUS "cxx-compiler: " ${CMAKE_CXX_COMPILER})
message(STATUS "cxx-flags   : " ${CMAKE_CXX_FLAGS})
message(STATUS "cxx-standard: " ${CMAKE_CXX_STANDARD})

# ************************
# *** dependencies     ***
# ************************

option(USE_SYSTEM_DEPS OFF "If enabled, the build will find and link to system dependencies, otherwise they are sourced from the original repos and compiled on the fly.")

# prepare setup for building external packages
if(NOT USE_SYSTEM_DEPS)
    if(NOT EXISTS ${EXTERNALS_PREFIX_PATH})
        file(MAKE_DIRECTORY ${EXTERNALS_PREFIX_PATH})
        file(MAKE_DIRECTORY ${EXTERNALS_PREFIX_PATH}/bin)
        file(MAKE_DIRECTORY ${EXTERNALS_PREFIX_PATH}/lib)
        file(MAKE_DIRECTORY ${EXTERNALS_PREFIX_PATH}/include)
        file(MAKE_DIRECTORY ${EXTERNALS_PREFIX_PATH}/resources)
    endif()
endif()

# include dependencies
include(cmake/extlib_cxxopts.cmake)
include(cmake/extlib_loguru.cmake)
include(cmake/extlib_json.cmake)
include(cmake/extlib_utf8.git.cmake)
include(cmake/extlib_jpeg.cmake)
include(cmake/extlib_openjpeg.cmake)
include(cmake/extlib_lcms2.cmake)
# include(cmake/extlib_qpdf_v11.cmake)
include(cmake/extlib_qpdf_v12.cmake)
include(cmake/extlib_blend2d.cmake)
include(cmake/extlib_freetype.cmake)
include(cmake/extlib_pdfium_jbig2.cmake)

# aggregate the targets created by the dependencies
set(DEPENDENCIES qpdf jpeg openjpeg lcms2 utf8 json loguru cxxopts blend2d freetype pdfium_jbig2)

# ************************
# *** libraries        ***
# ************************

# define LIB_LINK and OS_DEPENDENCIES
include(cmake/os_opts.cmake)
list(APPEND DEPENDENCIES ${OS_DEPENDENCIES})

# ***********************
# ***  includes       ***
# ***********************

# define subdirlist utility
include(cmake/subdirlist.cmake)

include_directories(${TOPLEVEL_PREFIX_PATH}/src)

subdirlist(SUBDIRS ${TOPLEVEL_PREFIX_PATH}/src)
message(STATUS "subdirs: " ${SUBDIRS})
include_directories(${SUBDIRS})

# **********************
# ***  Executables   ***
# **********************

string(REPLACE ";" ";" SUBDIRS_SEMICOLON "${SUBDIRS}")
message(STATUS "subdirs: ${SUBDIRS_SEMICOLON}")

string(REPLACE ";" ";" DEPENDENCIES_SEMICOLON "${DEPENDENCIES}")
message(STATUS "cmake dependencies: ${DEPENDENCIES_SEMICOLON}")

string(REPLACE ";" ";" LIBLINK_SEMICOLON "${LIB_LINK}")
message(STATUS "cmake lib-link: ${LIBLINK_SEMICOLON}")

add_executable(parse.exe "${TOPLEVEL_PREFIX_PATH}/app/parse.cpp")
add_executable(parse_fonts.exe "${TOPLEVEL_PREFIX_PATH}/app/parse_fonts.cpp")
add_executable(render.exe "${TOPLEVEL_PREFIX_PATH}/app/render.cpp")
add_executable(analyse.exe "${TOPLEVEL_PREFIX_PATH}/app/analyse.cpp")
add_executable(run_scaling.exe "${TOPLEVEL_PREFIX_PATH}/app/run_scaling.cpp")
# add_executable(page_images.exe "${TOPLEVEL_PREFIX_PATH}/app/page_images.cpp")

set_property(TARGET parse.exe PROPERTY CXX_STANDARD 20)
set_property(TARGET parse_fonts.exe PROPERTY CXX_STANDARD 20)
set_property(TARGET render.exe PROPERTY CXX_STANDARD 20)
set_property(TARGET analyse.exe PROPERTY CXX_STANDARD 20)
set_property(TARGET run_scaling.exe PROPERTY CXX_STANDARD 20)
# set_property(TARGET page_images.exe PROPERTY CXX_STANDARD 20)

add_dependencies(parse.exe ${DEPENDENCIES})
add_dependencies(parse_fonts.exe ${DEPENDENCIES})
add_dependencies(render.exe ${DEPENDENCIES})
add_dependencies(analyse.exe ${DEPENDENCIES})
add_dependencies(run_scaling.exe ${DEPENDENCIES})
# add_dependencies(page_images.exe ${DEPENDENCIES})

target_include_directories(parse.exe INTERFACE ${DEPENDENCIES})
target_include_directories(parse_fonts.exe INTERFACE ${DEPENDENCIES})
target_include_directories(render.exe INTERFACE ${DEPENDENCIES})
target_include_directories(analyse.exe INTERFACE ${DEPENDENCIES})
target_include_directories(run_scaling.exe INTERFACE ${DEPENDENCIES})
# target_include_directories(page_images.exe INTERFACE ${DEPENDENCIES})

target_link_libraries(parse.exe ${DEPENDENCIES} ${LIB_LINK})
target_link_libraries(parse_fonts.exe ${DEPENDENCIES} ${LIB_LINK})
target_link_libraries(render.exe ${DEPENDENCIES} ${LIB_LINK})
target_link_libraries(analyse.exe ${DEPENDENCIES} ${LIB_LINK})
target_link_libraries(run_scaling.exe ${DEPENDENCIES} ${LIB_LINK})
# target_link_libraries(page_images.exe ${DEPENDENCIES} ${LIB_LINK})

# **********************
# ***  Libraries     ***
# **********************

add_library(parse STATIC "${TOPLEVEL_PREFIX_PATH}/app/parse.cpp")

add_dependencies(parse ${DEPENDENCIES})

target_include_directories(parse INTERFACE ${DEPENDENCIES})

set_target_properties(parse PROPERTIES POSITION_INDEPENDENT_CODE ON)

target_link_libraries(parse ${DEPENDENCIES} ${LIB_LINK})

option(DOCLING_PARSE_BUILD_STRUCTURE_TESTS "Build the native structure concurrency harness" OFF)
if(DOCLING_PARSE_BUILD_STRUCTURE_TESTS)
    add_executable(structure_concurrency tests/cpp/test_structure_concurrency.cpp)
    set_property(TARGET structure_concurrency PROPERTY CXX_STANDARD 20)
    add_dependencies(structure_concurrency ${DEPENDENCIES})
    target_link_libraries(structure_concurrency PRIVATE ${DEPENDENCIES} ${LIB_LINK})
endif()

option(DOCLING_PARSE_BUILD_MEMORY_PROBE "Build the native line-memory probe" OFF)
if(DOCLING_PARSE_BUILD_MEMORY_PROBE)
    add_executable(test_memory_on_lines tests/cpp/test_memory_on_lines.cpp)
    add_dependencies(test_memory_on_lines ${DEPENDENCIES})
    target_link_libraries(test_memory_on_lines PRIVATE ${DEPENDENCIES} ${LIB_LINK})
    target_compile_definitions(test_memory_on_lines PRIVATE DOCLING_PARSE_CONTENT_DIAGNOSTICS=1)

    add_executable(stream_callback_regression tests/cpp/test_stream_callbacks.cpp)
    add_dependencies(stream_callback_regression ${DEPENDENCIES})
    target_link_libraries(stream_callback_regression PRIVATE ${DEPENDENCIES} ${LIB_LINK})
    enable_testing()
    add_test(NAME stream_callback_regression COMMAND stream_callback_regression)
    set_tests_properties(stream_callback_regression PROPERTIES TIMEOUT 30)

    add_executable(stream_state_lifetime_regression tests/cpp/test_stream_state_lifetimes.cpp)
    add_dependencies(stream_state_lifetime_regression ${DEPENDENCIES})
    target_link_libraries(stream_state_lifetime_regression PRIVATE ${DEPENDENCIES} ${LIB_LINK})
    target_compile_definitions(stream_state_lifetime_regression PRIVATE
        DOCLING_PARSE_CONTENT_DIAGNOSTICS=1 DOCLING_PARSE_STATE_LIFETIME_TESTING=1)
    add_test(NAME stream_state_lifetime_regression COMMAND stream_state_lifetime_regression)
    set_tests_properties(stream_state_lifetime_regression PROPERTIES TIMEOUT 30)

    add_executable(clip_history_regression tests/cpp/test_clip_history.cpp)
    add_dependencies(clip_history_regression ${DEPENDENCIES})
    target_link_libraries(clip_history_regression PRIVATE ${DEPENDENCIES} ${LIB_LINK})
    add_test(NAME clip_history_regression COMMAND clip_history_regression)
    set_tests_properties(clip_history_regression PROPERTIES TIMEOUT 30)
endif()

# Test the native library directly: the renderer's curve-flattening workaround
# (#407) otherwise bypasses the stroker routines patched for #396/#400/#401.
option(DOCLING_PARSE_BUILD_BLEND2D_TESTS "Build native Blend2D patch regression tests" OFF)
if(DOCLING_PARSE_BUILD_BLEND2D_TESTS)
    enable_testing()
    add_executable(blend2d_patch_regression tests/cpp/test_blend2d_patches.cpp)
    target_link_libraries(blend2d_patch_regression PRIVATE blend2d)
    foreach(case affine stall spike flat-return flat-overshoot)
        add_test(NAME blend2d_${case} COMMAND blend2d_patch_regression ${case})
        set_tests_properties(blend2d_${case} PROPERTIES TIMEOUT 60)
    endforeach()
endif()

# ***************************
# ***  Python-binding     ***
# ***************************

# https://pybind11.readthedocs.io/en/stable/compiling.html
# https://pybind11.readthedocs.io/en/stable/compiling.html#configuration-variables
find_package(pybind11 CONFIG REQUIRED)

#pybind11_add_module(docling_parse "${TOPLEVEL_PREFIX_PATH}/app/pybind_parse.cpp")
pybind11_add_module(pdf_parsers "${TOPLEVEL_PREFIX_PATH}/app/pybind_parse.cpp")

#add_dependencies(docling_parse parse)
add_dependencies(pdf_parsers parse)

#target_include_directories(docling_parse INTERFACE ${DEPENDENCIES})
target_include_directories(pdf_parsers INTERFACE ${DEPENDENCIES})

#target_compile_definitions(docling_parse PRIVATE VERSION_INFO=${CMAKE_PROJECT_VERSION})
target_compile_definitions(pdf_parsers PRIVATE VERSION_INFO=${CMAKE_PROJECT_VERSION})

#target_link_libraries(docling_parse PRIVATE parse)
target_link_libraries(pdf_parsers PRIVATE parse)

# *****************
# ***  Install  ***
# *****************

#install(TARGETS docling_parse DESTINATION "${TOPLEVEL_PREFIX_PATH}/docling_parse")
install(TARGETS pdf_parsers DESTINATION "${TOPLEVEL_PREFIX_PATH}/docling_parse")
