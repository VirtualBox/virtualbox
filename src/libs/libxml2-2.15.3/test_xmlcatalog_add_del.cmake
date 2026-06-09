# test_xmlcatalog_add_del.cmake
cmake_minimum_required(VERSION 3.16)

set(WORK "${CMAKE_CURRENT_BINARY_DIR}/catalog_add_del_work")
file(REMOVE_RECURSE "${WORK}")
file(MAKE_DIRECTORY "${WORK}")

set(CAT "${WORK}/mycatalog")

# small helper macro (shorter execute_process)
macro(run)
    execute_process(COMMAND ${ARGV} RESULT_VARIABLE rv)
    if(rv)
        message(FATAL_ERROR "Command failed: ${ARGV}")
    endif()
endmacro()

# 1. Create
run("${XMLCAT}" --create --noout "${CAT}")

# 2. Adds
run("${XMLCAT}" --noout --add public Pubid  sysid  "${CAT}")
run("${XMLCAT}" --noout --add public Pubid2 sysid2 "${CAT}")
run("${XMLCAT}" --noout --add public Pubid3 sysid3 "${CAT}")

# Check full contents
run(${CMAKE_COMMAND} -E compare_files "${CAT}" "${EXPECTED_FULL}")

# 3. Deletes
run("${XMLCAT}" --noout --del sysid  "${CAT}")
run("${XMLCAT}" --noout --del sysid3 "${CAT}")
run("${XMLCAT}" --noout --del sysid2 "${CAT}")

# Check empty contents
run(${CMAKE_COMMAND} -E compare_files "${CAT}" "${EXPECTED_EMPTY}")
