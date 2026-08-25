cmake_minimum_required(VERSION 3.18)

if(NOT DEFINED MOGUET_COMPILE_COMMANDS_BUILD_DIR)
    message(FATAL_ERROR "MOGUET_COMPILE_COMMANDS_BUILD_DIR is required")
endif()
if(NOT IS_ABSOLUTE "${MOGUET_COMPILE_COMMANDS_BUILD_DIR}")
    message(
        FATAL_ERROR
        "MOGUET_COMPILE_COMMANDS_BUILD_DIR must be absolute: "
        "${MOGUET_COMPILE_COMMANDS_BUILD_DIR}"
    )
endif()

set(
    _moguet_compile_commands_cache
    "${MOGUET_COMPILE_COMMANDS_BUILD_DIR}/CMakeCache.txt"
)
if(NOT EXISTS "${_moguet_compile_commands_cache}")
    message(
        FATAL_ERROR
        "Configured CMake cache is unavailable: "
        "${_moguet_compile_commands_cache}"
    )
endif()

file(
    STRINGS
    "${_moguet_compile_commands_cache}"
    _moguet_compile_commands_option_lines
    REGEX "^MOGUET_DEVELOPER_COMPILE_COMMANDS_LINK:[^=]*="
)
list(LENGTH _moguet_compile_commands_option_lines _moguet_option_line_count)
if(NOT _moguet_option_line_count EQUAL 1)
    message(
        FATAL_ERROR
        "Configured CMake cache has an invalid developer compile database option"
    )
endif()
list(GET _moguet_compile_commands_option_lines 0 _moguet_option_line)
string(
    REGEX REPLACE
    "^[^=]*="
    ""
    _moguet_compile_commands_option
    "${_moguet_option_line}"
)
if(NOT _moguet_compile_commands_option)
    return()
endif()

file(
    STRINGS
    "${_moguet_compile_commands_cache}"
    _moguet_source_directory_lines
    REGEX "^CMAKE_HOME_DIRECTORY:INTERNAL="
)
list(LENGTH _moguet_source_directory_lines _moguet_source_line_count)
if(NOT _moguet_source_line_count EQUAL 1)
    message(
        FATAL_ERROR
        "Configured CMake cache has an invalid source directory authority"
    )
endif()
list(GET _moguet_source_directory_lines 0 _moguet_source_directory_line)
string(
    REGEX REPLACE
    "^[^=]*="
    ""
    _moguet_source_directory
    "${_moguet_source_directory_line}"
)
if(NOT IS_ABSOLUTE "${_moguet_source_directory}")
    message(
        FATAL_ERROR
        "Configured CMake source directory is not absolute: "
        "${_moguet_source_directory}"
    )
endif()

set(
    _moguet_compile_commands_source
    "${MOGUET_COMPILE_COMMANDS_BUILD_DIR}/compile_commands.json"
)
if(
    NOT EXISTS "${_moguet_compile_commands_source}"
    OR IS_DIRECTORY "${_moguet_compile_commands_source}"
    OR IS_SYMLINK "${_moguet_compile_commands_source}"
)
    message(
        FATAL_ERROR
        "Generated compile database is unavailable or unsafe: "
        "${_moguet_compile_commands_source}"
    )
endif()

set(
    _moguet_compile_commands_artifact
    "${_moguet_source_directory}/compile_commands.json"
)
if(
    IS_DIRECTORY "${_moguet_compile_commands_artifact}"
    AND NOT IS_SYMLINK "${_moguet_compile_commands_artifact}"
)
    message(
        FATAL_ERROR
        "Refusing to replace the compile database artifact because it "
        "is a directory: ${_moguet_compile_commands_artifact}"
    )
endif()

file(
    RELATIVE_PATH
    _moguet_compile_commands_link_target
    "${_moguet_source_directory}"
    "${_moguet_compile_commands_source}"
)
string(
    RANDOM
    LENGTH 16
    ALPHABET 0123456789abcdef
    _moguet_compile_commands_suffix
)
set(
    _moguet_compile_commands_temporary_link
    "${_moguet_source_directory}/.compile_commands.json.${_moguet_compile_commands_suffix}.tmp"
)
file(
    CREATE_LINK
    "${_moguet_compile_commands_link_target}"
    "${_moguet_compile_commands_temporary_link}"
    SYMBOLIC
    RESULT _moguet_compile_commands_link_result
)
if(NOT _moguet_compile_commands_link_result STREQUAL "0")
    message(
        FATAL_ERROR
        "Unable to prepare the developer compile database link: "
        "${_moguet_compile_commands_link_result}"
    )
endif()

execute_process(
    COMMAND
        "${CMAKE_COMMAND}" -E rename
        "${_moguet_compile_commands_temporary_link}"
        "${_moguet_compile_commands_artifact}"
    RESULT_VARIABLE _moguet_compile_commands_publish_result
    ERROR_VARIABLE _moguet_compile_commands_publish_error
)
if(NOT _moguet_compile_commands_publish_result EQUAL 0)
    file(REMOVE "${_moguet_compile_commands_temporary_link}")
    message(
        FATAL_ERROR
        "Unable to publish the developer compile database link: "
        "${_moguet_compile_commands_publish_error}"
    )
endif()
