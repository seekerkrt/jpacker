cmake_minimum_required(VERSION 3.18)

foreach(
    _moguet_required_variable
    IN ITEMS
        MOGUET_NEGATIVE_COMPILE_SOURCE_DIR
        MOGUET_NEGATIVE_COMPILE_MAKE_EXECUTABLE
        MOGUET_NEGATIVE_COMPILE_CXX_FILE
        MOGUET_NEGATIVE_COMPILE_CPPFLAGS_FILE
        MOGUET_NEGATIVE_COMPILE_CXXFLAGS_FILE
)
    if(NOT DEFINED ${_moguet_required_variable})
        message(
            FATAL_ERROR
            "Missing negative compile input: ${_moguet_required_variable}"
        )
    endif()
endforeach()

foreach(
    _moguet_input_file
    IN ITEMS
        "${MOGUET_NEGATIVE_COMPILE_CXX_FILE}"
        "${MOGUET_NEGATIVE_COMPILE_CPPFLAGS_FILE}"
        "${MOGUET_NEGATIVE_COMPILE_CXXFLAGS_FILE}"
)
    if(NOT EXISTS "${_moguet_input_file}" OR IS_DIRECTORY "${_moguet_input_file}")
        message(
            FATAL_ERROR
            "Negative compile input file is unavailable: ${_moguet_input_file}"
        )
    endif()
endforeach()

file(READ "${MOGUET_NEGATIVE_COMPILE_CXX_FILE}" _moguet_cxx)
file(READ "${MOGUET_NEGATIVE_COMPILE_CPPFLAGS_FILE}" _moguet_cppflags)
file(READ "${MOGUET_NEGATIVE_COMPILE_CXXFLAGS_FILE}" _moguet_cxxflags)

execute_process(
    COMMAND
        "${MOGUET_NEGATIVE_COMPILE_MAKE_EXECUTABLE}"
        --no-print-directory
        -C "${MOGUET_NEGATIVE_COMPILE_SOURCE_DIR}"
        -f Makefile
        "CXX=${_moguet_cxx}"
        "CPPFLAGS=${_moguet_cppflags}"
        "CXXFLAGS=${_moguet_cxxflags}"
        check-reviewed-source-pinned-build-authority
    RESULT_VARIABLE _moguet_status
    OUTPUT_VARIABLE _moguet_stdout
    ERROR_VARIABLE _moguet_stderr
    ECHO_OUTPUT_VARIABLE
    ECHO_ERROR_VARIABLE
)

if(NOT "${_moguet_status}" STREQUAL "0")
    message(
        FATAL_ERROR
        "Reviewed source negative compile authority failed with status "
        "${_moguet_status}"
    )
endif()
