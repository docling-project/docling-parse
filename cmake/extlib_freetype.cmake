
message(STATUS "entering in extlib_freetype.cmake")

set(ext_name "freetype")

if(USE_SYSTEM_DEPS)
    # Standard CMake module; provides Freetype::Freetype.
    find_package(Freetype REQUIRED)
    if(NOT TARGET freetype)
        add_library(${ext_name} ALIAS Freetype::Freetype)
    endif()

else()
    include(ExternalProject)
    include(CMakeParseArguments)

    file(MAKE_DIRECTORY ${EXTERNALS_PREFIX_PATH}/include)
    file(MAKE_DIRECTORY ${EXTERNALS_PREFIX_PATH}/include/freetype2)
    file(MAKE_DIRECTORY ${EXTERNALS_PREFIX_PATH}/lib)

    set(FREETYPE_URL https://github.com/freetype/freetype.git)
    set(FREETYPE_TAG VER-2-13-3)
    # GCC/MinGW -> libfreetype.a, MSVC -> freetype.lib (Windows arm64 uses MSVC).
    # DISABLE_FORCE_DEBUG_POSTFIX + Release avoid the "d" postfix, so the name is
    # stable per-toolchain.
    if(MSVC)
        set(FREETYPE_IMPORTED_LIB ${EXTERNALS_PREFIX_PATH}/lib/freetype.lib)
    else()
        set(FREETYPE_IMPORTED_LIB ${EXTERNALS_PREFIX_PATH}/lib/libfreetype.a)
    endif()

    # All optional codec/shaping dependencies are disabled: the renderer only
    # needs outline loading for Type 1 / CFF / TrueType font programs, and a
    # dependency-free static build keeps every wheel platform (incl.
    # windows-arm64) simple.
    ExternalProject_Add(extlib_freetype

        PREFIX extlib_freetype

        UPDATE_COMMAND ""
        GIT_REPOSITORY ${FREETYPE_URL}
        GIT_TAG ${FREETYPE_TAG}

        BUILD_ALWAYS OFF

        INSTALL_DIR ${EXTERNALS_PREFIX_PATH}

        CMAKE_ARGS \\
        -DCMAKE_POSITION_INDEPENDENT_CODE=ON \\
        -DCMAKE_OSX_ARCHITECTURES=${CMAKE_OSX_ARCHITECTURES} \\
        -DCMAKE_C_FLAGS=${ENV_ARCHFLAGS} \\
        # Pin the sub-build type and disable FreeType's forced "d" debug
        # postfix: with the outer CMAKE_BUILD_TYPE=Debug (e.g. the CI checks
        # workflow) FreeType would otherwise install libfreetyped.a and the
        # IMPORTED_LOCATION below would dangle, breaking the final link.
        -DCMAKE_BUILD_TYPE=Release \\
        -DDISABLE_FORCE_DEBUG_POSTFIX=ON \\
        -DBUILD_SHARED_LIBS=OFF \\
        -DFT_DISABLE_ZLIB=ON \\
        -DFT_DISABLE_BZIP2=ON \\
        -DFT_DISABLE_PNG=ON \\
        -DFT_DISABLE_HARFBUZZ=ON \\
        -DFT_DISABLE_BROTLI=ON \\
        -DCMAKE_INSTALL_LIBDIR=${EXTERNALS_PREFIX_PATH}/lib \\
        -DCMAKE_INSTALL_PREFIX=${EXTERNALS_PREFIX_PATH}

        # FreeType's CMakeLists rejects in-source builds, so (unlike the other
        # extlibs) build in ExternalProject's separate binary directory.
        BUILD_IN_SOURCE OFF
        LOG_DOWNLOAD ON
    )

    add_library(${ext_name} STATIC IMPORTED)
    add_dependencies(${ext_name} extlib_freetype)
    set_target_properties(${ext_name} PROPERTIES
        IMPORTED_LOCATION ${FREETYPE_IMPORTED_LIB}
        INTERFACE_INCLUDE_DIRECTORIES "${EXTERNALS_PREFIX_PATH}/include/freetype2;${EXTERNALS_PREFIX_PATH}/include"
    )

endif()
