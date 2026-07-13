project(fuzzing CXX)

macro(fuzzer name)
    add_executable(${name} ${name}.c)
    target_link_libraries(${name} PRIVATE ${TORTURE_LINK_LIBRARIES})
    if (CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
        set_target_properties(${name}
                              PROPERTIES
                                  COMPILE_FLAGS "-fsanitize=fuzzer"
                                  LINK_FLAGS "-fsanitize=fuzzer")
        # Run the fuzzer to make sure it works
        add_test(${name} ${CMAKE_CURRENT_BINARY_DIR}/${name} -runs=1)
    else()
        target_sources(${name} PRIVATE fuzzer.c)
        # Run the fuzzer to make sure it works
        if (EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/${name}_corpus")
            file(GLOB files "${CMAKE_CURRENT_SOURCE_DIR}/${name}_corpus/*")
            set(i 0)
            foreach(file ${files})
                add_test(${name}_${i}
                         ${CMAKE_CURRENT_BINARY_DIR}/${name} ${file})
                math(EXPR i "${i} + 1")
            endforeach()
        endif()
    endif()
endmacro()

fuzzer(ssh_client_fuzzer)
fuzzer(ssh_client_config_fuzzer)
fuzzer(ssh_known_hosts_fuzzer)
fuzzer(ssh_privkey_fuzzer)
fuzzer(ssh_pubkey_fuzzer)
if (WITH_SERVER)
    fuzzer(ssh_server_fuzzer)
    fuzzer(ssh_bind_config_fuzzer)
endif (WITH_SERVER)
