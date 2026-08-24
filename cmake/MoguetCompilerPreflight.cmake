cmake_minimum_required(VERSION 3.18)

foreach(
    _moguet_required_variable
    IN ITEMS
        MOGUET_COMPILER_PREFLIGHT_BUILD_DIR
        MOGUET_REQUESTED_CXX
)
    if(NOT DEFINED ${_moguet_required_variable})
        message(
            FATAL_ERROR
            "Missing compiler preflight input: ${_moguet_required_variable}"
        )
    endif()
endforeach()

get_filename_component(
    _moguet_build_directory
    "${MOGUET_COMPILER_PREFLIGHT_BUILD_DIR}"
    ABSOLUTE
)
set(_moguet_cache_file "${_moguet_build_directory}/CMakeCache.txt")
if(NOT EXISTS "${_moguet_cache_file}")
    return()
endif()
if(IS_DIRECTORY "${_moguet_cache_file}")
    message(FATAL_ERROR "CMake cache is not a file: ${_moguet_cache_file}")
endif()

file(
    STRINGS
    "${_moguet_cache_file}"
    _moguet_compiler_cache_entries
    REGEX "^CMAKE_CXX_COMPILER:[^=]*="
)
list(LENGTH _moguet_compiler_cache_entries _moguet_compiler_entry_count)
if(_moguet_compiler_entry_count EQUAL 0)
    return()
endif()
if(NOT _moguet_compiler_entry_count EQUAL 1)
    message(
        FATAL_ERROR
        "CMake cache has an ambiguous C++ compiler identity: "
        "${_moguet_cache_file}"
    )
endif()

list(GET _moguet_compiler_cache_entries 0 _moguet_compiler_cache_entry)
string(
    REGEX REPLACE
    "^[^=]*="
    ""
    _moguet_configured_cxx
    "${_moguet_compiler_cache_entry}"
)

function(_moguet_resolve_compiler output_variable label compiler_value)
    set(_moguet_compiler_command ${compiler_value})
    if(NOT _moguet_compiler_command)
        message(FATAL_ERROR "${label} C++ compiler is empty")
    endif()
    list(GET _moguet_compiler_command 0 _moguet_compiler_executable)

    if(IS_ABSOLUTE "${_moguet_compiler_executable}")
        set(_moguet_compiler_path "${_moguet_compiler_executable}")
    else()
        unset(_moguet_compiler_path CACHE)
        find_program(
            _moguet_compiler_path
            NAMES "${_moguet_compiler_executable}"
        )
        if(NOT _moguet_compiler_path)
            message(
                FATAL_ERROR
                "Unable to resolve ${label} C++ compiler: "
                "${_moguet_compiler_executable}"
            )
        endif()
    endif()

    if(
        NOT EXISTS "${_moguet_compiler_path}"
        OR IS_DIRECTORY "${_moguet_compiler_path}"
    )
        message(
            FATAL_ERROR
            "${label} C++ compiler is unavailable: ${_moguet_compiler_path}"
        )
    endif()
    get_filename_component(
        _moguet_compiler_realpath
        "${_moguet_compiler_path}"
        REALPATH
    )
    set("${output_variable}" "${_moguet_compiler_realpath}" PARENT_SCOPE)
endfunction()

_moguet_resolve_compiler(
    _moguet_configured_cxx_realpath
    "configured"
    "${_moguet_configured_cxx}"
)
_moguet_resolve_compiler(
    _moguet_requested_cxx_realpath
    "requested"
    "${MOGUET_REQUESTED_CXX}"
)

if(
    NOT _moguet_configured_cxx_realpath STREQUAL
        _moguet_requested_cxx_realpath
)
    message(
        FATAL_ERROR
        "Refusing to reuse ${_moguet_build_directory} with a different "
        "C++ compiler. Configured: ${_moguet_configured_cxx_realpath}; "
        "requested: ${_moguet_requested_cxx_realpath}. Remove and recreate "
        "this build directory explicitly before changing compilers; Moguet "
        "will not delete it automatically."
    )
endif()
