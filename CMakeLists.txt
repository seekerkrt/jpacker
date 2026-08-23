cmake_minimum_required(VERSION 3.18)

# GNU Make's `CXXFLAGS ?= -O2 -pipe` applies the defaults only when the
# caller did not define CXXFLAGS at all. Record that distinction before
# project() initializes CMAKE_CXX_FLAGS, including an explicitly empty value.
set(_moguet_cxxflags_defined_before_project OFF)
if(DEFINED ENV{CXXFLAGS} OR DEFINED CMAKE_CXX_FLAGS)
    set(_moguet_cxxflags_defined_before_project ON)
endif()

project(Moguet LANGUAGES CXX)

include(cmake/MoguetSources.cmake)

find_package(CURL REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(ALPM REQUIRED IMPORTED_TARGET libalpm)
find_path(
    TOMLPLUSPLUS_INCLUDE_DIR
    NAMES toml++/toml.hpp
    REQUIRED
)

set(
    MOGUET_LOCALE_DIRECTORY
    "/usr/share/locale"
    CACHE STRING
    "Absolute runtime directory containing Moguet message catalogs"
)
if(NOT IS_ABSOLUTE "${MOGUET_LOCALE_DIRECTORY}")
    message(
        FATAL_ERROR
        "MOGUET_LOCALE_DIRECTORY must be an absolute path: "
        "${MOGUET_LOCALE_DIRECTORY}"
    )
endif()

set(MOGUET_GENERATED_INCLUDE_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated")
file(MAKE_DIRECTORY "${MOGUET_GENERATED_INCLUDE_DIR}")

# configure_file() keeps VERSION in CMake's configure dependencies. A missing
# VERSION remains an error, while an existing empty file retains the legacy
# "unknown" fallback.
configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/VERSION"
    "${MOGUET_GENERATED_INCLUDE_DIR}/VERSION"
    COPYONLY
)
file(READ "${MOGUET_GENERATED_INCLUDE_DIR}/VERSION" MOGUET_VERSION)
string(REGEX REPLACE "[ \t\r\n]+" " " MOGUET_VERSION "${MOGUET_VERSION}")
string(STRIP "${MOGUET_VERSION}" MOGUET_VERSION)
if(MOGUET_VERSION STREQUAL "")
    set(MOGUET_VERSION "unknown")
endif()

configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/localization_config.hpp.in"
    "${MOGUET_GENERATED_INCLUDE_DIR}/localization_config.hpp"
    @ONLY
)

# CMake initializes CMAKE_CXX_FLAGS and the linker flag caches from CXXFLAGS
# and LDFLAGS. It does not consume CPPFLAGS, so capture that external input
# once with the same cache semantics. Change it in an existing tree with
# -DMOGUET_CPPFLAGS:STRING=..., or configure a fresh tree.
set(
    MOGUET_CPPFLAGS
    "$ENV{CPPFLAGS}"
    CACHE STRING
    "External preprocessor flags initialized from environment CPPFLAGS"
)

add_library(moguet_external_cppflags INTERFACE)
if(NOT MOGUET_CPPFLAGS STREQUAL "")
    target_compile_options(
        moguet_external_cppflags
        INTERFACE
        "SHELL:${MOGUET_CPPFLAGS}"
    )
endif()

if(NOT DEFINED MOGUET_ENABLE_DEFAULT_COMPILE_OPTIONS)
    if(
        _moguet_cxxflags_defined_before_project
        OR NOT CMAKE_CXX_FLAGS STREQUAL ""
        OR NOT CMAKE_BUILD_TYPE STREQUAL ""
        OR CMAKE_CONFIGURATION_TYPES
    )
        set(_moguet_enable_default_compile_options OFF)
    else()
        set(_moguet_enable_default_compile_options ON)
    endif()
    set(
        MOGUET_ENABLE_DEFAULT_COMPILE_OPTIONS
        "${_moguet_enable_default_compile_options}"
        CACHE BOOL
        "Use Moguet's -O2 and -pipe defaults for a plain developer build"
    )
endif()

add_library(moguet_build_contract INTERFACE)
target_compile_features(moguet_build_contract INTERFACE cxx_std_20)
target_compile_options(moguet_build_contract INTERFACE -Wall -Wextra)
if(
    MOGUET_ENABLE_DEFAULT_COMPILE_OPTIONS
    AND CMAKE_CXX_FLAGS STREQUAL ""
    AND CMAKE_BUILD_TYPE STREQUAL ""
    AND NOT CMAKE_CONFIGURATION_TYPES
)
    target_compile_options(moguet_build_contract INTERFACE -O2 -pipe)
endif()
target_compile_definitions(
    moguet_build_contract
    INTERFACE
    "MOGUET_VERSION=\"${MOGUET_VERSION}\""
)
target_include_directories(
    moguet_build_contract
    INTERFACE
    "${MOGUET_GENERATED_INCLUDE_DIR}"
)

# Arch's tomlplusplus package also exports a shared-library target. Moguet's
# current production contract is header-only, so expose includes and no link
# dependency or TOML_HEADER_ONLY/TOML_SHARED_LIB definition changes.
add_library(moguet_toml_headers INTERFACE)
target_include_directories(
    moguet_toml_headers
    INTERFACE
    "${TOMLPLUSPLUS_INCLUDE_DIR}"
)

# The production objects consume curl headers, but the final executable alone
# owns the direct dynamic link. Avoid carrying CURL::libcurl through the OBJECT
# target and then repeating it on the final link command.
add_library(moguet_curl_compile_contract INTERFACE)
target_include_directories(
    moguet_curl_compile_contract
    INTERFACE
    ${CURL_INCLUDE_DIRS}
)

# Production compilation needs libalpm's pkg-config usage requirements, while
# the final executable owns the real libalpm link. Keeping these separate
# leaves future ALPM-stub test profiles free of the real library dependency.
add_library(moguet_alpm_compile_contract INTERFACE)
if(ALPM_INCLUDE_DIRS)
    target_include_directories(
        moguet_alpm_compile_contract
        INTERFACE
        ${ALPM_INCLUDE_DIRS}
    )
endif()
if(ALPM_CFLAGS_OTHER)
    target_compile_options(
        moguet_alpm_compile_contract
        INTERFACE
        ${ALPM_CFLAGS_OTHER}
    )
endif()

add_library(moguet_production_objects OBJECT ${MOGUET_PRODUCTION_SOURCES})
set_target_properties(
    moguet_production_objects
    PROPERTIES
        CXX_STANDARD 20
        CXX_STANDARD_REQUIRED YES
        CXX_EXTENSIONS NO
)
target_link_libraries(
    moguet_production_objects
    PRIVATE
        moguet_external_cppflags
        moguet_build_contract
        moguet_alpm_compile_contract
        moguet_curl_compile_contract
        nlohmann_json::nlohmann_json
        moguet_toml_headers
)

add_executable(moguet $<TARGET_OBJECTS:moguet_production_objects>)
target_link_libraries(
    moguet
    PRIVATE
        CURL::libcurl
        PkgConfig::ALPM
)
