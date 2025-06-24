
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
        -DCMAKE_INSTALL_PREFIX=${EXTERNALS_PREFIX_PATH}

        BUILD_IN_SOURCE ON
        LOG_DOWNLOAD ON
        # LOG_BUILD ON
    )

    add_library(${ext_name} STATIC IMPORTED)
    add_dependencies(${ext_name} extlib_jpeg)
    set_target_properties(${ext_name} PROPERTIES
        IMPORTED_LOCATION ${EXTERNALS_PREFIX_PATH}/lib/libjpeg.a
        INTERFACE_INCLUDE_DIRECTORIES ${EXTERNALS_PREFIX_PATH}/include
    )

endif()
