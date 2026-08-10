project(unittests C)

set(LIBSSH_UNIT_TESTS
    torture_bignum
    torture_buffer
    torture_bytearray
    torture_callbacks
    torture_crypto
    torture_init
    torture_list
    torture_misc
    torture_config
    torture_options
    torture_isipaddr
    torture_knownhosts_parsing
    torture_hashes
    torture_packet_filter
    torture_temp_dir
    torture_temp_file
    torture_push_pop_dir
    torture_session_keys
    torture_string
    torture_tokens
)

set(LIBSSH_THREAD_UNIT_TESTS
    torture_rand
    torture_threads_init
    torture_threads_buffer
    torture_threads_crypto
)

set(TORTURE_UNIT_ENVIRONMENT
    "LSAN_OPTIONS=suppressions=${libssh-tests_SOURCE_DIR}/suppressions/lsan.supp;")
if (OPENSSL_FOUND)
    list(APPEND TORTURE_UNIT_ENVIRONMENT OPENSSL_ENABLE_SHA1_SIGNATURES=1)
endif (OPENSSL_FOUND)

if (UNIX AND NOT WIN32)
    set(LIBSSH_UNIT_TESTS
        ${LIBSSH_UNIT_TESTS}
        # this uses a socketpair
        torture_packet
        # requires ssh-keygen
        torture_keyfiles
        torture_pki
        torture_pki_rsa
        torture_pki_dsa
        torture_pki_ed25519
        torture_pki_sk_ed25519
        torture_pki_sshsig
        # requires /dev/null
        torture_channel
    )
    if (HAVE_IFADDRS_H)
        set(LIBSSH_UNIT_TESTS
            ${LIBSSH_UNIT_TESTS}
            # requires some non-standard API from netdb.h, in.h
            # and arpa/inet.h for handling IP addresses
            torture_config_match_localnetwork
        )
    endif (HAVE_IFADDRS_H)
    if (WITH_SERVER)
        set(LIBSSH_UNIT_TESTS
            ${LIBSSH_UNIT_TESTS}
            torture_bind_config)

        if (WITH_GEX)
            set(LIBSSH_UNIT_TESTS
                ${LIBSSH_UNIT_TESTS}
                torture_moduli)
        endif()
    endif()

    if (WITH_PKCS11_URI)
        set(LIBSSH_UNIT_TESTS
            ${LIBSSH_UNIT_TESTS}
            torture_pki_rsa_uri
            torture_pki_ecdsa_uri
        )
        if (WITH_PKCS11_PROVIDER)
            set(LIBSSH_UNIT_TESTS
                ${LIBSSH_UNIT_TESTS}
                torture_pki_ed25519_uri
            )
            list(APPEND TORTURE_UNIT_ENVIRONMENT
                 PKCS11_PROVIDER_DEBUG=file:/tmp/p11prov-debug.log)
        endif()
    endif()

    if (WITH_FIDO2)
        set(LIBSSH_UNIT_TESTS
            ${LIBSSH_UNIT_TESTS}
            torture_pki_sk
        )
    endif (WITH_FIDO2)

    if (HAVE_ECC)
        set(LIBSSH_UNIT_TESTS
            ${LIBSSH_UNIT_TESTS}
            torture_pki_ecdsa
            torture_pki_sk_ecdsa
        )
    endif()

    set(LIBSSH_THREAD_UNIT_TESTS
        ${LIBSSH_THREAD_UNIT_TESTS}
        # requires pthread
        torture_threads_pki_rsa
    )
    if (WITH_SERVER)
        set(LIBSSH_THREAD_UNIT_TESTS
            ${LIBSSH_THREAD_UNIT_TESTS}
            torture_unit_server
            torture_server_x11
            torture_forwarded_tcpip_callback
            torture_server_direct_tcpip
        )
    endif (WITH_SERVER)
endif (UNIX AND NOT WIN32)

if (HAVE_LIBFIDO2)
    set(LIBSSH_UNIT_TESTS
        ${LIBSSH_UNIT_TESTS}
        torture_sk_usbhid
    )
endif (HAVE_LIBFIDO2)

if (WITH_SFTP)
    set(LIBSSH_UNIT_TESTS
        ${LIBSSH_UNIT_TESTS}
        torture_unit_sftp
    )
endif (WITH_SFTP)

foreach(_UNIT_TEST ${LIBSSH_UNIT_TESTS})
    add_cmocka_test(${_UNIT_TEST}
                    SOURCES ${_UNIT_TEST}.c
                    COMPILE_OPTIONS ${DEFAULT_C_COMPILE_FLAGS}
                    LINK_LIBRARIES ${TEST_TARGET_LIBRARIES}
    )

    set_property(TEST ${_UNIT_TEST}
                 PROPERTY
                     ENVIRONMENT ${TORTURE_UNIT_ENVIRONMENT})
endforeach()

if (CMAKE_USE_PTHREADS_INIT)
    foreach(_UNIT_TEST ${LIBSSH_THREAD_UNIT_TESTS})
        add_cmocka_test(${_UNIT_TEST}
                        SOURCES ${_UNIT_TEST}.c
                        COMPILE_OPTIONS ${DEFAULT_C_COMPILE_FLAGS}
                        LINK_LIBRARIES ${TEST_TARGET_LIBRARIES} Threads::Threads
        )

    set_property(TEST ${_UNIT_TEST}
                 PROPERTY
                    ENVIRONMENT ${TORTURE_UNIT_ENVIRONMENT})
    endforeach()
endif ()

