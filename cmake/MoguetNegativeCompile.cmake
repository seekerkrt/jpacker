cmake_minimum_required(VERSION 3.18)

foreach(
    _moguet_required_variable
    IN ITEMS
        MOGUET_NEGATIVE_COMPILE_SOURCE
        MOGUET_NEGATIVE_COMPILE_CXX_FILE
        MOGUET_NEGATIVE_COMPILE_CXX_ARG1_FILE
        MOGUET_NEGATIVE_COMPILE_LAUNCHER_FILE
        MOGUET_NEGATIVE_COMPILE_CPPFLAGS_FILE
        MOGUET_NEGATIVE_COMPILE_CXXFLAGS_FILE
        MOGUET_NEGATIVE_COMPILE_CONFIGURATION_FLAGS_FILE
        MOGUET_NEGATIVE_COMPILE_PROJECT_OPTIONS_FILE
)
    if(NOT DEFINED ${_moguet_required_variable})
        message(
            FATAL_ERROR
            "Missing negative compile input: ${_moguet_required_variable}"
        )
    endif()
endforeach()

if(
    NOT EXISTS "${MOGUET_NEGATIVE_COMPILE_SOURCE}"
    OR IS_DIRECTORY "${MOGUET_NEGATIVE_COMPILE_SOURCE}"
)
    message(
        FATAL_ERROR
        "Negative compile source is unavailable: "
        "${MOGUET_NEGATIVE_COMPILE_SOURCE}"
    )
endif()

foreach(
    _moguet_input_file
    IN ITEMS
        "${MOGUET_NEGATIVE_COMPILE_CXX_FILE}"
        "${MOGUET_NEGATIVE_COMPILE_CXX_ARG1_FILE}"
        "${MOGUET_NEGATIVE_COMPILE_LAUNCHER_FILE}"
        "${MOGUET_NEGATIVE_COMPILE_CPPFLAGS_FILE}"
        "${MOGUET_NEGATIVE_COMPILE_CXXFLAGS_FILE}"
        "${MOGUET_NEGATIVE_COMPILE_CONFIGURATION_FLAGS_FILE}"
        "${MOGUET_NEGATIVE_COMPILE_PROJECT_OPTIONS_FILE}"
)
    if(NOT EXISTS "${_moguet_input_file}" OR IS_DIRECTORY "${_moguet_input_file}")
        message(
            FATAL_ERROR
            "Negative compile input file is unavailable: ${_moguet_input_file}"
        )
    endif()
endforeach()

file(READ "${MOGUET_NEGATIVE_COMPILE_CXX_FILE}" _moguet_cxx)
file(READ "${MOGUET_NEGATIVE_COMPILE_CXX_ARG1_FILE}" _moguet_cxx_arg1)
file(READ "${MOGUET_NEGATIVE_COMPILE_CPPFLAGS_FILE}" _moguet_cppflags_raw)
file(READ "${MOGUET_NEGATIVE_COMPILE_CXXFLAGS_FILE}" _moguet_cxxflags_raw)
file(
    READ
    "${MOGUET_NEGATIVE_COMPILE_CONFIGURATION_FLAGS_FILE}"
    _moguet_configuration_flags_raw
)
file(STRINGS "${MOGUET_NEGATIVE_COMPILE_LAUNCHER_FILE}" _moguet_launcher)
file(
    STRINGS
    "${MOGUET_NEGATIVE_COMPILE_PROJECT_OPTIONS_FILE}"
    _moguet_project_options
)

if(_moguet_cxx STREQUAL "")
    message(FATAL_ERROR "Negative compile C++ compiler is empty")
endif()

separate_arguments(_moguet_cxx_arg1 UNIX_COMMAND "${_moguet_cxx_arg1}")
separate_arguments(_moguet_cppflags UNIX_COMMAND "${_moguet_cppflags_raw}")
separate_arguments(_moguet_cxxflags UNIX_COMMAND "${_moguet_cxxflags_raw}")
separate_arguments(
    _moguet_configuration_flags
    UNIX_COMMAND
    "${_moguet_configuration_flags_raw}"
)

function(_moguet_append_arguments output_variable input_variable)
    set(_moguet_arguments "${${output_variable}}")
    foreach(_moguet_argument IN LISTS ${input_variable})
        # CMake list expansion uses semicolons as separators. Re-escape a
        # literal semicolon after parsing so execute_process receives the
        # original argument as one argv element.
        string(REPLACE ";" "\\;" _moguet_argument "${_moguet_argument}")
        list(APPEND _moguet_arguments "${_moguet_argument}")
    endforeach()
    set("${output_variable}" "${_moguet_arguments}" PARENT_SCOPE)
endfunction()

set(_moguet_compile_command "")
_moguet_append_arguments(_moguet_compile_command _moguet_launcher)
list(APPEND _moguet_compile_command "${_moguet_cxx}")
_moguet_append_arguments(_moguet_compile_command _moguet_cxx_arg1)

set(_moguet_common_arguments "")
_moguet_append_arguments(_moguet_common_arguments _moguet_cxxflags)
_moguet_append_arguments(
    _moguet_common_arguments
    _moguet_configuration_flags
)
_moguet_append_arguments(_moguet_common_arguments _moguet_project_options)
_moguet_append_arguments(_moguet_common_arguments _moguet_cppflags)

execute_process(
    COMMAND
        ${_moguet_compile_command}
        ${_moguet_common_arguments}
        -fsyntax-only
        "${MOGUET_NEGATIVE_COMPILE_SOURCE}"
    RESULT_VARIABLE _moguet_baseline_status
    OUTPUT_VARIABLE _moguet_baseline_stdout
    ERROR_VARIABLE _moguet_baseline_stderr
)
if(NOT "${_moguet_baseline_status}" STREQUAL "0")
    message(
        FATAL_ERROR
        "Reviewed source authority baseline compile failed with status "
        "${_moguet_baseline_status}\n"
        "${_moguet_baseline_stdout}${_moguet_baseline_stderr}"
    )
endif()

set(
    _moguet_authority_cases
    LIFECYCLE_EXPECTED
    FATAL_PREFLIGHT
    LIFECYCLE_ALREADY
    RETAINED_DESCRIPTOR
    ACCEPTED_CHECKOUT
    ALREADY_CHECKOUT
    PINNED_ACCEPTED
    PINNED_ALREADY
    EDITOR_BOUNDARY
    EDITOR_OVERLAY
    PROVENANCE_REVIEWED_GENERATION
    PROVENANCE_REVIEWED_BINDING
    PROVENANCE_REVIEWED_BINDING_AUTHORITY
    INSTALLED_ARTIFACT_BINDING
    PROVENANCE_PERSISTENT_DECODER
    INVOCATION_SOURCE_BUILD_CONTEXT
    REVIEWED_RECIPE_SNAPSHOT_IDENTITY
    INVOCATION_MAKEPKG_ENVIRONMENT
    EVALUATED_DEVEL_SOURCE_PROJECTION
    FRESH_DEVEL_PACKAGE_ARTIFACT
    EVALUATED_DEVEL_SOURCE_BUILD_PROOF
)
foreach(_moguet_authority_case IN LISTS _moguet_authority_cases)
    execute_process(
        COMMAND
            ${_moguet_compile_command}
            ${_moguet_common_arguments}
            "-DMOGUET_FORGE_${_moguet_authority_case}"
            -fsyntax-only
            "${MOGUET_NEGATIVE_COMPILE_SOURCE}"
        RESULT_VARIABLE _moguet_authority_status
        OUTPUT_VARIABLE _moguet_authority_stdout
        ERROR_VARIABLE _moguet_authority_stderr
    )
    set(
        _moguet_authority_diagnostic
        "${_moguet_authority_stdout}${_moguet_authority_stderr}"
    )
    if("${_moguet_authority_status}" STREQUAL "0")
        message(
            FATAL_ERROR
            "Reviewed source authority forgery ${_moguet_authority_case} "
            "compiled successfully"
        )
    endif()
    if(
        NOT _moguet_authority_diagnostic
            MATCHES "is private within this context"
    )
        message(
            FATAL_ERROR
            "Reviewed source authority forgery ${_moguet_authority_case} "
            "failed for an unexpected reason\n"
            "${_moguet_authority_diagnostic}"
        )
    endif()
endforeach()

list(LENGTH _moguet_authority_cases _moguet_authority_case_count)
message(
    STATUS
    "Reviewed source authority negative compile: baseline=1, "
    "expected diagnostics=${_moguet_authority_case_count}"
)
