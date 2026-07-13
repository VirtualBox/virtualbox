project(external-override C)

include_directories(${CMAKE_SOURCE_DIR}/include)

set(LIBSSH_OVERRIDE_TESTS
    torture_override
)

# chacha20_override
add_library(chacha20_override SHARED
            chacha20_override.c
            ${libssh_SOURCE_DIR}/src/external/chacha.c
           )
set(CHACHA20_OVERRIDE_LIBRARY
    ${libssh_BINARY_DIR}/lib/${CMAKE_SHARED_LIBRARY_PREFIX}chacha20_override${CMAKE_SHARED_LIBRARY_SUFFIX})

# poly1305_override
add_library(poly1305_override SHARED
            poly1305_override.c
            ${libssh_SOURCE_DIR}/src/external/poly1305.c
           )
set(POLY1305_OVERRIDE_LIBRARY
${libssh_BINARY_DIR}/lib/${CMAKE_SHARED_LIBRARY_PREFIX}poly1305_override${CMAKE_SHARED_LIBRARY_SUFFIX})

if (WITH_GCRYPT)
    set (override_src
        ${libssh_SOURCE_DIR}/src/getrandom_gcrypt.c
        ${libssh_SOURCE_DIR}/src/md_gcrypt.c
    )
    set(override_libs
        ${GCRYPT_LIBRARIES}
    )
elseif (WITH_MBEDTLS)
    set (override_src
        ${libssh_SOURCE_DIR}/src/getrandom_mbedcrypto.c
        ${libssh_SOURCE_DIR}/src/md_mbedcrypto.c
    )
    set(override_libs
        ${MBEDTLS_CRYPTO_LIBRARY}
    )
else ()
    set (override_src
        ${libssh_SOURCE_DIR}/src/getrandom_crypto.c
        ${libssh_SOURCE_DIR}/src/md_crypto.c
    )
    set(override_libs
        OpenSSL::Crypto
    )
endif (WITH_GCRYPT)

# ed25519_override
add_library(ed25519_override SHARED
    ed25519_override.c
    ${libssh_SOURCE_DIR}/src/external/fe25519.c
    ${libssh_SOURCE_DIR}/src/external/ge25519.c
    ${libssh_SOURCE_DIR}/src/external/sc25519.c
    ${libssh_SOURCE_DIR}/src/external/ed25519.c
    ${override_src}
    )
target_link_libraries(ed25519_override
                      PRIVATE ${override_libs})
set(ED25519_OVERRIDE_LIBRARY
${libssh_BINARY_DIR}/lib/${CMAKE_SHARED_LIBRARY_PREFIX}ed25519_override${CMAKE_SHARED_LIBRARY_SUFFIX})

# curve25519_override
add_library(curve25519_override SHARED
    curve25519_override.c
    ${libssh_SOURCE_DIR}/src/external/curve25519_ref.c
    ${libssh_SOURCE_DIR}/src/external/fe25519.c
    ${libssh_SOURCE_DIR}/src/external/ge25519.c
    ${libssh_SOURCE_DIR}/src/external/sc25519.c
    ${libssh_SOURCE_DIR}/src/external/ed25519.c
    ${override_src}
)
target_link_libraries(curve25519_override
                      PRIVATE ${override_libs})
set(CURVE25519_OVERRIDE_LIBRARY
${libssh_BINARY_DIR}/lib/${CMAKE_SHARED_LIBRARY_PREFIX}curve25519_override${CMAKE_SHARED_LIBRARY_SUFFIX})

set(OVERRIDE_LIBRARIES
    ${CHACHA20_OVERRIDE_LIBRARY}:${POLY1305_OVERRIDE_LIBRARY}:${ED25519_OVERRIDE_LIBRARY}:${CURVE25519_OVERRIDE_LIBRARY}
)

if (WITH_MBEDTLS)
    if (HAVE_MBEDTLS_CHACHA20_H AND HAVE_MBEDTLS_POLY1305_H)
        list(APPEND OVERRIDE_RESULTS "-DSHOULD_CALL_INTERNAL_CHACHAPOLY=0")
    else ()
        list(APPEND OVERRIDE_RESULTS "-DSHOULD_CALL_INTERNAL_CHACHAPOLY=1")
    endif ()
    list(APPEND OVERRIDE_RESULTS "-DSHOULD_CALL_INTERNAL_ED25519=1")
    list(APPEND OVERRIDE_RESULTS "-DSHOULD_CALL_INTERNAL_CURVE25519=1")
elseif (WITH_GCRYPT)
    if (HAVE_GCRYPT_CHACHA_POLY)
        list(APPEND OVERRIDE_RESULTS "-DSHOULD_CALL_INTERNAL_CHACHAPOLY=0")
    else ()
        list(APPEND OVERRIDE_RESULTS "-DSHOULD_CALL_INTERNAL_CHACHAPOLY=1")
    endif ()
    list(APPEND OVERRIDE_RESULTS "-DSHOULD_CALL_INTERNAL_ED25519=1")
    list(APPEND OVERRIDE_RESULTS "-DSHOULD_CALL_INTERNAL_CURVE25519=1")
else ()
    if (HAVE_OPENSSL_EVP_CHACHA20)
        list(APPEND OVERRIDE_RESULTS "-DSHOULD_CALL_INTERNAL_CHACHAPOLY=0")
    else ()
        list(APPEND OVERRIDE_RESULTS "-DSHOULD_CALL_INTERNAL_CHACHAPOLY=1")
    endif ()
    list(APPEND OVERRIDE_RESULTS "-DSHOULD_CALL_INTERNAL_CURVE25519=0")
    list(APPEND OVERRIDE_RESULTS "-DSHOULD_CALL_INTERNAL_ED25519=0")
endif ()

if (NOT OSX)
    # Remove any preload string from the environment variables list
    foreach(env_string ${TORTURE_ENVIRONMENT})
        if (${env_string} MATCHES "^LD_PRELOAD=*")
            list(REMOVE_ITEM TORTURE_ENVIRONMENT ${env_string})
            set(PRELOAD_STRING "${env_string}:")
        endif ()
    endforeach ()

    if ("${PRELOAD_STRING}" STREQUAL "")
        set(PRELOAD_STRING "LD_PRELOAD=")
    endif ()

    list(APPEND TORTURE_ENVIRONMENT
         "${PRELOAD_STRING}${OVERRIDE_LIBRARIES}")
endif()

foreach(_OVERRIDE_TEST ${LIBSSH_OVERRIDE_TESTS})
    add_cmocka_test(${_OVERRIDE_TEST}
                    SOURCES ${_OVERRIDE_TEST}.c
                    COMPILE_OPTIONS ${DEFAULT_C_COMPILE_FLAGS}
                        ${OVERRIDE_RESULTS}
                    LINK_LIBRARIES
                        ${TORTURE_SHARED_LIBRARY}
                        chacha20_override
                        poly1305_override
                        ed25519_override
                        curve25519_override
    )

    if (OSX)
        set_property(
            TEST
                ${_OVERRIDE_TEST}
            PROPERTY
            ENVIRONMENT DYLD_FORCE_FLAT_NAMESPACE=1;DYLD_INSERT_LIBRARIES=${OVERRIDE_LIBRARIES})

    else ()
        set_property(
            TEST
                ${_OVERRIDE_TEST}
            PROPERTY
                ENVIRONMENT ${TORTURE_ENVIRONMENT})

    endif()
endforeach()
