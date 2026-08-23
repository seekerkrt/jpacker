cmake_minimum_required(VERSION 3.18)

# GNU Make's `CXXFLAGS ?= -O2 -pipe` applies the defaults only when the
# caller did not define CXXFLAGS at all. Record that distinction before
# project() initializes CMAKE_CXX_FLAGS, including an explicitly empty value.
set(_moguet_cxxflags_defined_before_project OFF)
if(DEFINED ENV{CXXFLAGS} OR DEFINED CACHE{CMAKE_CXX_FLAGS})
    set(_moguet_cxxflags_defined_before_project ON)
endif()

project(Moguet LANGUAGES CXX)

include(CTest)

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

function(
    moguet_parse_external_compile_flags
    output_variable
    input_name
    raw_flags
)
    separate_arguments(_moguet_arguments UNIX_COMMAND "${raw_flags}")

    set(_moguet_pending_macro_option "")
    foreach(_moguet_argument IN LISTS _moguet_arguments)
        if(_moguet_argument MATCHES "^--?std=")
            message(
                FATAL_ERROR
                "External ${input_name} contains a C/C++ standard selector "
                "(-std=... or --std=...), which conflicts with Moguet's "
                "reserved strict C++20 contract. Remove the external "
                "standard selector."
            )
        endif()

        if(
            _moguet_argument MATCHES
                "^-DMOGUET_VERSION($|[^A-Za-z0-9_])"
            OR _moguet_argument MATCHES
                "^-UMOGUET_VERSION($|[^A-Za-z0-9_])"
        )
            message(
                FATAL_ERROR
                "External ${input_name} defines or undefines MOGUET_VERSION, "
                "which is reserved for the repository root VERSION file. "
                "Remove the external MOGUET_VERSION -D or -U option."
            )
        endif()

        if(NOT _moguet_pending_macro_option STREQUAL "")
            if(
                _moguet_argument MATCHES
                    "^MOGUET_VERSION($|[^A-Za-z0-9_])"
            )
                message(
                    FATAL_ERROR
                    "External ${input_name} defines or undefines "
                    "MOGUET_VERSION, which is reserved for the repository "
                    "root VERSION file. Remove the external MOGUET_VERSION "
                    "-D or -U option."
                )
            endif()
            set(_moguet_pending_macro_option "")
        endif()

        if(_moguet_argument STREQUAL "-D" OR _moguet_argument STREQUAL "-U")
            set(_moguet_pending_macro_option "${_moguet_argument}")
        endif()
    endforeach()

    # Keep every parsed argv element in one SHELL: group so CMake's compile
    # option de-duplication cannot collapse repeated tokens such as
    # `-D A=1 -D B=2`. This only re-encodes argv for CMake's parser; no shell
    # process is invoked.
    set(_moguet_shell_group "")
    foreach(_moguet_argument IN LISTS _moguet_arguments)
        string(REPLACE "\\" "\\\\" _moguet_quoted "${_moguet_argument}")
        string(REPLACE "'" "'\\''" _moguet_quoted "${_moguet_quoted}")
        if(_moguet_shell_group STREQUAL "")
            set(_moguet_shell_group "'${_moguet_quoted}'")
        else()
            string(APPEND _moguet_shell_group " '${_moguet_quoted}'")
        endif()
    endforeach()

    if(_moguet_shell_group STREQUAL "")
        set("${output_variable}" "" PARENT_SCOPE)
    else()
        # A literal semicolon must remain escaped until the SHELL: group reaches
        # target_compile_options() as one CMake list element.
        string(REPLACE ";" "\\;" _moguet_shell_group "${_moguet_shell_group}")
        set(
            "${output_variable}"
            "SHELL:${_moguet_shell_group}"
            PARENT_SCOPE
        )
    endif()
endfunction()

moguet_parse_external_compile_flags(
    _moguet_cppflag_option_group
    "MOGUET_CPPFLAGS (initialized from CPPFLAGS)"
    "${MOGUET_CPPFLAGS}"
)
moguet_parse_external_compile_flags(
    _moguet_cxxflag_option_group
    "CMAKE_CXX_FLAGS (including CXXFLAGS initialization)"
    "${CMAKE_CXX_FLAGS}"
)

add_library(moguet_external_cppflags INTERFACE)
if(NOT _moguet_cppflag_option_group STREQUAL "")
    target_compile_options(
        moguet_external_cppflags
        INTERFACE
        "${_moguet_cppflag_option_group}"
    )
endif()

if(NOT DEFINED MOGUET_ENABLE_DEFAULT_COMPILE_OPTIONS)
    if(_moguet_cxxflags_defined_before_project)
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

# These two translation units have undefined-symbol firewalls. Keep the exact
# objects consumed by production addressable so CTest can inspect them without
# substituting a broad archive, a final executable, or a separately compiled
# probe object.
set(
    MOGUET_UNIFIED_PLAN_PROJECTION_SOURCE
    source/unified_plan_projection.cpp
)
set(
    MOGUET_UNIFIED_PLAN_RENDERER_SOURCE
    source/unified_plan_renderer.cpp
)
set(
    MOGUET_ADDRESSABLE_PRODUCTION_SOURCES
    "${MOGUET_UNIFIED_PLAN_PROJECTION_SOURCE}"
    "${MOGUET_UNIFIED_PLAN_RENDERER_SOURCE}"
)
set(
    _moguet_unique_addressable_production_sources
    ${MOGUET_ADDRESSABLE_PRODUCTION_SOURCES}
)
list(REMOVE_DUPLICATES _moguet_unique_addressable_production_sources)
list(
    LENGTH
    MOGUET_ADDRESSABLE_PRODUCTION_SOURCES
    _moguet_addressable_production_source_count
)
list(
    LENGTH
    _moguet_unique_addressable_production_sources
    _moguet_unique_addressable_production_source_count
)
if(
    NOT _moguet_addressable_production_source_count EQUAL 2
    OR NOT _moguet_addressable_production_source_count EQUAL
        _moguet_unique_addressable_production_source_count
)
    message(
        FATAL_ERROR
        "MOGUET_ADDRESSABLE_PRODUCTION_SOURCES must contain exactly two "
        "unique translation units"
    )
endif()
foreach(
    _moguet_addressable_production_source
    IN LISTS MOGUET_ADDRESSABLE_PRODUCTION_SOURCES
)
    list(
        FIND
        MOGUET_PRODUCTION_SOURCES
        "${_moguet_addressable_production_source}"
        _moguet_addressable_production_source_index
    )
    if(_moguet_addressable_production_source_index EQUAL -1)
        message(
            FATAL_ERROR
            "Addressable production source is absent from the canonical "
            "manifest: ${_moguet_addressable_production_source}"
        )
    endif()
endforeach()

set(MOGUET_PRODUCTION_OBJECT_SOURCES ${MOGUET_PRODUCTION_SOURCES})
list(
    REMOVE_ITEM
    MOGUET_PRODUCTION_OBJECT_SOURCES
    ${MOGUET_ADDRESSABLE_PRODUCTION_SOURCES}
)
set(
    _moguet_partitioned_production_sources
    ${MOGUET_PRODUCTION_OBJECT_SOURCES}
    ${MOGUET_ADDRESSABLE_PRODUCTION_SOURCES}
)
set(
    _moguet_unique_partitioned_production_sources
    ${_moguet_partitioned_production_sources}
)
list(REMOVE_DUPLICATES _moguet_unique_partitioned_production_sources)
list(
    LENGTH
    _moguet_partitioned_production_sources
    _moguet_partitioned_production_source_count
)
list(
    LENGTH
    _moguet_unique_partitioned_production_sources
    _moguet_unique_partitioned_production_source_count
)
if(
    NOT _moguet_partitioned_production_source_count EQUAL
        _moguet_unique_partitioned_production_source_count
)
    message(FATAL_ERROR "Production OBJECT source partition overlaps")
endif()
set(_moguet_sorted_production_sources ${MOGUET_PRODUCTION_SOURCES})
set(
    _moguet_sorted_partitioned_production_sources
    ${_moguet_partitioned_production_sources}
)
list(SORT _moguet_sorted_production_sources)
list(SORT _moguet_sorted_partitioned_production_sources)
if(
    NOT "${_moguet_sorted_production_sources}" STREQUAL
        "${_moguet_sorted_partitioned_production_sources}"
)
    message(
        FATAL_ERROR
        "Production OBJECT sources are not an exact partition of "
        "MOGUET_PRODUCTION_SOURCES"
    )
endif()

add_library(
    moguet_production_objects
    OBJECT
    ${MOGUET_PRODUCTION_OBJECT_SOURCES}
)
add_library(
    moguet_unified_plan_projection_object
    OBJECT
    "${MOGUET_UNIFIED_PLAN_PROJECTION_SOURCE}"
)
add_library(
    moguet_unified_plan_renderer_object
    OBJECT
    "${MOGUET_UNIFIED_PLAN_RENDERER_SOURCE}"
)

function(moguet_configure_production_object_target target_name)
    set_target_properties(
        "${target_name}"
        PROPERTIES
            CXX_STANDARD 20
            CXX_STANDARD_REQUIRED YES
            CXX_EXTENSIONS NO
    )
    target_link_libraries(
        "${target_name}"
        PRIVATE
            moguet_external_cppflags
            moguet_build_contract
            moguet_alpm_compile_contract
            # Consume CURL::libcurl's complete compile usage requirements.
            # The executable remains the explicit actual curl link owner.
            CURL::libcurl
            nlohmann_json::nlohmann_json
            moguet_toml_headers
    )
endfunction()

moguet_configure_production_object_target(moguet_production_objects)
moguet_configure_production_object_target(
    moguet_unified_plan_projection_object
)
moguet_configure_production_object_target(
    moguet_unified_plan_renderer_object
)

# Raw TARGET_OBJECTS consumption does not transfer the OBJECT target's PRIVATE
# curl dependency to the final link, so keep the actual linkage explicit.
add_executable(
    moguet
    $<TARGET_OBJECTS:moguet_production_objects>
    $<TARGET_OBJECTS:moguet_unified_plan_projection_object>
    $<TARGET_OBJECTS:moguet_unified_plan_renderer_object>
)
target_link_libraries(
    moguet
    PRIVATE
        CURL::libcurl
        PkgConfig::ALPM
)

if(BUILD_TESTING)
    include(cmake/MoguetTests.cmake)
endif()
