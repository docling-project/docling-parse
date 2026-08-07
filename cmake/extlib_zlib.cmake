#
# Copyright IBM Inc. All rights reserved.
#
# SPDX-License-Identifier: MIT
#

message(STATUS "entering in extlib_zlib.cmake")

set(ext_name "zlib")

if(USE_SYSTEM_DEPS)
    find_package(ZLIB REQUIRED)

    add_library(${ext_name} INTERFACE IMPORTED)
    target_link_libraries(${ext_name} INTERFACE ZLIB::ZLIB)
else()
    include(ExternalProject)

    set(ZLIB_URL https://github.com/madler/zlib.git)
    set(ZLIB_TAG v1.3.1)

    if(MSVC)
        set(ZLIB_LIB ${EXTERNALS_PREFIX_PATH}/lib/zlibstatic.lib)
    else()
        set(ZLIB_LIB ${EXTERNALS_PREFIX_PATH}/lib/libz.a)
    endif()

    ExternalProject_Add(extlib_zlib

        PREFIX extlib_zlib

        UPDATE_COMMAND ""
        GIT_REPOSITORY ${ZLIB_URL}
        GIT_TAG ${ZLIB_TAG}

        BUILD_ALWAYS OFF

        INSTALL_DIR ${EXTERNALS_PREFIX_PATH}

        CMAKE_ARGS \\
        -DCMAKE_POSITION_INDEPENDENT_CODE=ON \\
        -DCMAKE_OSX_ARCHITECTURES=${CMAKE_OSX_ARCHITECTURES} \\
        -DCMAKE_BUILD_TYPE=Release \\
        -DCMAKE_INSTALL_LIBDIR=${EXTERNALS_PREFIX_PATH}/lib \\
        -DCMAKE_INSTALL_PREFIX=${EXTERNALS_PREFIX_PATH}

        LOG_DOWNLOAD ON
    )

    add_library(${ext_name} STATIC IMPORTED)
    add_dependencies(${ext_name} extlib_zlib)
    set(EXT_INCLUDE_DIRS ${EXTERNALS_PREFIX_PATH}/include)
    file(MAKE_DIRECTORY ${EXT_INCLUDE_DIRS})
    set_target_properties(${ext_name} PROPERTIES
      IMPORTED_LOCATION ${ZLIB_LIB}
      INTERFACE_LINK_DIRECTORIES ${EXTERNALS_PREFIX_PATH}/lib
      INTERFACE_INCLUDE_DIRECTORIES ${EXT_INCLUDE_DIRS}
    )

    if(NOT TARGET ZLIB::ZLIB)
        add_library(ZLIB::ZLIB ALIAS ${ext_name})
    endif()
endif()
