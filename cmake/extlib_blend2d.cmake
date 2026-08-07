
message(STATUS "entering in extlib_blend2d.cmake")

if(USE_SYSTEM_DEPS)
    find_package(blend2d REQUIRED CONFIG)
    # The installed target is blend2d::blend2d; create a plain alias so the
    # name "blend2d" can be used uniformly in the DEPENDENCIES list.
    if(NOT TARGET blend2d)
        add_library(blend2d ALIAS blend2d::blend2d)
    endif()
else()
    include(FetchContent)

    # --- AsmJit (embedded by blend2d for JIT pipeline generation) ---
    # Use the inline form of FetchContent_Populate (not deprecated) to download
    # the source only. blend2d will add it via add_subdirectory using ASMJIT_DIR.
    FetchContent_Populate(
        asmjit
        GIT_REPOSITORY https://github.com/asmjit/asmjit.git
        GIT_TAG        master
        GIT_SHALLOW    TRUE
    )
    # Point blend2d to the downloaded asmjit source.
    set(ASMJIT_DIR "${asmjit_SOURCE_DIR}" CACHE PATH "Path to AsmJit source" FORCE)

    # --- Blend2D ---
    # BLEND2D_STATIC      → produce libblend2d.a
    # BLEND2D_NO_INSTALL  → skip install rules (we consume the build-tree target)
    # BLEND2D_TEST        → skip building test/sample binaries
    # BLEND2D_EXTERNAL_ASMJIT stays OFF (default) so blend2d embeds asmjit with
    # optimised flags (ASMJIT_NO_FOREIGN, ASMJIT_NO_STDCXX, etc.).
    set(BLEND2D_STATIC     TRUE  CACHE BOOL "Build blend2d as a static library" FORCE)
    set(BLEND2D_NO_INSTALL TRUE  CACHE BOOL "Disable blend2d install rules"     FORCE)
    set(BLEND2D_TEST       FALSE CACHE BOOL "Disable blend2d tests"              FORCE)
    set(BLEND2D_DEMOS      FALSE CACHE BOOL "Disable blend2d demos"              FORCE)
    # --- Optional blend2d knobs (uncomment to override) ---
    # set(BLEND2D_NO_JIT         FALSE CACHE BOOL "Disable JIT pipeline generation (not recommended)" FORCE)
    # set(BLEND2D_NO_JIT_LOGGING FALSE CACHE BOOL "Disable JIT logging (reduces binary size)" FORCE)
    # set(BLEND2D_NO_STDCXX      FALSE CACHE BOOL "Disable linking to C++ stdlib" FORCE)
    # set(BLEND2D_NO_TLS         FALSE CACHE BOOL "Disable use of thread-local storage" FORCE)
    # set(BLEND2D_NO_FUTEX       FALSE CACHE BOOL "Disable use of futexes" FORCE)
    # set(BLEND2D_NO_NATVIS      FALSE CACHE BOOL "Disable natvis debug visualisers (MSVC)" FORCE)
    # set(BLEND2D_EXTERNAL_ASMJIT FALSE CACHE BOOL "Use an installed asmjit via find_package instead of the embedded copy" FORCE)
    # set(BLEND2D_SANITIZE       ""    CACHE STRING "Sanitizers to enable (e.g. address,undefined)" FORCE)
    # set(BLEND2D_SANITIZE_OPTS  ""    CACHE STRING "Extra flags passed to the sanitizer" FORCE)
    # set(ASMJIT_EMBED           TRUE  CACHE BOOL "Embed asmjit into blend2d (set automatically when BLEND2D_EXTERNAL_ASMJIT=OFF)" FORCE)

    FetchContent_Declare(
        blend2d
        GIT_REPOSITORY https://github.com/blend2d/blend2d.git
        # Pinned so the (renamed, snake_case) C++ API cannot drift under us;
        # bump deliberately. This is the commit the font-rendering code was
        # validated against.
        GIT_TAG        6dbc2cefbc996379e07104e34519a440b49b15d7
    )
    FetchContent_MakeAvailable(blend2d)
    # Release wheels must not contain Blend2D's debug assertion path. Blend2D
    # normally infers BL_BUILD_RELEASE from NDEBUG, but make that mode explicit
    # for non-Debug builds so embedded builds do not depend on generator-specific
    # CMAKE_BUILD_TYPE propagation.
    target_compile_definitions(
        blend2d
        PUBLIC $<$<NOT:$<CONFIG:Debug>>:BL_BUILD_RELEASE>
    )

    # Blend2D defines bl_runtime_assertion_failure() unconditionally in
    # runtime.cpp. BL_BUILD_RELEASE removes assertion call sites, but the helper
    # can still be retained from the static archive unless the final extension
    # link can discard unused sections.
    set(BLEND2D_GNU_GC_SECTIONS "$<AND:$<NOT:$<CONFIG:Debug>>,$<OR:$<PLATFORM_ID:Linux>,$<AND:$<PLATFORM_ID:Windows>,$<CXX_COMPILER_ID:GNU>>>>")
    target_compile_options(
        blend2d
        PRIVATE "$<${BLEND2D_GNU_GC_SECTIONS}:-ffunction-sections;-fdata-sections>"
    )
    target_link_options(
        blend2d
        INTERFACE "$<${BLEND2D_GNU_GC_SECTIONS}:-Wl,--gc-sections>"
    )

    # MinGW auto-exports global symbols from DLL/PYD links. If symbols from the
    # static Blend2D archive are exported, they become linker roots and section
    # GC cannot remove bl_runtime_assertion_failure(). Hide static-archive
    # symbols from the final Windows GNU extension export table while preserving
    # explicitly exported symbols such as the pybind module initializer.
    set(BLEND2D_MINGW_EXCLUDE_STATIC_EXPORTS "$<AND:$<NOT:$<CONFIG:Debug>>,$<PLATFORM_ID:Windows>,$<CXX_COMPILER_ID:GNU>>")
    target_link_options(
        blend2d
        INTERFACE "$<${BLEND2D_MINGW_EXCLUDE_STATIC_EXPORTS}:-Wl,--exclude-libs,ALL>"
    )
    # FetchContent creates the target "blend2d" (and alias blend2d::blend2d).
endif()
