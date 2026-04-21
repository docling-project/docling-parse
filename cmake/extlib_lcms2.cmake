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
        set(LCMS2_IMPORTED_LIB ${EXTERNALS_PREFIX_PATH}/lib/lcms2.lib)
        set(LCMS2_BUILD_SHARED OFF)
    else()
        set(LCMS2_IMPORTED_LIB ${EXTERNALS_PREFIX_PATH}/lib/liblcms2.a)
        set(LCMS2_BUILD_SHARED OFF)
    endif()

    ExternalProject_Add(extlib_lcms2
        PREFIX extlib_lcms2

        UPDATE_COMMAND ""
        GIT_REPOSITORY ${LCMS2_URL}
        GIT_TAG ${LCMS2_TAG}

        BUILD_ALWAYS OFF

        INSTALL_DIR ${EXTERNALS_PREFIX_PATH}

        CMAKE_ARGS \\
        -DCMAKE_POSITION_INDEPENDENT_CODE=ON \\
        -DBUILD_SHARED_LIBS=${LCMS2_BUILD_SHARED} \\
        -DCMAKE_BUILD_TYPE=Release \\
        -DCMAKE_OSX_ARCHITECTURES=${CMAKE_OSX_ARCHITECTURES} \\
        -DCMAKE_C_FLAGS=${ENV_ARCHFLAGS} \\
        -DCMAKE_CXX_FLAGS=${ENV_ARCHFLAGS} \\
        -DCMAKE_INSTALL_PREFIX=${EXTERNALS_PREFIX_PATH} \\
        -DCMAKE_INSTALL_LIBDIR=${EXTERNALS_PREFIX_PATH}/lib

        LOG_DOWNLOAD ON
    )

    add_library(${ext_name} STATIC IMPORTED)
    add_dependencies(${ext_name} extlib_lcms2)
    set_target_properties(${ext_name} PROPERTIES
        IMPORTED_LOCATION ${LCMS2_IMPORTED_LIB}
        INTERFACE_INCLUDE_DIRECTORIES ${EXTERNALS_PREFIX_PATH}/include
    )
endif()
