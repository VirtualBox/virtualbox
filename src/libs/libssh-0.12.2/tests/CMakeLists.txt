project(libssh-tests C)

if (BSD OR SOLARIS OR OSX)
    find_package(Argp)
endif (BSD OR SOLARIS OR OSX)

set(TORTURE_LIBRARY torture)

include_directories(${CMOCKA_INCLUDE_DIR}
                    ${libssh_BINARY_DIR}/include
                    ${libssh_BINARY_DIR}
                    ${libssh_SOURCE_DIR}/src
                    ${CMAKE_SOURCE_DIR}/include
                    ${CMAKE_CURRENT_SOURCE_DIR}
                    ${CMAKE_BINARY_DIR}/tests)

set(TORTURE_LINK_LIBRARIES
    ${CMOCKA_LIBRARY}
    ssh::static)

if (NOT WIN32)
    set(TORTURE_LINK_LIBRARIES
        ${TORTURE_LINK_LIBRARIES}
        pthread)
endif(NOT WIN32)
if (WITH_GSSAPI AND GSSAPI_FOUND)
    find_package(OpenSSL 1.1.1 REQUIRED)
    set(TORTURE_LINK_LIBRARIES
        ${TORTURE_LINK_LIBRARIES}
        OpenSSL::Crypto)
endif (WITH_GSSAPI AND GSSAPI_FOUND)

# Check for sk-dummy library if FIDO2 support is enabled
if (WITH_FIDO2)
    find_file(SK_DUMMY_LIBRARY
        NAMES sk-dummy.so
        PATHS /usr/lib64/sshtest /usr/lib/sshtest
        NO_DEFAULT_PATH
    )
endif (WITH_FIDO2)

# create test library
add_library(${TORTURE_LIBRARY}
            STATIC
                cmdline.c
                torture.c
                torture_key.c
                torture_pki.c
                torture_sk.c
                torture_cmocka.c)
target_link_libraries(${TORTURE_LIBRARY} PRIVATE ${TORTURE_LINK_LIBRARIES})
target_compile_options(${TORTURE_LIBRARY} PRIVATE
                       -DSSH_PING_EXECUTABLE="${CMAKE_CURRENT_BINARY_DIR}/ssh_ping"
)

# Check for sk-dummy and add HAVE_SK_DUMMY definition if available
if (SK_DUMMY_LIBRARY)
    add_library(sk-dummy SHARED IMPORTED)
    set_target_properties(sk-dummy PROPERTIES IMPORTED_LOCATION "${SK_DUMMY_LIBRARY}")
    target_link_libraries(${TORTURE_LIBRARY} PRIVATE sk-dummy)
    set(HAVE_SK_DUMMY 1)
    set(SK_DUMMY_LIBRARY_PATH ${SK_DUMMY_LIBRARY})
endif()

if (WITH_COVERAGE)
    append_coverage_compiler_flags_to_target(${TORTURE_LIBRARY})
endif (WITH_COVERAGE)

# The shared version of the library is only useful when client testing is
# enabled
if (CLIENT_TESTING)
    # create shared test library
    set(TORTURE_SHARED_LIBRARY torture_shared)

    # Create a list of symbols that should be wrapped for override test
    set(WRAP_SYMBOLS "")
    list(APPEND WRAP_SYMBOLS
         "-Wl,--wrap=chacha_keysetup"
         "-Wl,--wrap=chacha_ivsetup"
         "-Wl,--wrap=chacha_encrypt_bytes")
    list(APPEND WRAP_SYMBOLS "-Wl,--wrap=poly1305_auth")
    list(APPEND WRAP_SYMBOLS
         "-Wl,--wrap=crypto_sign_ed25519_keypair"
         "-Wl,--wrap=crypto_sign_ed25519"
         "-Wl,--wrap=crypto_sign_ed25519_open")
    list(APPEND WRAP_SYMBOLS
         "-Wl,--wrap=crypto_scalarmult_base"
         "-Wl,--wrap=crypto_scalarmult")
    list(APPEND WRAP_SYMBOLS
         "-Wl,--wrap=sntrup761_keypair"
         "-Wl,--wrap=sntrup761_enc"
         "-Wl,--wrap=sntrup761_dec")
    list(APPEND WRAP_SYMBOLS
         "-Wl,--wrap=libcrux_ml_kem_mlkem768_portable_generate_key_pair"
         "-Wl,--wrap=libcrux_ml_kem_mlkem768_portable_validate_public_key"
         "-Wl,--wrap=libcrux_ml_kem_mlkem768_portable_encapsulate"
         "-Wl,--wrap=libcrux_ml_kem_mlkem768_portable_decapsulate")

    add_library(${TORTURE_SHARED_LIBRARY}
                SHARED
                    cmdline.c
                    torture.c
                    torture_key.c
                    torture_pki.c
                    torture_sk.c
                    torture_cmocka.c
               )
    target_link_libraries(${TORTURE_SHARED_LIBRARY} PUBLIC
                          ${CMOCKA_LIBRARY}
                          ssh::static
                          ${WRAP_SYMBOLS}
                         )

    # Link sk-dummy to torture_shared library if available
    if (SK_DUMMY_LIBRARY)
        target_link_libraries(${TORTURE_SHARED_LIBRARY} PRIVATE sk-dummy)
    endif (SK_DUMMY_LIBRARY)

    target_compile_options(${TORTURE_SHARED_LIBRARY} PRIVATE
                           -DSSH_PING_EXECUTABLE="${CMAKE_CURRENT_BINARY_DIR}/ssh_ping"
                           -DTORTURE_SHARED
    )
    if (WITH_COVERAGE)
        append_coverage_compiler_flags_to_target(${TORTURE_SHARED_LIBRARY})
    endif (WITH_COVERAGE)
endif ()

if (ARGP_LIBRARIES)
    target_link_libraries(${TORTURE_LIBRARY}
        PUBLIC ${ARGP_LIBRARIES}
    )
endif()

set(TEST_TARGET_LIBRARIES
    ${TORTURE_LIBRARY}
    ${TORTURE_LINK_LIBRARIES}
)

add_subdirectory(unittests)

# OpenSSH Capabilities are required for all unit tests
find_program(SSH_EXECUTABLE NAMES ssh)
find_program(SSH_KEYGEN_EXECUTABLE NAMES ssh-keygen)
if (SSH_EXECUTABLE)
    file(SIZE ${SSH_EXECUTABLE} SSH_EXECUTABLE_SIZE)
    execute_process(COMMAND ${SSH_EXECUTABLE} -V ERROR_VARIABLE OPENSSH_VERSION_STR)
    string(REGEX REPLACE "^.*OpenSSH_([0-9]+).[0-9].*$" "\\1" OPENSSH_VERSION_MAJOR "${OPENSSH_VERSION_STR}")
    string(REGEX REPLACE "^.*OpenSSH_[0-9]+.([0-9]).*$" "\\1" OPENSSH_VERSION_MINOR "${OPENSSH_VERSION_STR}")
    set(OPENSSH_VERSION "${OPENSSH_VERSION_MAJOR}.${OPENSSH_VERSION_MINOR}")
    add_definitions(-DOPENSSH_VERSION_MAJOR=${OPENSSH_VERSION_MAJOR} -DOPENSSH_VERSION_MINOR=${OPENSSH_VERSION_MINOR})
    if("${OPENSSH_VERSION}" VERSION_GREATER_EQUAL "8.1" AND SSH_KEYGEN_EXECUTABLE)
        set(OPENSSH_SUPPORTS_SSHSIG 1)
        message(STATUS "OpenSSH ${OPENSSH_VERSION} supports SSH signatures")
    else()
        set(OPENSSH_SUPPORTS_SSHSIG 0)
        message(STATUS "OpenSSH ${OPENSSH_VERSION} does not support SSH signatures (requires 8.1+)")
    endif()
    if("${OPENSSH_VERSION}" VERSION_LESS "6.3")
        # ssh - Q was introduced in 6.3
        message("Version less than 6.3, hardcoding cipher list")
        set(OPENSSH_CIPHERS "aes128-ctr\naes192-ctr\naes256-ctr\narcfour256\narcfour128\naes128-gcm@openssh.com\naes256-gcm@openssh.com\naes128-cbc\n3des-cbc\nblowfish-cbc\ncast128-cbc\naes192-cbc\naes256-cbc\narcfour\nrijndael-cbc@lysator.liu.se\n")
        set(OPENSSH_MACS "hmac-md5-etm@openssh.com\nhmac-sha1-etm@openssh.com\numac-64-etm@openssh.com\numac-128-etm@openssh.com\nhmac-sha2-256-etm@openssh.com\nhmac-sha2-512-etm@openssh.com\nhmac-ripemd160-etm@openssh.com\nhmac-sha1-96-etm@openssh.com\nhmac-md5-96-etm@openssh.com\nhmac-md5\nhmac-sha1\numac-64@openssh.com\numac-128@openssh.com\nhmac-sha2-256\nhmac-sha2-512\nhmac-ripemd160\nhmac-ripemd160@openssh.com\nhmac-sha1-96\nhmac-md5-96\n")
        set(OPENSSH_KEX "ecdh-sha2-nistp256\necdh-sha2-nistp384\necdh-sha2-nistp521\ndiffie-hellman-group-exchange-sha256\ndiffie-hellman-group-exchange-sha1\ndiffie-hellman-group14-sha1\ndiffie-hellman-group1-sha1\n")
        set(OPENSSH_KEYS "ssh-rsa\necdsa-sha2-nistp256\n")
    else()
        execute_process(COMMAND ${SSH_EXECUTABLE} -Q cipher OUTPUT_VARIABLE OPENSSH_CIPHERS)
        execute_process(COMMAND ${SSH_EXECUTABLE} -Q mac OUTPUT_VARIABLE OPENSSH_MACS)
        execute_process(COMMAND ${SSH_EXECUTABLE} -Q kex OUTPUT_VARIABLE OPENSSH_KEX)
        execute_process(COMMAND ${SSH_EXECUTABLE} -Q key OUTPUT_VARIABLE OPENSSH_KEYS)
        execute_process(COMMAND ${SSH_EXECUTABLE} -Q sig OUTPUT_VARIABLE OPENSSH_SIGS ERROR_QUIET)

        # We need both of them, but lets get rid of duplicate items presented in both lists
        # to avoid processing too long arguments in pkd
        set(OPENSSH_KEYS "${OPENSSH_KEYS}${OPENSSH_SIGS}")
        string(REPLACE "\n" ";" OPENSSH_KEYS "${OPENSSH_KEYS}")
        list(REMOVE_DUPLICATES OPENSSH_KEYS)
        string(REPLACE ";" "\n" OPENSSH_KEYS "${OPENSSH_KEYS}")
    endif()

    set(SSH_ALGORITHMS
        3des-cbc aes128-cbc aes192-cbc aes256-cbc rijndael-cbc@lysator.liu.se aes128-ctr aes192-ctr
        aes256-ctr aes128-gcm@openssh.com aes256-gcm@openssh.com chacha20-poly1305@openssh.com
        hmac-sha1 hmac-sha1-96 hmac-sha2-256 hmac-sha2-512 hmac-md5 hmac-md5-96 umac-64@openssh.com
        umac-128@openssh.com hmac-sha1-etm@openssh.com hmac-sha1-96-etm@openssh.com
        hmac-sha2-256-etm@openssh.com hmac-sha2-512-etm@openssh.com hmac-md5-etm@openssh.com
        hmac-md5-96-etm@openssh.com umac-64-etm@openssh.com umac-128-etm@openssh.com
        diffie-hellman-group1-sha1 diffie-hellman-group14-sha1 diffie-hellman-group14-sha256
        diffie-hellman-group16-sha512 diffie-hellman-group18-sha512 diffie-hellman-group-exchange-sha1
        diffie-hellman-group-exchange-sha256 ecdh-sha2-nistp256 ecdh-sha2-nistp384 ecdh-sha2-nistp521
        sntrup761x25519-sha512@openssh.com sntrup761x25519-sha512
        mlkem768x25519-sha256 mlkem768nistp256-sha256 mlkem1024nistp384-sha384
        curve25519-sha256 curve25519-sha256@libssh.org
        ssh-ed25519 ssh-ed25519-cert-v01@openssh.com ssh-rsa
        ecdsa-sha2-nistp256 ecdsa-sha2-nistp384 ecdsa-sha2-nistp521
        ssh-rsa-cert-v01@openssh.com
        ecdsa-sha2-nistp256-cert-v01@openssh.com ecdsa-sha2-nistp384-cert-v01@openssh.com
        ecdsa-sha2-nistp521-cert-v01@openssh.com
        sk-ssh-ed25519@openssh.com sk-ecdsa-sha2-nistp256@openssh.com
        sk-ssh-ed25519-cert-v01@openssh.com sk-ecdsa-sha2-nistp256-cert-v01@openssh.com
        )
    foreach(ALGORITHM ${SSH_ALGORITHMS})
        string(TOUPPER ${ALGORITHM} VARNAME)
        string(REGEX REPLACE "[-@.]" "_" VARNAME "OPENSSH_${VARNAME}")

        # Match the current algorithm into the complete list of OpenSSH supported algorithms.
        # If matching, create an OPENSSH_CIPHER_NAME variable.
        string(REGEX MATCH ".*${ALGORITHM}\n" "${VARNAME}" "${OPENSSH_CIPHERS}${OPENSSH_MACS}${OPENSSH_KEX}${OPENSSH_KEYS}")
    endforeach(ALGORITHM)

    string(STRIP "${OPENSSH_CIPHERS}" OPENSSH_CIPHERS)
    string(STRIP "${OPENSSH_MACS}" OPENSSH_MACS)
    string(STRIP "${OPENSSH_KEX}" OPENSSH_KEX)
    string(STRIP "${OPENSSH_KEYS}" OPENSSH_KEYS)
    string(REPLACE "\n" "," OPENSSH_CIPHERS "${OPENSSH_CIPHERS}")
    string(REPLACE "\n" "," OPENSSH_MACS "${OPENSSH_MACS}")
    string(REPLACE "\n" "," OPENSSH_KEX "${OPENSSH_KEX}")
    string(REPLACE "\n" "," OPENSSH_KEYS "${OPENSSH_KEYS}")

endif()

find_program(DROPBEAR_EXECUTABLE NAMES dbclient)
if (DROPBEAR_EXECUTABLE)
    execute_process(COMMAND ${DROPBEAR_EXECUTABLE} -V ERROR_VARIABLE DROPBEAR_VERSION_STR)
    string(REGEX REPLACE "^.*Dropbear v([0-9]+)\\.([0-9]+).*$" "\\1.\\2" DROPBEAR_VERSION "${DROPBEAR_VERSION_STR}")
    set(DROPBEAR_VERSION "${DROPBEAR_VERSION}")

    # HMAC-SHA1 support was removed in version 2025.87
    if("${DROPBEAR_VERSION}" VERSION_LESS "2025.87")
        message("Dropbear Version less than 2025.87, enabling dropbear HMAC-SHA1 tests")
        add_definitions(-DDROPBEAR_SUPPORTS_HMAC_SHA1)
    endif()
else()
    message(STATUS "Could NOT find Dropbear (missing: dbclient executable)")
    set(DROPBEAR_EXECUTABLE "/bin/false")
endif()

find_program(PUTTY_EXECUTABLE
             NAMES
                plink
                plink.exe
                putty   # Fallback for systems where plink isn't separate
             DOC "Path to PuTTY plink executable for automated tests")

if (PUTTY_EXECUTABLE)
    message(STATUS "Found PuTTY client: ${PUTTY_EXECUTABLE}")
else()
    set(PUTTY_EXECUTABLE "/bin/putty-not-found")
endif()

find_program(PUTTYGEN_EXECUTABLE
             NAMES
                puttygen
                puttygen.exe
             DOC "Path to PuTTYgen executable for key conversion")

if (PUTTYGEN_EXECUTABLE)
    message(STATUS "Found PuTTY keygen: ${PUTTYGEN_EXECUTABLE}")
else()
    set(PUTTYGEN_EXECUTABLE "/bin/puttygen-not-found")
endif()

find_program(SSHD_EXECUTABLE
             NAME
                sshd
             PATHS
                /sbin
                /usr/sbin
                /usr/local/sbin)

if (WITH_PKCS11_URI)
    find_package(softhsm)
    if (NOT SOFTHSM_FOUND)
        message(SEND_ERROR "Could not find softhsm module!")
    endif (NOT SOFTHSM_FOUND)
    find_library(PKCS11SPY
        NAMES
            pkcs11-spy.so
    )

    #Copy the script to setup PKCS11 tokens
    file(COPY pkcs11/setup-softhsm-tokens.sh DESTINATION ${CMAKE_CURRENT_BINARY_DIR}/pkcs11 FILE_PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE)
endif (WITH_PKCS11_URI)

if (CLIENT_TESTING OR SERVER_TESTING)
    find_package(socket_wrapper 1.1.5 REQUIRED)
    find_package(nss_wrapper 1.1.2 REQUIRED)
    find_package(uid_wrapper 1.2.0 REQUIRED)
    find_package(pam_wrapper 1.0.1 REQUIRED)
    find_package(priv_wrapper 1.0.0)

    if (NOT SSHD_EXECUTABLE)
        message(SEND_ERROR "Could not find sshd which is required for client testing")
    endif()
    find_program(NCAT_EXECUTABLE
                 NAME
                    ncat
                 PATHS
                    /bin
                    /usr/bin
                    /usr/local/bin)

    set(LOCAL_USER "nobody")
    set(LOCAL_UID "65533")
    find_program(ID_EXECUTABLE NAMES id)
    find_program(WHO_EXECUTABLE NAMES whoami)
    if (ID_EXECUTABLE AND WHO_EXECUTABLE)
        execute_process(COMMAND ${WHO_EXECUTABLE} OUTPUT_VARIABLE LOCAL_USER OUTPUT_STRIP_TRAILING_WHITESPACE)
        execute_process(COMMAND ${ID_EXECUTABLE} -u OUTPUT_VARIABLE LOCAL_UID OUTPUT_STRIP_TRAILING_WHITESPACE)
    endif()

    find_program(TIMEOUT_EXECUTABLE
                 NAME
                    timeout
                 PATHS
                    /bin
                    /usr/bin
                    /usr/local/bin)
    if (TIMEOUT_EXECUTABLE)
        set(WITH_TIMEOUT "1")
    endif()

    # For chroot() use priv_wrapper package if found, or internal chroot_wrapper
    if (priv_wrapper_FOUND)
        set(CHROOT_WRAPPER "${PRIV_WRAPPER_LIBRARY}")
    else()
        add_library(chroot_wrapper SHARED chroot_wrapper.c)
        set(CHROOT_WRAPPER_LIBRARY ${libssh_BINARY_DIR}/lib/${CMAKE_SHARED_LIBRARY_PREFIX}chroot_wrapper${CMAKE_SHARED_LIBRARY_SUFFIX})
        set(TEST_TARGET_LIBRARIES
            ${TEST_TARGET_LIBRARIES}
            chroot_wrapper
        )
        set(CHROOT_WRAPPER "${CHROOT_WRAPPER_LIBRARY}")
    endif()

    # fs wrapper
    add_library(fs_wrapper SHARED fs_wrapper.c)
    set(FS_WRAPPER_LIBRARY
        ${libssh_BINARY_DIR}/lib/${CMAKE_SHARED_LIBRARY_PREFIX}fs_wrapper${CMAKE_SHARED_LIBRARY_SUFFIX})
    set(TEST_TARGET_LIBRARIES
        ${TEST_TARGET_LIBRARIES}
        fs_wrapper
    )
    set(FS_WRAPPER "${FS_WRAPPER_LIBRARY}")

    # ssh_ping
    add_executable(ssh_ping ssh_ping.c)
    target_compile_options(ssh_ping PRIVATE ${DEFAULT_C_COMPILE_FLAGS})
    target_link_libraries(ssh_ping ssh::static pthread)

    # homedir will be used in passwd
    set(HOMEDIR ${CMAKE_CURRENT_BINARY_DIR}/home)

    ### Setup nss_wrapper
    configure_file(etc/passwd.in ${CMAKE_CURRENT_BINARY_DIR}/etc/passwd @ONLY)
    configure_file(etc/shadow.in ${CMAKE_CURRENT_BINARY_DIR}/etc/shadow @ONLY)
    configure_file(etc/group.in ${CMAKE_CURRENT_BINARY_DIR}/etc/group @ONLY)
    configure_file(etc/hosts.in ${CMAKE_CURRENT_BINARY_DIR}/etc/hosts @ONLY)

    ### Setup pam_wrapper
    configure_file(etc/pam_matrix_passdb.in ${CMAKE_CURRENT_BINARY_DIR}/etc/pam_matrix_passdb @ONLY)
    configure_file(etc/pam.d/sshd.in ${CMAKE_CURRENT_BINARY_DIR}/etc/pam.d/sshd @ONLY)


    set(TORTURE_ENVIRONMENT
        "LD_PRELOAD=${FS_WRAPPER}:${SOCKET_WRAPPER_LIBRARY}:${NSS_WRAPPER_LIBRARY}:${UID_WRAPPER_LIBRARY}:${PAM_WRAPPER_LIBRARY}:${CHROOT_WRAPPER}")
    if (priv_wrapper_FOUND)
        list(APPEND TORTURE_ENVIRONMENT PRIV_WRAPPER=1 PRIV_WRAPPER_CHROOT_DISABLE=1)
        list(APPEND TORTURE_ENVIRONMENT PRIV_WRAPPER_PRCTL_DISABLE="ALL" PRIV_WRAPPER_SETRLIMIT_DISABLE="ALL")
    endif()
    list(APPEND TORTURE_ENVIRONMENT UID_WRAPPER=1 UID_WRAPPER_ROOT=1)
    list(APPEND TORTURE_ENVIRONMENT NSS_WRAPPER_PASSWD=${CMAKE_CURRENT_BINARY_DIR}/etc/passwd)
    list(APPEND TORTURE_ENVIRONMENT NSS_WRAPPER_SHADOW=${CMAKE_CURRENT_BINARY_DIR}/etc/shadow)
    list(APPEND TORTURE_ENVIRONMENT NSS_WRAPPER_GROUP=${CMAKE_CURRENT_BINARY_DIR}/etc/group)
    list(APPEND TORTURE_ENVIRONMENT PAM_WRAPPER_SERVICE_DIR=${CMAKE_CURRENT_BINARY_DIR}/etc/pam.d)
    list(APPEND TORTURE_ENVIRONMENT LSAN_OPTIONS=suppressions=${CMAKE_CURRENT_SOURCE_DIR}/suppressions/lsan.supp)
    list(APPEND TORTURE_ENVIRONMENT OPENSSL_ENABLE_SHA1_SIGNATURES=1)

    # Give bob some keys
    file(COPY keys/id_rsa DESTINATION ${CMAKE_CURRENT_BINARY_DIR}/home/bob/.ssh/ FILE_PERMISSIONS OWNER_READ OWNER_WRITE)
    file(COPY keys/id_rsa.pub DESTINATION ${CMAKE_CURRENT_BINARY_DIR}/home/bob/.ssh/ FILE_PERMISSIONS OWNER_READ OWNER_WRITE)
    # Same as id_rsa, protected with passphrase "secret"
    file(COPY keys/id_rsa_protected DESTINATION ${CMAKE_CURRENT_BINARY_DIR}/home/bob/.ssh/ FILE_PERMISSIONS OWNER_READ OWNER_WRITE)
    file(COPY keys/id_rsa_protected.pub DESTINATION ${CMAKE_CURRENT_BINARY_DIR}/home/bob/.ssh/ FILE_PERMISSIONS OWNER_READ OWNER_WRITE)
    file(COPY keys/id_ecdsa DESTINATION ${CMAKE_CURRENT_BINARY_DIR}/home/bob/.ssh/ FILE_PERMISSIONS OWNER_READ OWNER_WRITE)
    file(COPY keys/id_ecdsa.pub DESTINATION ${CMAKE_CURRENT_BINARY_DIR}/home/bob/.ssh/ FILE_PERMISSIONS OWNER_READ OWNER_WRITE)
    file(COPY keys/id_ed25519 DESTINATION ${CMAKE_CURRENT_BINARY_DIR}/home/bob/.ssh/ FILE_PERMISSIONS OWNER_READ OWNER_WRITE)
    file(COPY keys/id_ed25519.pub DESTINATION ${CMAKE_CURRENT_BINARY_DIR}/home/bob/.ssh/ FILE_PERMISSIONS OWNER_READ OWNER_WRITE)
    # Security key support
    file(COPY keys/id_ecdsa_sk DESTINATION ${CMAKE_CURRENT_BINARY_DIR}/home/bob/.ssh/ FILE_PERMISSIONS OWNER_READ OWNER_WRITE)
    file(COPY keys/id_ecdsa_sk.pub DESTINATION ${CMAKE_CURRENT_BINARY_DIR}/home/bob/.ssh/ FILE_PERMISSIONS OWNER_READ OWNER_WRITE)
    file(COPY keys/id_ed25519_sk DESTINATION ${CMAKE_CURRENT_BINARY_DIR}/home/bob/.ssh/ FILE_PERMISSIONS OWNER_READ OWNER_WRITE)
    file(COPY keys/id_ed25519_sk.pub DESTINATION ${CMAKE_CURRENT_BINARY_DIR}/home/bob/.ssh/ FILE_PERMISSIONS OWNER_READ OWNER_WRITE)

    # Allow to auth with bob's public keys on alice and doe account
    configure_file(keys/id_rsa.pub ${CMAKE_CURRENT_BINARY_DIR}/home/alice/.ssh/authorized_keys @ONLY)
    configure_file(keys/id_rsa.pub ${CMAKE_CURRENT_BINARY_DIR}/home/doe/.ssh/authorized_keys @ONLY)
    configure_file(keys/id_ecdsa.pub ${CMAKE_CURRENT_BINARY_DIR}/home/frank/.ssh/authorized_keys @ONLY)

    # append ECDSA public key
    file(READ keys/id_ecdsa.pub CONTENTS)
    file(APPEND ${CMAKE_CURRENT_BINARY_DIR}/home/alice/.ssh/authorized_keys "${CONTENTS}")

    # append ed25519 public key
    file(READ keys/id_ed25519.pub CONTENTS)
    file(APPEND ${CMAKE_CURRENT_BINARY_DIR}/home/alice/.ssh/authorized_keys "${CONTENTS}")

    # append sk-ecdsa public key
    file(READ keys/id_ecdsa_sk.pub CONTENTS)
    file(APPEND ${CMAKE_CURRENT_BINARY_DIR}/home/alice/.ssh/authorized_keys "${CONTENTS}")

    # append sk-ed25519 public key
    file(READ keys/id_ed25519_sk.pub CONTENTS)
    file(APPEND ${CMAKE_CURRENT_BINARY_DIR}/home/alice/.ssh/authorized_keys "${CONTENTS}")

    # Allow to auth with bob his public keys on charlie account
    configure_file(keys/pkcs11/id_pkcs11_rsa_openssh.pub ${CMAKE_CURRENT_BINARY_DIR}/home/charlie/.ssh/authorized_keys @ONLY)

    # append ECDSA public key
    file(READ keys/pkcs11/id_pkcs11_ecdsa_256_openssh.pub CONTENTS)
    file(APPEND ${CMAKE_CURRENT_BINARY_DIR}/home/charlie/.ssh/authorized_keys "${CONTENTS}")

    file(READ keys/pkcs11/id_pkcs11_ecdsa_384_openssh.pub CONTENTS)
    file(APPEND ${CMAKE_CURRENT_BINARY_DIR}/home/charlie/.ssh/authorized_keys "${CONTENTS}")

    file(READ keys/pkcs11/id_pkcs11_ecdsa_521_openssh.pub CONTENTS)
    file(APPEND ${CMAKE_CURRENT_BINARY_DIR}/home/charlie/.ssh/authorized_keys "${CONTENTS}")

    file(READ keys/pkcs11/id_pkcs11_ed25519_openssh.pub CONTENTS)
    file(APPEND ${CMAKE_CURRENT_BINARY_DIR}/home/charlie/.ssh/authorized_keys "${CONTENTS}")

    # Copy the signed key to an doe's homedir.
    file(COPY keys/certauth/id_rsa DESTINATION
        ${CMAKE_CURRENT_BINARY_DIR}/home/doe/.ssh/ FILE_PERMISSIONS OWNER_READ OWNER_WRITE)
    file(COPY keys/certauth/id_rsa.pub DESTINATION
        ${CMAKE_CURRENT_BINARY_DIR}/home/doe/.ssh/ FILE_PERMISSIONS OWNER_READ OWNER_WRITE)
    file(COPY keys/certauth/id_rsa-cert.pub DESTINATION
        ${CMAKE_CURRENT_BINARY_DIR}/home/doe/.ssh/ FILE_PERMISSIONS OWNER_READ OWNER_WRITE)
endif ()

file(COPY gss/kdcsetup.sh DESTINATION ${CMAKE_CURRENT_BINARY_DIR}/gss FILE_PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE)

message(STATUS "TORTURE_ENVIRONMENT=${TORTURE_ENVIRONMENT}")

configure_file(tests_config.h.cmake ${CMAKE_CURRENT_BINARY_DIR}/tests_config.h)

if (WITH_BENCHMARKS)
    add_subdirectory(benchmarks)
endif ()

if (CLIENT_TESTING)
    add_subdirectory(client)

    # Only add override testing if testing the client
    add_subdirectory(external_override)
endif ()

if (WITH_SERVER AND SERVER_TESTING)
    add_subdirectory(pkd)
    add_subdirectory(server)
endif ()

if (FUZZ_TESTING)
    add_subdirectory(fuzz)
endif()

add_custom_target(test_memcheck
    # FIXME: The pkd_hello_i1 test is skipped under valgrind as it times out
    # Passing suppression file is also stupid so lets go with override here:
    # https://stackoverflow.com/a/56116311
    COMMAND ${CMAKE_CTEST_COMMAND} -E pkd_hello_i1
        --output-on-failure --force-new-ctest-process --test-action memcheck
        --overwrite MemoryCheckSuppressionFile=${CMAKE_SOURCE_DIR}/tests/valgrind.supp
    WORKING_DIRECTORY "${CMAKE_BINARY_DIR}")
