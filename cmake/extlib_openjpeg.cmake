
message(STATUS "entering in extlib_openjpeg.cmake")

set(ext_name "openjpeg")

if(USE_SYSTEM_DEPS)
    find_package(PkgConfig)
    pkg_check_modules(libopenjp2 REQUIRED IMPORTED_TARGET libopenjp2)

    add_library(${ext_name} ALIAS PkgConfig::libopenjp2)

else()
    include(ExternalProject)
    include(CMakeParseArguments)

    file(MAKE_DIRECTORY ${EXTERNALS_PREFIX_PATH}/include)
    file(MAKE_DIRECTORY ${EXTERNALS_PREFIX_PATH}/include/openjpeg-2.5)
    file(MAKE_DIRECTORY ${EXTERNALS_PREFIX_PATH}/lib)

    set(OPENJPEG_URL https://github.com/uclouvain/openjpeg.git)
    set(OPENJPEG_TAG v2.5.3)
    set(OPENJPEG_IMPORTED_LIB ${EXTERNALS_PREFIX_PATH}/lib/libopenjp2.a)

    ExternalProject_Add(extlib_openjpeg

        PREFIX extlib_openjpeg

        UPDATE_COMMAND ""
        GIT_REPOSITORY ${OPENJPEG_URL}
        GIT_TAG ${OPENJPEG_TAG}

        BUILD_ALWAYS OFF

        INSTALL_DIR ${EXTERNALS_PREFIX_PATH}

        CMAKE_ARGS \\
        -DCMAKE_POSITION_INDEPENDENT_CODE=ON \\
        -DCMAKE_OSX_ARCHITECTURES=${CMAKE_OSX_ARCHITECTURES} \\
        -DCMAKE_C_FLAGS=${ENV_ARCHFLAGS} \\
        -DBUILD_CODEC=OFF \\
        -DBUILD_JPIP=OFF \\
        -DBUILD_JPWL=OFF \\
        -DBUILD_THIRDPARTY=OFF \\
        -DBUILD_TESTING=OFF \\
        -DBUILD_SHARED_LIBS=OFF \\
        -DCMAKE_INSTALL_LIBDIR=${EXTERNALS_PREFIX_PATH}/lib \\
        -DCMAKE_INSTALL_PREFIX=${EXTERNALS_PREFIX_PATH}

        BUILD_IN_SOURCE ON
        LOG_DOWNLOAD ON
    )

    add_library(${ext_name} STATIC IMPORTED)
    add_dependencies(${ext_name} extlib_openjpeg)
    set_target_properties(${ext_name} PROPERTIES
        IMPORTED_LOCATION ${OPENJPEG_IMPORTED_LIB}
        INTERFACE_INCLUDE_DIRECTORIES "${EXTERNALS_PREFIX_PATH}/include/openjpeg-2.5;${EXTERNALS_PREFIX_PATH}/include"
        INTERFACE_COMPILE_DEFINITIONS "OPJ_STATIC"
    )

endif()
