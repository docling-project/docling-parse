
message(STATUS "entering in extlib_cxxopts.cmake")

set(ext_name_cxxopts "cxxopts")

if(USE_SYSTEM_DEPS)
    message(STATUS "using system-deps in extlib_cxxopts.cmake")

    # this will define the cxxopts target
    # find_package(cxxopts REQUIRED)

    # add_library(${ext_name_cxxopts} INTERFACE IMPORTED)
    # add_dependencies(${ext_name_cxxopts} cxxopts)

    find_package(PkgConfig)		   
    pkg_check_modules(libcxxopts REQUIRED IMPORTED_TARGET cxxopts)
    add_library(${ext_name_cxxopts} ALIAS PkgConfig::libcxxopts)

else()
    message(STATUS "ignoring system-deps extlib_cxxopts.cmake")

    include(ExternalProject)
    include(CMakeParseArguments)

    set(CXXOPTS_URL https://github.com/jarro2783/cxxopts.git)
    # set(CXXOPTS_TAG v3.2.0)
    set(CXXOPTS_TAG v3.3.1)

    ExternalProject_Add(extlib_cxxopts

        PREFIX extlib_cxxopts

        UPDATE_COMMAND ""
        GIT_REPOSITORY ${CXXOPTS_URL}
        GIT_TAG ${CXXOPTS_TAG}

        BUILD_ALWAYS OFF

        INSTALL_DIR ${EXTERNALS_PREFIX_PATH}

        CMAKE_ARGS \\
        -DCMAKE_POSITION_INDEPENDENT_CODE=ON \\
        -DCMAKE_INSTALL_PREFIX=${EXTERNALS_PREFIX_PATH}

        BUILD_IN_SOURCE ON
        LOG_DOWNLOAD ON
    )

    add_library(${ext_name_cxxopts} INTERFACE)
    add_dependencies(${ext_name_cxxopts} extlib_cxxopts)
    set_target_properties(${ext_name_cxxopts} PROPERTIES INTERFACE_INCLUDE_DIRECTORIES ${EXTERNALS_PREFIX_PATH}/include
    )
endif()
