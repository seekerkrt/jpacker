cmake_minimum_required(VERSION 3.18)

foreach(
    _moguet_required_variable
    IN ITEMS
        MOGUET_COMPLETION_PYTHON
        MOGUET_COMPLETION_GENERATOR
        MOGUET_COMPLETION_EXPORTER
        MOGUET_COMPLETION_OUTPUT_DIRECTORY
)
    if(NOT DEFINED ${_moguet_required_variable})
        message(FATAL_ERROR "${_moguet_required_variable} is required")
    endif()
endforeach()

if(NOT IS_DIRECTORY "${MOGUET_COMPLETION_OUTPUT_DIRECTORY}")
    message(
        FATAL_ERROR
        "Completion output directory is unavailable: "
        "${MOGUET_COMPLETION_OUTPUT_DIRECTORY}"
    )
endif()

set(_moguet_completion_shells bash zsh fish)
set(_moguet_completion_filenames moguet.bash _moguet moguet.fish)
set(_moguet_completion_temporary_files "")
set(_moguet_completion_output_files "")

foreach(
    _moguet_completion_shell
    _moguet_completion_filename
    IN ZIP_LISTS
        _moguet_completion_shells
        _moguet_completion_filenames
)
    string(
        RANDOM
        LENGTH 16
        ALPHABET 0123456789abcdef
        _moguet_completion_suffix
    )
    set(
        _moguet_completion_output_file
        "${MOGUET_COMPLETION_OUTPUT_DIRECTORY}/${_moguet_completion_filename}"
    )
    set(
        _moguet_completion_temporary_file
        "${_moguet_completion_output_file}.${_moguet_completion_suffix}.tmp"
    )
    execute_process(
        COMMAND
            "${CMAKE_COMMAND}" -E env
            "MOGUET_CLI_AUTHORITY_EXPORTER=${MOGUET_COMPLETION_EXPORTER}"
            PYTHONDONTWRITEBYTECODE=1
            "${MOGUET_COMPLETION_PYTHON}"
            "${MOGUET_COMPLETION_GENERATOR}"
            --render "${_moguet_completion_shell}"
        RESULT_VARIABLE _moguet_completion_render_result
        ERROR_VARIABLE _moguet_completion_render_error
        OUTPUT_FILE "${_moguet_completion_temporary_file}"
    )
    if(NOT _moguet_completion_render_result EQUAL 0)
        file(
            REMOVE
            "${_moguet_completion_temporary_file}"
            ${_moguet_completion_temporary_files}
        )
        message(
            FATAL_ERROR
            "Unable to render ${_moguet_completion_shell} completion: "
            "${_moguet_completion_render_error}"
        )
    endif()
    list(
        APPEND
        _moguet_completion_temporary_files
        "${_moguet_completion_temporary_file}"
    )
    list(
        APPEND
        _moguet_completion_output_files
        "${_moguet_completion_output_file}"
    )
endforeach()

foreach(
    _moguet_completion_temporary_file
    _moguet_completion_output_file
    IN ZIP_LISTS
        _moguet_completion_temporary_files
        _moguet_completion_output_files
)
    execute_process(
        COMMAND
            "${CMAKE_COMMAND}" -E rename
            "${_moguet_completion_temporary_file}"
            "${_moguet_completion_output_file}"
        RESULT_VARIABLE _moguet_completion_publish_result
        ERROR_VARIABLE _moguet_completion_publish_error
    )
    if(NOT _moguet_completion_publish_result EQUAL 0)
        file(REMOVE ${_moguet_completion_temporary_files})
        message(
            FATAL_ERROR
            "Unable to publish ${_moguet_completion_output_file}: "
            "${_moguet_completion_publish_error}"
        )
    endif()
endforeach()
