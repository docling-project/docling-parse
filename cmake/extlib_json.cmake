
message(STATUS "entering in extlib_json.cmake")

set(ext_name "json")

if(USE_SYSTEM_DEPS)
    # this will define the nlohmann_json::nlohmann_json target
    find_package(nlohmann_json REQUIRED)

    add_library(${ext_name} INTERFACE IMPORTED)
    add_dependencies(${ext_name} nlohmann_json::nlohmann_json)

else()

    include(ExternalProject)
    include(CMakeParseArguments)

    set(JSON_URL https://github.com/nlohmann/json.git)
    # set(JSON_TAG v3.11.3)
    set(JSON_TAG v3.12.0)
    ExternalProject_Add(extlib_json
        
        PREFIX extlib_json

        GIT_REPOSITORY ${JSON_URL}
        GIT_TAG ${JSON_TAG}

        UPDATE_COMMAND ""
        CONFIGURE_COMMAND ""

        BUILD_COMMAND ""
        BUILD_ALWAYS OFF

        INSTALL_DIR     ${EXTERNALS_PREFIX_PATH}
        INSTALL_COMMAND ${CMAKE_COMMAND} -E copy_directory <SOURCE_DIR>/include/ ${EXTERNALS_PREFIX_PATH}/include/
    )

    add_library(${ext_name} INTERFACE IMPORTED)
    add_dependencies(${ext_name} extlib_json)
    set_target_properties(${ext_name} PROPERTIES INTERFACE_INCLUDE_DIRECTORIES ${EXTERNALS_PREFIX_PATH}/include)

endif()


