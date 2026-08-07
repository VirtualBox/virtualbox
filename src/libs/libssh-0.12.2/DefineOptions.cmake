option(WITH_GSSAPI "Build with GSSAPI support" ON)
option(WITH_ZLIB "Build with ZLIB support" ON)
option(WITH_SFTP "Build with SFTP support" ON)
option(WITH_SERVER "Build with SSH server support" ON)
option(WITH_DEBUG_CRYPTO "Build with crypto debug output" OFF)
option(WITH_DEBUG_PACKET "Build with packet debug output" OFF)
option(WITH_DEBUG_CALLTRACE "Build with calltrace debug output" ON)
option(WITH_GCRYPT "Compile against libgcrypt (deprecated)" OFF)
option(WITH_MBEDTLS "Compile against libmbedtls" OFF)
option(WITH_BLOWFISH_CIPHER "Compile with blowfish support" OFF)
option(WITH_PCAP "Compile with Pcap generation support" ON)
option(WITH_INTERNAL_DOC "Compile doxygen internal documentation" OFF)
option(BUILD_SHARED_LIBS "Build shared libraries" ON)
option(WITH_PKCS11_URI "Build with PKCS#11 URI support" OFF)
option(WITH_PKCS11_PROVIDER
       "Use the PKCS#11 provider for accessing pkcs11 objects" OFF)
option(WITH_FIDO2 "Build with FIDO2/U2F support" OFF)
option(UNIT_TESTING "Build with unit tests" OFF)
option(CLIENT_TESTING "Build with client tests; requires openssh" OFF)
option(SERVER_TESTING "Build with server tests; requires openssh and dropbear"
       OFF)
option(
    GSSAPI_TESTING
    "Build with GSSAPI tests; requires krb5-server,krb5-libs and krb5-workstation"
    OFF)
option(WITH_BENCHMARKS
       "Build benchmarks tools; enables unit testing and client tests" OFF)
option(WITH_EXAMPLES "Build examples" ON)
option(WITH_NACL "Build with libnacl (curve25519)" ON)
option(WITH_SYMBOL_VERSIONING "Build with symbol versioning" ON)
option(WITH_ABI_BREAK "Allow ABI break" OFF)
option(WITH_GEX "Enable DH Group exchange mechanisms" ON)
option(
    WITH_INSECURE_NONE
    "Enable insecure none cipher and MAC algorithms (not suitable for production!)"
    OFF)
option(
    WITH_EXEC
    "Enable libssh to execute arbitrary commands from configuration files or options (match exec, proxy commands and OpenSSH-based proxy-jumps)."
    ON)
option(
    FUZZ_TESTING
    "Build with fuzzer for the server and client (automatically enables none cipher!)"
    OFF)
option(PICKY_DEVELOPER "Build with picky developer flags" OFF)
option(WITH_HERMETIC_USR "Build with support for hermetic /usr/" OFF)

if (WITH_ZLIB)
    set(WITH_LIBZ ON)
else (WITH_ZLIB)
    set(WITH_LIBZ OFF)
endif (WITH_ZLIB)

if (WITH_BENCHMARKS)
    set(UNIT_TESTING ON)
    set(CLIENT_TESTING ON)
endif ()

if (UNIT_TESTING
    OR CLIENT_TESTING
    OR SERVER_TESTING
    OR GSSAPI_TESTING)
    set(BUILD_STATIC_LIB ON)
endif ()

if (WITH_NACL)
    set(WITH_NACL ON)
endif (WITH_NACL)

if (WITH_ABI_BREAK)
    set(WITH_SYMBOL_VERSIONING ON)
endif (WITH_ABI_BREAK)

set(GLOBAL_CONF_DIR "/etc/ssh")
if (WIN32)
    # Use PROGRAMDATA on Windows
    if (DEFINED ENV{PROGRAMDATA})
        set(GLOBAL_CONF_DIR "$ENV{PROGRAMDATA}/ssh")
    else ()
        set(GLOBAL_CONF_DIR "C:/ProgramData/ssh")
    endif ()
    if (WITH_HERMETIC_USR)
        set(USR_GLOBAL_CONF_DIR "/usr${GLOBAL_CONF_DIR}")
    endif ()
endif ()

if (NOT GLOBAL_BIND_CONFIG)
    set(GLOBAL_BIND_CONFIG "${GLOBAL_CONF_DIR}/libssh_server_config")

    if (WITH_HERMETIC_USR)
        set(USR_GLOBAL_BIND_CONFIG "/usr${GLOBAL_BIND_CONFIG}")
    endif ()
endif (NOT GLOBAL_BIND_CONFIG)

if (NOT GLOBAL_CLIENT_CONFIG)
    set(GLOBAL_CLIENT_CONFIG "${GLOBAL_CONF_DIR}/ssh_config")

    if (WITH_HERMETIC_USR)
        set(USR_GLOBAL_CLIENT_CONFIG "/usr${GLOBAL_CLIENT_CONFIG}")
    endif ()
endif (NOT GLOBAL_CLIENT_CONFIG)

if (FUZZ_TESTING)
    set(WITH_INSECURE_NONE ON)
endif (FUZZ_TESTING)

if (WIN32)
    set(WITH_EXEC 0)
endif (WIN32)
