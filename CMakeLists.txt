cmake_minimum_required(VERSION 3.26)
project(dusklight_dialogue_options CXX)

if (NOT DUSKLIGHT_VERSION)
    set(DUSKLIGHT_VERSION "v2.0.0")
endif ()

include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/FetchDusklight.cmake")
add_subdirectory("${DUSKLIGHT_DIR}/sdk" dusklight-sdk EXCLUDE_FROM_ALL)

add_mod(dusklight_dialogue_options
        FEATURES game
        SOURCES src/mod.cpp
        MOD_JSON mod.json
        RES_DIR res
)

if (MSVC)
    target_link_options(dusklight_dialogue_options PRIVATE "/OPT:NOREF")
endif ()
