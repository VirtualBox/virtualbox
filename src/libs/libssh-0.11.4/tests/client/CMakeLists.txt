project(clienttests C)

find_package(socket_wrapper)

set(LIBSSH_CLIENT_TESTS
    torture_algorithms
    torture_client_callbacks
    torture_client_config
    torture_connect
    torture_hostkey
    torture_auth
    torture_auth_cert
    torture_rekey
    torture_forward
    torture_knownhosts
    torture_knownhosts_verify
    torture_proxycommand
    torture_session
    torture_request_env
    torture_request_pty_modes
    torture_client_global_requests)

find_program(SCP_EXECUTABLE NAMES scp)
if (SCP_EXECUTABLE)
    set(LIBSSH_CLIENT_TESTS
        ${LIBSSH_CLIENT_TESTS}
        torture_scp)
endif()

if (WITH_PKCS11_URI)
    set(LIBSSH_CLIENT_TESTS
        ${LIBSSH_CLIENT_TESTS}
        torture_auth_pkcs11)
endif()

if (HAVE_PTHREAD)
    set(LIBSSH_CLIENT_TESTS
        ${LIBSSH_CLIENT_TESTS}
        torture_proxyjump)
endif()

if (WITH_GSSAPI AND GSSAPI_FOUND AND GSSAPI_TESTING)
    set(LIBSSH_CLIENT_TESTS
        ${LIBSSH_CLIENT_TESTS}
        torture_gssapi_auth)
endif()

if (DEFAULT_C_NO_DEPRECATION_FLAGS)
    set_source_files_properties(torture_knownhosts.c
                                PROPERTIES
                                    COMPILE_FLAGS ${DEFAULT_C_NO_DEPRECATION_FLAGS})
endif()

if (WITH_SFTP)
    if (WITH_BENCHMARKS)
        set(SFTP_BENCHMARK_TESTS
            torture_sftp_benchmark)
    endif()
    set(LIBSSH_CLIENT_TESTS
        ${LIBSSH_CLIENT_TESTS}
        torture_sftp_init
        torture_sftp_ext
        torture_sftp_canonicalize_path
        torture_sftp_dir
        torture_sftp_read
        torture_sftp_fsync
        torture_sftp_hardlink
        torture_sftp_limits
        torture_sftp_rename
        torture_sftp_expand_path
        torture_sftp_aio
        torture_sftp_home_directory
        torture_sftp_setstat
        torture_sftp_packet_read
        ${SFTP_BENCHMARK_TESTS})
endif (WITH_SFTP)

set(TORTURE_CLIENT_ENVIRONMENT ${TORTURE_ENVIRONMENT})
list(APPEND TORTURE_CLIENT_ENVIRONMENT NSS_WRAPPER_HOSTS=${CMAKE_BINARY_DIR}/tests/etc/hosts)

foreach(_CLI_TEST ${LIBSSH_CLIENT_TESTS})
    add_cmocka_test(${_CLI_TEST}
                    SOURCES ${_CLI_TEST}.c
                    COMPILE_OPTIONS ${DEFAULT_C_COMPILE_FLAGS}
                    LINK_LIBRARIES ${TORTURE_LIBRARY} util
    )

    if (OSX)
        set_property(
            TEST
                ${_CLI_TEST}
            PROPERTY
            ENVIRONMENT DYLD_FORCE_FLAT_NAMESPACE=1;DYLD_INSERT_LIBRARIES=${SOCKET_WRAPPER_LIBRARY})
    else ()
        set_property(
            TEST
                ${_CLI_TEST}
            PROPERTY
                ENVIRONMENT ${TORTURE_CLIENT_ENVIRONMENT})
    endif()
endforeach()
