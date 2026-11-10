
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
    # v2.5.3 (and also the latest tag v2.5.4) is affected by OSV-2025-219, a
    # heap-buffer-overflow WRITE in opj_j2k_read_tile_header /
    # opj_j2k_read_sod (OSS-Fuzz 403673832, severity HIGH). The fix is merged
    # on master but no release tag contains it yet, so pin the fix commit
    # directly: d33cbecc is the merge of uclouvain/openjpeg#1621 ("Fix heap
    # buffer overflow in opj_j2k_read_sod") and contains the other OSV fixed
    # commit 91d08b1 as a parent. Replace this with the v2.5.5+ tag once one
    # is released.
    set(OPENJPEG_TAG d33cbecc148d3affcdf403211fddc2cc5d442379)
    # GCC/MinGW -> libopenjp2.a, MSVC -> openjp2.lib (Windows arm64 uses MSVC).
    if(MSVC)
        set(OPENJPEG_IMPORTED_LIB ${EXTERNALS_PREFIX_PATH}/lib/openjp2.lib)
    else()
        set(OPENJPEG_IMPORTED_LIB ${EXTERNALS_PREFIX_PATH}/lib/libopenjp2.a)
    endif()

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
