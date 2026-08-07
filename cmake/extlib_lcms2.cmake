message(STATUS "entering in extlib_lcms2.cmake")

set(ext_name "lcms2")

if(USE_SYSTEM_DEPS)
    find_package(PkgConfig)
    if(PkgConfig_FOUND)
        pkg_check_modules(liblcms2 IMPORTED_TARGET lcms2)
    endif()

    if(TARGET PkgConfig::liblcms2)
        add_library(${ext_name} ALIAS PkgConfig::liblcms2)
    else()
        find_path(LCMS2_INCLUDE_DIR lcms2.h)
        find_library(LCMS2_LIBRARY NAMES lcms2 liblcms2)

        if(NOT LCMS2_INCLUDE_DIR OR NOT LCMS2_LIBRARY)
            message(FATAL_ERROR "lcms2 not found. Install Little CMS 2 or disable USE_SYSTEM_DEPS.")
        endif()

        add_library(${ext_name} UNKNOWN IMPORTED)
        set_target_properties(${ext_name} PROPERTIES
            IMPORTED_LOCATION "${LCMS2_LIBRARY}"
            INTERFACE_INCLUDE_DIRECTORIES "${LCMS2_INCLUDE_DIR}"
        )
    endif()

else()
    include(ExternalProject)
    include(CMakeParseArguments)

    file(MAKE_DIRECTORY ${EXTERNALS_PREFIX_PATH}/include)
    file(MAKE_DIRECTORY ${EXTERNALS_PREFIX_PATH}/lib)

    set(LCMS2_URL https://github.com/mm2/Little-CMS.git)
    set(LCMS2_TAG lcms2.17)

    if(WIN32)
        # Windows (MSVC and MinGW): use vendored CMakeLists. Upstream has no
        # CMakeLists and its ./configure cannot run under MSVC and is fragile
        # under MinGW/Ninja (requires sh/make). Produces lcms2.lib (MSVC) or
        # liblcms2.a (MinGW).
        if(MSVC)
            set(LCMS2_IMPORTED_LIB ${EXTERNALS_PREFIX_PATH}/lib/lcms2.lib)
        else()
            set(LCMS2_IMPORTED_LIB ${EXTERNALS_PREFIX_PATH}/lib/liblcms2.a)
        endif()

        ExternalProject_Add(extlib_lcms2
            PREFIX extlib_lcms2

            UPDATE_COMMAND ""
            GIT_REPOSITORY ${LCMS2_URL}
            GIT_TAG ${LCMS2_TAG}

            BUILD_ALWAYS OFF

            INSTALL_DIR ${EXTERNALS_PREFIX_PATH}

            PATCH_COMMAND ${CMAKE_COMMAND} -E copy
                ${CMAKE_CURRENT_LIST_DIR}/lcms2_CMakeLists.txt
                <SOURCE_DIR>/CMakeLists.txt

            CMAKE_ARGS \\
            -DCMAKE_POSITION_INDEPENDENT_CODE=ON \\
            -DCMAKE_BUILD_TYPE=Release \\
            -DCMAKE_INSTALL_PREFIX=${EXTERNALS_PREFIX_PATH}

            LOG_DOWNLOAD ON
        )
    else()
        # Unix (macOS/Linux) build via autotools. Produces liblcms2.a.
        set(LCMS2_IMPORTED_LIB ${EXTERNALS_PREFIX_PATH}/lib/liblcms2.a)

        ExternalProject_Add(extlib_lcms2
            PREFIX extlib_lcms2

            UPDATE_COMMAND ""
            GIT_REPOSITORY ${LCMS2_URL}
            GIT_TAG ${LCMS2_TAG}

            BUILD_ALWAYS OFF

            INSTALL_DIR ${EXTERNALS_PREFIX_PATH}

            BUILD_IN_SOURCE ON
            CONFIGURE_COMMAND ./configure
                --prefix=${EXTERNALS_PREFIX_PATH}
                --disable-shared
                --enable-static
                CFLAGS=-fPIC\ ${ENV_ARCHFLAGS}
            BUILD_COMMAND make
            INSTALL_COMMAND make install

            LOG_DOWNLOAD ON
        )
    endif()

    add_library(${ext_name} STATIC IMPORTED)
    add_dependencies(${ext_name} extlib_lcms2)
    set_target_properties(${ext_name} PROPERTIES
        IMPORTED_LOCATION ${LCMS2_IMPORTED_LIB}
        INTERFACE_INCLUDE_DIRECTORIES ${EXTERNALS_PREFIX_PATH}/include
    )
endif()
