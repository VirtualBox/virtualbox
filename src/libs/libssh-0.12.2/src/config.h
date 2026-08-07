/* Name of package */
#define PACKAGE "libssh"

/* Version number of package */
#define VERSION "0.11.4"

#define SYSCONFDIR "etc"
//#define BINARYDIR "C:/Src/libssh-0.9.5/build"
//#define SOURCEDIR "C:/Src/libssh-0.9.5"

/* Global configuration directory */
/* #undef USR_GLOBAL_CONF_DIR */
#ifndef RT_OS_WINDOWS
#define GLOBAL_CONF_DIR "/etc/ssh"
#else
#define GLOBAL_CONF_DIR "C:/ProgramData/ssh"
#endif

/* Global bind configuration file path */
#define GLOBAL_BIND_CONFIG "/etc/ssh/libssh_server_config"

/* Global client configuration file path */
#define GLOBAL_CLIENT_CONFIG "/etc/ssh/ssh_config"

/************************** HEADER FILES *************************/

/* Define to 1 if you have the <argp.h> header file. */
/* #undef HAVE_ARGP_H */

#ifndef RT_OS_WINDOWS
/* Define to 1 if you have the <aprpa/inet.h> header file. */
#define HAVE_ARPA_INET_H 1

/* Define to 1 if you have the <glob.h> header file. */
#define HAVE_GLOB_H 1
#endif
/* Define to 1 if you have the <valgrind/valgrind.h> header file. */
/* #undef HAVE_VALGRIND_VALGRIND_H */

/* Define to 1 if you have the <pty.h> header file. */
/* #undef HAVE_PTY_H */

#ifndef RT_OS_WINDOWS
/* Define to 1 if you have the <utmp.h> header file. */
#define HAVE_UTMP_H 1

/* Define to 1 if you have the <util.h> header file. */
#define HAVE_UTIL_H 1
#endif

/* Define to 1 if you have the <libutil.h> header file. */
/* #undef HAVE_LIBUTIL_H */

#ifndef RT_OS_WINDOWS
/* Define to 1 if you have the <sys/time.h> header file. */
#define HAVE_SYS_TIME_H
#endif

/* Define to 1 if you have the <sys/utime.h> header file. */
//#define HAVE_SYS_UTIME_H 1

#ifdef RT_OS_WINDOWS
/* Define to 1 if you have the <io.h> header file. */
#define HAVE_IO_H 1
#else
/* Define to 1 if you have the <termios.h> header file. */
#define HAVE_TERMIOS_H 1

/* Define to 1 if you have the <unistd.h> header file. */
#define HAVE_UNISTD_H 1
#endif

/* Define to 1 if you have the <stdint.h> header file. */
#define HAVE_STDINT_H 1

/* Define to 1 if you have the <openssl/aes.h> header file. */
#define HAVE_OPENSSL_AES_H 1

#ifdef RT_OS_WINDOWS
/* Define to 1 if you have the <wspiapi.h> header file. */
#define HAVE_WSPIAPI_H 1
#endif
/* Define to 1 if you have the <openssl/blowfish.h> header file. */
/* #undef HAVE_OPENSSL_BLOWFISH_H */

/* Define to 1 if you have the <openssl/des.h> header file. */
//#define HAVE_OPENSSL_DES_H 1

/* Define to 1 if you have the <openssl/ecdh.h> header file. */
#define HAVE_OPENSSL_ECDH_H 1

/* Define to 1 if you have the <openssl/ec.h> header file. */
#define HAVE_OPENSSL_EC_H 1

/* Define to 1 if you have the <openssl/ecdsa.h> header file. */
#define HAVE_OPENSSL_ECDSA_H 1

/* Define to 1 if you have the <pthread.h> header file. */
/* #undef HAVE_PTHREAD_H */

/* Define to 1 if you have eliptic curve cryptography in openssl */
#define HAVE_OPENSSL_ECC 1

/* Define to 1 if you have eliptic curve cryptography in gcrypt */
/* #undef HAVE_GCRYPT_ECC */

/* Define to 1 if you have eliptic curve cryptography */
#define HAVE_ECC 1

/* Define to 1 if you have DSA */
#define HAVE_DSA 1

/* Define to 1 if you have gl_flags as a glob_t sturct member */
/* #undef HAVE_GLOB_GL_FLAGS_MEMBER */

/* Define to 1 if you have OpenSSL with Ed25519 support */
//#define HAVE_OPENSSL_ED25519 1

/* Define to 1 if you have OpenSSL with X25519 support */
//#define HAVE_OPENSSL_X25519 1

/*************************** FUNCTIONS ***************************/

/* Define to 1 if you have the `EVP_aes128_ctr' function. */
#define HAVE_OPENSSL_EVP_AES_CTR 1

/* Define to 1 if you have the `EVP_aes128_cbc' function. */
#define HAVE_OPENSSL_EVP_AES_CBC 1

/* Define to 1 if you have the `EVP_aes128_gcm' function. */
#define HAVE_OPENSSL_EVP_AES_GCM 1

/* Define to 1 if you have the `CRYPTO_THREADID_set_callback' function. */
/* #undef HAVE_OPENSSL_CRYPTO_THREADID_SET_CALLBACK */

/* Define to 1 if you have the `CRYPTO_ctr128_encrypt' function. */
#define HAVE_OPENSSL_CRYPTO_CTR128_ENCRYPT 1

/* Define to 1 if you have the `EVP_CIPHER_CTX_new' function. */
#define HAVE_OPENSSL_EVP_CIPHER_CTX_NEW 1

/* Define to 1 if you have the `EVP_KDF_CTX_new_id' function. */
/* #undef HAVE_OPENSSL_EVP_KDF_CTX_NEW_ID */

#ifndef VBOX
/* Define to 1 if you have the `FIPS_mode' function. */
#define HAVE_OPENSSL_FIPS_MODE 1
#endif

/* Define to 1 if you have the `EVP_DigestSign' function. */
//#define HAVE_OPENSSL_EVP_DIGESTSIGN 1

/* Define to 1 if you have the `EVP_DigestVerify' function. */
//#define HAVE_OPENSSL_EVP_DIGESTVERIFY 1

/* Define to 1 if you have the `OPENSSL_ia32cap_loc' function. */
/* #undef HAVE_OPENSSL_IA32CAP_LOC */

/* Define to 1 if you have the `snprintf' function. */
#define HAVE_SNPRINTF 1

#ifdef RT_OS_WINDOWS
/* Define to 1 if you have the `_snprintf' function. */
#define HAVE__SNPRINTF 1

/* Define to 1 if you have the `_snprintf_s' function. */
#define HAVE__SNPRINTF_S 1
#endif
/* Define to 1 if you have the `vsnprintf' function. */
#define HAVE_VSNPRINTF 1

#ifdef RT_OS_WINDOWS
/* Define to 1 if you have the `_vsnprintf' function. */
#define HAVE__VSNPRINTF 1

/* Define to 1 if you have the `_vsnprintf_s' function. */
#define HAVE__VSNPRINTF_S 1
#endif

/* Define to 1 if you have the `isblank' function. */
#define HAVE_ISBLANK 1

/* Define to 1 if you have the `strncpy' function. */
#define HAVE_STRNCPY 1

#if defined(RT_OS_DARWIN) || defined(RT_OS_LINUX)
/* Define to 1 if you have the `strndup' function. */
#define HAVE_STRNDUP 1
#endif

#ifndef RT_OS_WINDOWS
/* Define to 1 if you have the `cfmakeraw' function. */
#define HAVE_CFMAKERAW 1
#endif

/* Define to 1 if you have the `getaddrinfo' function. */
#define HAVE_GETADDRINFO 1

#ifndef RT_OS_WINDOWS
/* Define to 1 if you have the `poll' function. */
#define HAVE_POLL 1
#endif

/* Define to 1 if you have the `select' function. */
#define HAVE_SELECT 1

/* Define to 1 if you have the `clock_gettime' function. */
/* #undef HAVE_CLOCK_GETTIME */

#if defined(RT_OS_WINDOWS) || (defined(RT_OS_DARWIN) && MAC_OS_X_VERSION_MIN_REQUIRED > 1090)
/* Define to 1 if you have the `ntohll' function. */
#define HAVE_NTOHLL 1

/* Define to 1 if you have the `htonll' function. */
#define HAVE_HTONLL 1
#endif

/* Define to 1 if you have the `strtoull' function. */
#define HAVE_STRTOULL 1

/* Define to 1 if you have the `__strtoull' function. */
/* #undef HAVE___STRTOULL */

#ifdef RT_OS_WINDOWS
/* Define to 1 if you have the `_strtoui64' function. */
#define HAVE__STRTOUI64 1
#else
/* Define to 1 if you have the `glob' function. */
#define HAVE_GLOB 1
#endif

/* Define to 1 if you have the `explicit_bzero' function. */
/* #undef HAVE_EXPLICIT_BZERO */

#ifdef RT_OS_DARWIN
/* Define to 1 if you have the `memset_s' function. */
#define HAVE_MEMSET_S 1
#endif

#ifdef RT_OS_WINDOWS
/* Define to 1 if you have the `SecureZeroMemory' function. */
#define HAVE_SECURE_ZERO_MEMORY 1
#endif

/* Define to 1 if you have the `cmocka_set_test_filter' function. */
/* #undef HAVE_CMOCKA_SET_TEST_FILTER */

/*************************** LIBRARIES ***************************/

/* Define to 1 if you have the `crypto' library (-lcrypto). */
#define HAVE_LIBCRYPTO 1

/* Define to 1 if you have the `gcrypt' library (-lgcrypt). */
/* #undef HAVE_LIBGCRYPT */

/* Define to 1 if you have the 'mbedTLS' library (-lmbedtls). */
/* #undef HAVE_LIBMBEDCRYPTO */

#ifndef RT_OS_WINDOWS
/* Define to 1 if you have the `pthread' library (-lpthread). */
#define HAVE_PTHREAD 1
#endif

/* Define to 1 if you have the `cmocka' library (-lcmocka). */
/* #undef HAVE_CMOCKA */

/**************************** OPTIONS ****************************/

#ifndef RT_OS_WINDOWS
#define HAVE_GCC_THREAD_LOCAL_STORAGE 1
#else
#define HAVE_MSC_THREAD_LOCAL_STORAGE 1
#endif

/* #undef HAVE_FALLTHROUGH_ATTRIBUTE */
/* #undef HAVE_UNUSED_ATTRIBUTE */

/* #undef HAVE_CONSTRUCTOR_ATTRIBUTE */
/* #undef HAVE_DESTRUCTOR_ATTRIBUTE */

/* #undef HAVE_GCC_VOLATILE_MEMORY_PROTECTION */

#define HAVE_COMPILER__FUNC__ 1
#define HAVE_COMPILER__FUNCTION__ 1

/* #undef HAVE_GCC_BOUNDED_ATTRIBUTE */

/* Define to 1 if you want to enable GSSAPI */
/* #undef WITH_GSSAPI */

/* Define to 1 if you want to enable ZLIB */
#define WITH_ZLIB 1

/* Define to 1 if you want to enable SFTP */
#define WITH_SFTP 1

/* Define to 1 if you want to enable server support */
//#define WITH_SERVER 1

/* Define to 1 if you want to enable DH group exchange algorithms */
//#define WITH_GEX 1

/* Define to 1 if you want to enable blowfish cipher support */
/* #undef WITH_BLOWFISH_CIPHER */

/* Define to 1 if you want to enable debug output for crypto functions */
/* #undef DEBUG_CRYPTO */

/* Define to 1 if you want to enable debug output for packet functions */
/* #undef DEBUG_PACKET */

/* Define to 1 if you want to enable pcap output support (experimental) */
#define WITH_PCAP 1

/* Define to 1 if you want to enable calltrace debug output */
#define DEBUG_CALLTRACE 1

/* Define to 1 if you want to enable NaCl support */
/* #undef WITH_NACL */

/*************************** ENDIAN *****************************/

/* Define WORDS_BIGENDIAN to 1 if your processor stores words with the most
   significant byte first (like Motorola and SPARC, unlike Intel). */
/* #undef WORDS_BIGENDIAN */

#ifdef RT_OS_WINDOWS
unsigned __int64 htonll(unsigned __int64 Value);
unsigned __int64 ntohll(unsigned __int64 Value);
#endif

#if defined(RT_OS_LINUX) && !defined(LLONG_MAX)
#define LLONG_MAX	9223372036854775807LL
#endif
