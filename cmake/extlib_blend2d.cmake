
message(STATUS "entering in extlib_blend2d.cmake")

# Blend2D requires CMake >= 3.14 (FetchContent_MakeAvailable).
# The project's actual build environment satisfies this requirement.

if(USE_SYSTEM_DEPS)
    find_package(blend2d REQUIRED CONFIG)
    # The installed target is blend2d::blend2d; create a plain alias so the
    # name "blend2d" can be used uniformly in the DEPENDENCIES list.
    if(NOT TARGET blend2d)
        add_library(blend2d ALIAS blend2d::blend2d)
    endif()
else()
    include(FetchContent)

    # Build blend2d as a static library.
    # blend2d's own CMakeLists.txt will automatically fetch AsmJit via
    # FetchContent when ASMJIT_DIR is not set.
    set(BLEND2D_STATIC TRUE CACHE BOOL "Build blend2d as a static library" FORCE)

    FetchContent_Declare(
        blend2d
        GIT_REPOSITORY https://github.com/blend2d/blend2d.git
        GIT_TAG        master
        GIT_SHALLOW    TRUE
    )

    FetchContent_MakeAvailable(blend2d)
    # After this call the CMake target "blend2d" (and alias "blend2d::blend2d")
    # is available for linking. AsmJit is transitively linked.
endif()
