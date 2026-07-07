
message(STATUS "entering in extlib_jpeg.cmake")

set(ext_name "jpeg")

if(USE_SYSTEM_DEPS)
    find_package(PkgConfig)
    pkg_check_modules(libjpeg REQUIRED IMPORTED_TARGET libjpeg)

    add_library(${ext_name} ALIAS PkgConfig::libjpeg)
    #set_target_properties(${ext_name} PROPERTIES INTERFACE_LINK_LIBRARIES "${libjpeg_LIBRARIES}")
    #set_target_properties(${ext_name} PROPERTIES INTERFACE_LINK_DIRECTORIES "${libjpeg_LIBRARY_DIRS}")
    #set_target_properties(${ext_name} PROPERTIES INTERFACE_INCLUDE_DIRECTORIES "${libjpeg_INCLUDEDIR}")

else()
    include(ExternalProject)
    include(CMakeParseArguments)

    set(JPEG_URL https://github.com/libjpeg-turbo/libjpeg-turbo.git)
    # set(JPEG_TAG 3.0.3)
    set(JPEG_TAG 3.1.1)

    # -A ARM64 (CMAKE_GENERATOR_PLATFORM) only sets the MSBuild target platform.
    # CMAKE_SYSTEM_PROCESSOR is a separate variable that CMake always resets to
    # the host-reported architecture unless CMAKE_SYSTEM_NAME is also given (see
    # CMakeDetermineSystem.cmake); on this CI runner the host reports AMD64. Without
    # forcing both, libjpeg-turbo's own CMakeLists picks its x86_64 NASM SIMD
    # backend and fails to link against the ARM64 target.
    set(JPEG_EXTRA_CMAKE_ARGS "")
    if(WIN32 AND CMAKE_GENERATOR_PLATFORM STREQUAL "ARM64")
        set(JPEG_EXTRA_CMAKE_ARGS -DCMAKE_SYSTEM_NAME=Windows -DCMAKE_SYSTEM_PROCESSOR=ARM64)
    endif()

    ExternalProject_Add(extlib_jpeg

        PREFIX extlib_jpeg

        UPDATE_COMMAND ""
        GIT_REPOSITORY ${JPEG_URL}
        GIT_TAG ${JPEG_TAG}

        BUILD_ALWAYS OFF

        INSTALL_DIR ${EXTERNALS_PREFIX_PATH}

        CMAKE_ARGS \\
        -DCMAKE_POSITION_INDEPENDENT_CODE=ON \\
        -DCMAKE_OSX_ARCHITECTURES=${CMAKE_OSX_ARCHITECTURES} \\
        -DCMAKE_C_FLAGS=${ENV_ARCHFLAGS} \\
        -DARMV8_BUILD=ON \\
        -DCMAKE_INSTALL_LIBDIR=${EXTERNALS_PREFIX_PATH}/lib \\
        -DCMAKE_INSTALL_PREFIX=${EXTERNALS_PREFIX_PATH} \\
        ${JPEG_EXTRA_CMAKE_ARGS}

        BUILD_IN_SOURCE ON
        LOG_DOWNLOAD ON
        # LOG_BUILD ON
    )

    # GCC/MinGW and MSVC name the installed static library differently:
    # GCC/MinGW -> libjpeg.a, MSVC -> jpeg-static.lib. The Windows arm64 wheel
    # builds with MSVC (Visual Studio generator), so pick the name per-toolchain.
    if(MSVC)
        set(JPEG_IMPORTED_LIB ${EXTERNALS_PREFIX_PATH}/lib/jpeg-static.lib)
    else()
        set(JPEG_IMPORTED_LIB ${EXTERNALS_PREFIX_PATH}/lib/libjpeg.a)
    endif()

    add_library(${ext_name} STATIC IMPORTED)
    add_dependencies(${ext_name} extlib_jpeg)
    set_target_properties(${ext_name} PROPERTIES
        IMPORTED_LOCATION ${JPEG_IMPORTED_LIB}
        INTERFACE_INCLUDE_DIRECTORIES ${EXTERNALS_PREFIX_PATH}/include
    )

endif()
