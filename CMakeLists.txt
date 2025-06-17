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
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wno-sign-compare -O3 ${ENV_ARCHFLAGS}")
else()
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wno-sign-compare -O3 -DROOT_PATH='\"${TOPLEVEL_PREFIX_PATH}\"' ${ENV_ARCHFLAGS}")
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
include(cmake/extlib_qpdf_v11.cmake)

# aggregate the targets created by the dependencies
set(DEPENDENCIES qpdf jpeg utf8 json loguru cxxopts)

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

add_executable(parse_v1.exe "${TOPLEVEL_PREFIX_PATH}/app/parse_v1.cpp")
add_executable(parse_v2.exe "${TOPLEVEL_PREFIX_PATH}/app/parse_v2.cpp")
add_executable(parse_v2_fonts.exe "${TOPLEVEL_PREFIX_PATH}/app/parse_v2_fonts.cpp")

set_property(TARGET parse_v1.exe PROPERTY CXX_STANDARD 20)
set_property(TARGET parse_v2.exe PROPERTY CXX_STANDARD 20)
set_property(TARGET parse_v2_fonts.exe PROPERTY CXX_STANDARD 20)

add_dependencies(parse_v1.exe ${DEPENDENCIES})
add_dependencies(parse_v2.exe ${DEPENDENCIES})
add_dependencies(parse_v2_fonts.exe ${DEPENDENCIES})

target_include_directories(parse_v1.exe INTERFACE ${DEPENDENCIES})
target_include_directories(parse_v2.exe INTERFACE ${DEPENDENCIES})
target_include_directories(parse_v2_fonts.exe INTERFACE ${DEPENDENCIES})

target_link_libraries(parse_v1.exe ${DEPENDENCIES} ${LIB_LINK})
target_link_libraries(parse_v2.exe ${DEPENDENCIES} ${LIB_LINK})
target_link_libraries(parse_v2_fonts.exe ${DEPENDENCIES} ${LIB_LINK})

# **********************
# ***  Libraries     ***
# **********************

add_library(parse_v1 STATIC "${TOPLEVEL_PREFIX_PATH}/app/parse_v1.cpp")
add_library(parse_v2 STATIC "${TOPLEVEL_PREFIX_PATH}/app/parse_v1.cpp")

add_dependencies(parse_v1 ${DEPENDENCIES})
add_dependencies(parse_v2 ${DEPENDENCIES})

target_include_directories(parse_v1 INTERFACE ${DEPENDENCIES})
target_include_directories(parse_v2 INTERFACE ${DEPENDENCIES})

set_target_properties(parse_v1 PROPERTIES POSITION_INDEPENDENT_CODE ON)
set_target_properties(parse_v2 PROPERTIES POSITION_INDEPENDENT_CODE ON)

target_link_libraries(parse_v1 ${DEPENDENCIES} ${LIB_LINK})
target_link_libraries(parse_v2 ${DEPENDENCIES} ${LIB_LINK})

# ***************************
# ***  Python-binding     ***
# ***************************

# https://pybind11.readthedocs.io/en/stable/compiling.html
# https://pybind11.readthedocs.io/en/stable/compiling.html#configuration-variables
find_package(pybind11 CONFIG REQUIRED)

#pybind11_add_module(docling_parse "${TOPLEVEL_PREFIX_PATH}/app/pybind_parse.cpp")
pybind11_add_module(pdf_parsers "${TOPLEVEL_PREFIX_PATH}/app/pybind_parse.cpp")

#add_dependencies(docling_parse parse_v1 parse_v2)
add_dependencies(pdf_parsers parse_v1 parse_v2)

#target_include_directories(docling_parse INTERFACE ${DEPENDENCIES})
target_include_directories(pdf_parsers INTERFACE ${DEPENDENCIES})

#target_compile_definitions(docling_parse PRIVATE VERSION_INFO=${CMAKE_PROJECT_VERSION})
target_compile_definitions(pdf_parsers PRIVATE VERSION_INFO=${CMAKE_PROJECT_VERSION})

#target_link_libraries(docling_parse PRIVATE parse_v1 parse_v2)
target_link_libraries(pdf_parsers PRIVATE parse_v1 parse_v2)

# *****************
# ***  Install  ***
# *****************

#install(TARGETS docling_parse DESTINATION "${TOPLEVEL_PREFIX_PATH}/docling_parse")
install(TARGETS pdf_parsers DESTINATION "${TOPLEVEL_PREFIX_PATH}/docling_parse")
