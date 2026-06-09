# Ensure output directory exists
get_filename_component(_outdir "${STDOUT}" DIRECTORY)
file(MAKE_DIRECTORY "${_outdir}")

# Run the program
execute_process(
    COMMAND "${PROG}" --shell "${XML}"
    INPUT_FILE "${STDIN}"
    OUTPUT_FILE "${STDOUT}"
    ERROR_FILE  "${STDERR}"
    RESULT_VARIABLE rc
    WORKING_DIRECTORY "${WORKDIR}"
 )

if(NOT rc EQUAL 0)
    message(STATUS "Program exited with ${rc}")
endif()

# Diff stdout
if(NOT EXISTS "${EXPECTED_STDOUT}")
    message(FATAL_ERROR "Missing expected stdout file: ${EXPECTED_STDOUT}")
endif()

execute_process(
    COMMAND ${CMAKE_COMMAND} -E compare_files
        "${STDOUT}" "${EXPECTED_STDOUT}"
    RESULT_VARIABLE cmp_stdout
)

if(NOT cmp_stdout EQUAL 0)
    message(FATAL_ERROR "stdout does not match expected")
endif()

# Diff stderr if it exists
if(EXISTS "${EXPECTED_STDERR}")
    execute_process(
        COMMAND ${CMAKE_COMMAND} -E compare_files
            "${STDERR}" "${EXPECTED_STDERR}"
        RESULT_VARIABLE cmp_stderr
    )
    if(NOT cmp_stderr EQUAL 0)
        message(FATAL_ERROR "stderr does not match expected")
    endif()
endif()
