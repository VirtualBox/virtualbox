#include "config.h"

#define LIBSSH_STATIC

#ifndef _WIN32
#define _POSIX_PTHREAD_SEMANTICS
#include <pwd.h>
#endif

#include "torture.h"
#include "libssh/options.h"
#include "libssh/session.h"
#include "libssh/config_parser.h"
#include "match.c"
#include "config.c"
#include "libssh/socket.h"
#include "libssh/misc.h"

extern LIBSSH_THREAD int ssh_log_level;

#define USERNAME "testuser"
#define PROXYCMD "ssh -q -W %h:%p gateway.example.com"
#define ID_FILE "/etc/xxx"
#define KEXALGORITHMS "ecdh-sha2-nistp521,diffie-hellman-group16-sha512,diffie-hellman-group18-sha512,diffie-hellman-group14-sha1"
#define HOSTKEYALGORITHMS "ssh-ed25519,ecdsa-sha2-nistp521,ssh-rsa"
#define PUBKEYACCEPTEDTYPES "rsa-sha2-512,ssh-rsa,ecdsa-sha2-nistp521"
#define MACS "hmac-sha1,hmac-sha2-256,hmac-sha2-512,hmac-sha1-etm@openssh.com,hmac-sha2-256-etm@openssh.com,hmac-sha2-512-etm@openssh.com"
#define USER_KNOWN_HOSTS "%d/my_known_hosts"
#define GLOBAL_KNOWN_HOSTS "/etc/ssh/my_ssh_known_hosts"
#define BIND_ADDRESS "::1"


#define LIBSSH_TESTCONFIG1 "libssh_testconfig1.tmp"
#define LIBSSH_TESTCONFIG2 "libssh_testconfig2.tmp"
#define LIBSSH_TESTCONFIG3 "libssh_testconfig3.tmp"
#define LIBSSH_TESTCONFIG4 "libssh_testconfig4.tmp"
#define LIBSSH_TESTCONFIG5 "libssh_testconfig5.tmp"
#define LIBSSH_TESTCONFIG6 "libssh_testconfig6.tmp"
#define LIBSSH_TESTCONFIG7 "libssh_testconfig7.tmp"
#define LIBSSH_TESTCONFIG8 "libssh_testconfig8.tmp"
#define LIBSSH_TESTCONFIG9 "libssh_testconfig9.tmp"
#define LIBSSH_TESTCONFIG10 "libssh_testconfig10.tmp"
#define LIBSSH_TESTCONFIG11 "libssh_testconfig11.tmp"
#define LIBSSH_TESTCONFIG12 "libssh_testconfig12.tmp"
#define LIBSSH_TESTCONFIG14 "libssh_testconfig14.tmp"
#define LIBSSH_TESTCONFIG15 "libssh_testconfig15.tmp"
#define LIBSSH_TESTCONFIG16 "libssh_testconfig16.tmp"
#define LIBSSH_TESTCONFIG17 "libssh_testconfig17.tmp"
#define LIBSSH_TESTCONFIGGLOB "libssh_testc*[36].tmp"
#define LIBSSH_TEST_PUBKEYTYPES "libssh_test_PubkeyAcceptedKeyTypes.tmp"
#define LIBSSH_TEST_PUBKEYALGORITHMS "libssh_test_PubkeyAcceptedAlgorithms.tmp"
#define LIBSSH_TEST_NONEWLINEEND "libssh_test_NoNewLineEnd.tmp"
#define LIBSSH_TEST_NONEWLINEONELINE "libssh_test_NoNewLineOneline.tmp"
#define LIBSSH_TEST_RECURSIVE_INCLUDE "libssh_test_recursive_include.tmp"
#define LIBSSH_TESTCONFIG_MATCH_COMPLEX "libssh_test_match_complex.tmp"
#define LIBSSH_TESTCONFIG_LOGLEVEL_MISSING "libssh_test_loglevel_missing.tmp"
#define LIBSSH_TESTCONFIG_JUMP "libssh_test_jump.tmp"

#define LIBSSH_TESTCONFIG_STRING1 \
    "User "USERNAME"\nInclude "LIBSSH_TESTCONFIG2"\n\n"

#define LIBSSH_TESTCONFIG_STRING2 \
    "Include "LIBSSH_TESTCONFIG3"\n" \
    "ProxyCommand "PROXYCMD"\n\n"

#define LIBSSH_TESTCONFIG_STRING3 \
    "\n\nIdentityFile "ID_FILE"\n" \
    "\n\nKexAlgorithms "KEXALGORITHMS"\n" \
    "\n\nHostKeyAlgorithms "HOSTKEYALGORITHMS"\n" \
    "\n\nPubkeyAcceptedAlgorithms "PUBKEYACCEPTEDTYPES"\n" \
    "\n\nMACs "MACS"\n"

/* Multiple Port settings -> parsing returns early. */
#define LIBSSH_TESTCONFIG_STRING4 \
   "Port 123\nPort 456\n"

/* Testing glob include */
#define LIBSSH_TESTCONFIG_STRING5 \
    "User "USERNAME"\nInclude "LIBSSH_TESTCONFIGGLOB"\n\n" \

#define LIBSSH_TESTCONFIG_STRING6 \
    "ProxyCommand "PROXYCMD"\n\n"

/* new options */
#define LIBSSH_TESTCONFIG_STRING7 \
    "\tBindAddress "BIND_ADDRESS"\n" \
    "\tConnectTimeout 30\n" \
    "\tLogLevel DEBUG3\n" \
    "\tGlobalKnownHostsFile "GLOBAL_KNOWN_HOSTS"\n" \
    "\tCompression yes\n" \
    "\tStrictHostkeyChecking no\n" \
    "\tGSSAPIDelegateCredentials yes\n" \
    "\tGSSAPIServerIdentity example.com\n" \
    "\tGSSAPIClientIdentity home.sweet\n" \
    "\tUserKnownHostsFile "USER_KNOWN_HOSTS"\n"

/* authentication methods */
#define LIBSSH_TESTCONFIG_STRING8 \
    "Host gss\n" \
    "\tGSSAPIAuthentication yes\n" \
    "Host kbd\n" \
    "\tKbdInteractiveAuthentication yes\n" \
    "Host pass\n" \
    "\tPasswordAuthentication yes\n" \
    "Host pubkey\n" \
    "\tPubkeyAuthentication yes\n" \
    "Host nogss\n" \
    "\tGSSAPIAuthentication no\n" \
    "Host nokbd\n" \
    "\tKbdInteractiveAuthentication no\n" \
    "Host nopass\n" \
    "\tPasswordAuthentication no\n" \
    "Host nopubkey\n" \
    "\tPubkeyAuthentication no\n"

/* unsupported options and corner cases */
#define LIBSSH_TESTCONFIG_STRING9 \
    "\n" /* empty line */ \
    "# comment line\n" \
    "  # comment line not starting with hash\n" \
    "UnknownConfigurationOption yes\n" \
    "GSSAPIKexAlgorithms yes\n" \
    "ControlMaster auto\n" /* SOC_NA */ \
    "VisualHostkey yes\n" /* SOC_UNSUPPORTED */ \
    "HostName =equal.sign\n" /* valid */ \
    "ProxyJump = many-spaces.com\n" /* valid */

/* Match keyword */
#define LIBSSH_TESTCONFIG_STRING10 \
    "Match host example\n" \
    "\tHostName example.com\n" \
    "Match host example1,example2\n" \
    "\tHostName exampleN\n" \
    "Match user guest\n" \
    "\tHostName guest.com\n" \
    "Match user tester host testhost\n" \
    "\tHostName testhost.com\n" \
    "Match !user tester host testhost\n" \
    "\tHostName nonuser-testhost.com\n" \
    "Match all\n" \
    "\tHostName all-matched.com\n" \
    /* Unsupported options */ \
    "Match originalhost example\n" \
    "\tHostName original-example.com\n" \
    "Match localuser guest\n" \
    "\tHostName local-guest.com\n"

/* ProxyJump */
#define LIBSSH_TESTCONFIG_STRING11 \
    "Host simple\n" \
    "\tProxyJump jumpbox\n" \
    "Host user\n" \
    "\tProxyJump user@jumpbox\n" \
    "Host port\n" \
    "\tProxyJump jumpbox:2222\n" \
    "Host two-step\n" \
    "\tProxyJump u1@first:222,u2@second:33\n" \
    "Host none\n" \
    "\tProxyJump none\n" \
    "Host only-command\n" \
    "\tProxyCommand "PROXYCMD"\n" \
    "\tProxyJump jumpbox\n" \
    "Host only-jump\n" \
    "\tProxyJump jumpbox\n" \
    "\tProxyCommand "PROXYCMD"\n" \
    "Host ipv6\n" \
    "\tProxyJump [2620:52:0::fed]\n"

/* RekeyLimit combinations */
#define LIBSSH_TESTCONFIG_STRING12 \
    "Host default\n" \
    "\tRekeyLimit default none\n" \
    "Host data1\n" \
    "\tRekeyLimit 42G\n" \
    "Host data2\n" \
    "\tRekeyLimit 31M\n" \
    "Host data3\n" \
    "\tRekeyLimit 521K\n" \
    "Host time1\n" \
    "\tRekeyLimit default 3D\n" \
    "Host time2\n" \
    "\tRekeyLimit default 2h\n" \
    "Host time3\n" \
    "\tRekeyLimit default 160m\n" \
    "Host time4\n" \
    "\tRekeyLimit default 9600\n"

/* Multiple IdentityFile settings all are applied */
#define LIBSSH_TESTCONFIG_STRING13 \
   "IdentityFile id_rsa_one\n" \
   "CertificateFile id_rsa_one-cert.pub\n" \
   "IdentityFile id_ecdsa_two\n" \
   "CertificateFile id_ecdsa_two-cert.pub\n" \

/* +,-,^ features for all supported list */
/* kex won't work in fips */
#define LIBSSH_TESTCONFIG_STRING14 \
    "HostKeyAlgorithms +ssh-rsa\n" \
    "Ciphers +aes128-cbc,aes256-cbc\n" \
    "KexAlgorithms +diffie-hellman-group14-sha1,diffie-hellman-group1-sha1\n" \
    "MACs +hmac-sha1,hmac-sha1-etm@openssh.com\n"

/* have to be algorithms which are in the default list */
#define LIBSSH_TESTCONFIG_STRING15 \
    "HostKeyAlgorithms -rsa-sha2-512,rsa-sha2-256\n" \
    "Ciphers -aes256-ctr\n" \
    "KexAlgorithms -diffie-hellman-group18-sha512,diffie-hellman-group16-sha512\n" \
    "MACs -hmac-sha2-256-etm@openssh.com\n"

#define LIBSSH_TESTCONFIG_STRING16 \
    "HostKeyAlgorithms ^rsa-sha2-512,rsa-sha2-256\n" \
    "Ciphers ^aes256-cbc\n" \
    "KexAlgorithms ^diffie-hellman-group18-sha512,diffie-hellman-group16-sha512\n" \
    "MACs ^hmac-sha1\n"

/* Connection Multiplexing */
#define LIBSSH_TESTCONFIG_STRING17 \
    "Host simple\n" \
    "\tControlMaster auto\n" \
    "\tControlPath /tmp/ssh-%r@%h:%p\n" \
    "Host none\n" \
    "\tControlMaster yes\n" \
    "\tControlPath none\n"

#define LIBSSH_TEST_PUBKEYTYPES_STRING \
    "PubkeyAcceptedKeyTypes "PUBKEYACCEPTEDTYPES"\n"

#define LIBSSH_TEST_PUBKEYALGORITHMS_STRING \
    "PubkeyAcceptedAlgorithms "PUBKEYACCEPTEDTYPES"\n"

#define LIBSSH_TEST_NONEWLINEEND_STRING \
    "ConnectTimeout 30\n" \
    "LogLevel DEBUG3"

#define LIBSSH_TEST_NONEWLINEONELINE_STRING \
    "ConnectTimeout 30"

#define LIBSSH_TEST_RECURSIVE_INCLUDE_STRING \
    "Include " LIBSSH_TEST_RECURSIVE_INCLUDE

/* Complex match cases */
#define LIBSSH_TESTCONFIG_MATCH_COMPLEX_STRING \
    "Match originalhost \"Foo,Bar\" exec \"[ \\\"$(ps h o comm p $(ps h o ppid p $PPID))\\\" != \\\"rsync\\\" ]\"\n" \
    "Match exec \"[ \\\"$(ps h o comm p $(ps h o ppid p $PPID))\\\" != \\\"rsync\\\" ]\"\n" \
    "\tForwardAgent yes\n" \
    "\tHostName complex-match\n"

#define LIBSSH_TESTCONFIG_LOGLEVEL_MISSING_STRING "LogLevel\n"
#define LIBSSH_TESTCONFIG_JUMP_STRING \
    "# The jump host\n" \
    "Host ub-jumphost\n" \
    "    HostName 1xxxxxx\n" \
    "    User ubuntu\n" \
    "    IdentityFile ~/of/temp-libssh.pem\n" \
    "    Port 23\n" \
    "    LogLevel DEBUG3\n" \
    "\n" \
    "# Cisco Router through Jump Host\n" \
    "Host cisco-router\n" \
    "    HostName xx.xxxxxxxxx\n" \
    "    User username\n" \
    "    ProxyJump ub-jumphost\n" \
    "    Port 5555\n" \
    "    #RequiredRSASize 512\n" \
    "    PasswordAuthentication yes\n" \
    "    LogLevel DEBUG3\n"

/**
 * @brief helper function loading configuration from either file or string
 */
static void _parse_config(ssh_session session,
                          const char *file, const char *string, int expected)
{
    int ret = -1;

    /* make sure either config file or config string is given,
     * not both */
    assert_int_not_equal(file == NULL, string == NULL);

    if (file != NULL) {
        ret = ssh_config_parse_file(session, file);
    } else if (string != NULL) {
        ret = ssh_config_parse_string(session, string);
    } else {
        /* should not happen */
        fail();
    }

    /* make sure parsing went as expected */
    assert_ssh_return_code_equal(session, ret, expected);
}

static int setup_config_files(void **state)
{
    (void) state; /* unused */

    unlink(LIBSSH_TESTCONFIG1);
    unlink(LIBSSH_TESTCONFIG2);
    unlink(LIBSSH_TESTCONFIG3);
    unlink(LIBSSH_TESTCONFIG4);
    unlink(LIBSSH_TESTCONFIG5);
    unlink(LIBSSH_TESTCONFIG6);
    unlink(LIBSSH_TESTCONFIG7);
    unlink(LIBSSH_TESTCONFIG8);
    unlink(LIBSSH_TESTCONFIG9);
    unlink(LIBSSH_TESTCONFIG10);
    unlink(LIBSSH_TESTCONFIG11);
    unlink(LIBSSH_TESTCONFIG12);
    unlink(LIBSSH_TESTCONFIG14);
    unlink(LIBSSH_TESTCONFIG15);
    unlink(LIBSSH_TESTCONFIG16);
    unlink(LIBSSH_TESTCONFIG17);
    unlink(LIBSSH_TEST_PUBKEYTYPES);
    unlink(LIBSSH_TEST_PUBKEYALGORITHMS);
    unlink(LIBSSH_TEST_NONEWLINEEND);
    unlink(LIBSSH_TEST_NONEWLINEONELINE);
    unlink(LIBSSH_TESTCONFIG_MATCH_COMPLEX);
    unlink(LIBSSH_TESTCONFIG_LOGLEVEL_MISSING);
    unlink(LIBSSH_TESTCONFIG_JUMP);

    torture_write_file(LIBSSH_TESTCONFIG1,
                       LIBSSH_TESTCONFIG_STRING1);
    torture_write_file(LIBSSH_TESTCONFIG2,
                       LIBSSH_TESTCONFIG_STRING2);
    torture_write_file(LIBSSH_TESTCONFIG3,
                       LIBSSH_TESTCONFIG_STRING3);

    /* Multiple Port settings -> parsing returns early. */
    torture_write_file(LIBSSH_TESTCONFIG4,
                       LIBSSH_TESTCONFIG_STRING4);

    /* Testing glob include */
    torture_write_file(LIBSSH_TESTCONFIG5,
                       LIBSSH_TESTCONFIG_STRING5);

    torture_write_file(LIBSSH_TESTCONFIG6,
                       LIBSSH_TESTCONFIG_STRING6);

    /* new options */
    torture_write_file(LIBSSH_TESTCONFIG7,
                       LIBSSH_TESTCONFIG_STRING7);

    /* authentication methods */
    torture_write_file(LIBSSH_TESTCONFIG8,
                       LIBSSH_TESTCONFIG_STRING8);

    /* unsupported options and corner cases */
    torture_write_file(LIBSSH_TESTCONFIG9,
                       LIBSSH_TESTCONFIG_STRING9);

    /* Match keyword */
    torture_write_file(LIBSSH_TESTCONFIG10,
                       LIBSSH_TESTCONFIG_STRING10);

    /* ProxyJump */
    torture_write_file(LIBSSH_TESTCONFIG11,
                       LIBSSH_TESTCONFIG_STRING11);

    /* RekeyLimit combinations */
    torture_write_file(LIBSSH_TESTCONFIG12,
                       LIBSSH_TESTCONFIG_STRING12);

    /* +,-,^ feature */
    torture_write_file(LIBSSH_TESTCONFIG14,
                       LIBSSH_TESTCONFIG_STRING14);
    torture_write_file(LIBSSH_TESTCONFIG15,
                       LIBSSH_TESTCONFIG_STRING15);
    torture_write_file(LIBSSH_TESTCONFIG16,
                       LIBSSH_TESTCONFIG_STRING16);
    torture_write_file(LIBSSH_TESTCONFIG17,
                       LIBSSH_TESTCONFIG_STRING17);

    torture_write_file(LIBSSH_TEST_PUBKEYTYPES,
                       LIBSSH_TEST_PUBKEYTYPES_STRING);

    torture_write_file(LIBSSH_TEST_PUBKEYALGORITHMS,
                       LIBSSH_TEST_PUBKEYALGORITHMS_STRING);

    torture_write_file(LIBSSH_TEST_NONEWLINEEND,
                       LIBSSH_TEST_NONEWLINEEND_STRING);

    torture_write_file(LIBSSH_TEST_NONEWLINEONELINE,
                       LIBSSH_TEST_NONEWLINEONELINE_STRING);

    /* Match complex combinations */
    torture_write_file(LIBSSH_TESTCONFIG_MATCH_COMPLEX,
                       LIBSSH_TESTCONFIG_MATCH_COMPLEX_STRING);
    torture_write_file(LIBSSH_TESTCONFIG_LOGLEVEL_MISSING,
                       LIBSSH_TESTCONFIG_LOGLEVEL_MISSING_STRING);
    torture_write_file(LIBSSH_TESTCONFIG_JUMP,
                       LIBSSH_TESTCONFIG_JUMP_STRING);

    return 0;
}

static int teardown_config_files(void **state)
{
    (void) state; /* unused */

    unlink(LIBSSH_TESTCONFIG1);
    unlink(LIBSSH_TESTCONFIG2);
    unlink(LIBSSH_TESTCONFIG3);
    unlink(LIBSSH_TESTCONFIG4);
    unlink(LIBSSH_TESTCONFIG5);
    unlink(LIBSSH_TESTCONFIG6);
    unlink(LIBSSH_TESTCONFIG7);
    unlink(LIBSSH_TESTCONFIG8);
    unlink(LIBSSH_TESTCONFIG9);
    unlink(LIBSSH_TESTCONFIG10);
    unlink(LIBSSH_TESTCONFIG11);
    unlink(LIBSSH_TESTCONFIG12);
    unlink(LIBSSH_TESTCONFIG14);
    unlink(LIBSSH_TESTCONFIG15);
    unlink(LIBSSH_TESTCONFIG16);
    unlink(LIBSSH_TESTCONFIG17);
    unlink(LIBSSH_TEST_PUBKEYTYPES);
    unlink(LIBSSH_TEST_PUBKEYALGORITHMS);
    unlink(LIBSSH_TEST_NONEWLINEEND);
    unlink(LIBSSH_TEST_NONEWLINEONELINE);
    unlink(LIBSSH_TESTCONFIG_MATCH_COMPLEX);
    unlink(LIBSSH_TESTCONFIG_LOGLEVEL_MISSING);
    unlink(LIBSSH_TESTCONFIG_JUMP);

    return 0;
}

static int setup(void **state)
{
    ssh_session session = NULL;
    char *wd = NULL;
    int verbosity;

    session = ssh_new();

    verbosity = torture_libssh_verbosity();
    ssh_options_set(session, SSH_OPTIONS_LOG_VERBOSITY, &verbosity);
    wd = torture_get_current_working_dir();
    ssh_options_set(session, SSH_OPTIONS_SSH_DIR, wd);
    free(wd);

    *state = session;

    return 0;
}

static int setup_no_sshdir(void **state)
{
    ssh_session session = NULL;
    int verbosity;

    session = ssh_new();

    verbosity = torture_libssh_verbosity();
    ssh_options_set(session, SSH_OPTIONS_LOG_VERBOSITY, &verbosity);

    *state = session;

    return 0;
}

static int teardown(void **state)
{
    ssh_free(*state);

    return 0;
}

/**
 * @brief tests ssh config parsing with Include directives
 */
static void torture_config_include(void **state,
                                   const char *file, const char *string)
{
    int ret;
    char *v = NULL;
    char *fips_algos = NULL;
    ssh_session session = *state;

    _parse_config(session, file, string, SSH_OK);

    /* Test the variable presence */
    ret = ssh_options_get(session, SSH_OPTIONS_PROXYCOMMAND, &v);
    assert_true(ret == 0);
    assert_non_null(v);

    assert_string_equal(v, PROXYCMD);
    SSH_STRING_FREE_CHAR(v);

    ret = ssh_options_get(session, SSH_OPTIONS_IDENTITY, &v);
    assert_true(ret == 0);
    assert_non_null(v);

    assert_string_equal(v, ID_FILE);
    SSH_STRING_FREE_CHAR(v);

    ret = ssh_options_get(session, SSH_OPTIONS_USER, &v);
    assert_true(ret == 0);
    assert_non_null(v);

    assert_string_equal(v, USERNAME);
    SSH_STRING_FREE_CHAR(v);

    if (ssh_fips_mode()) {
        fips_algos = ssh_keep_fips_algos(SSH_KEX, KEXALGORITHMS);
        assert_non_null(fips_algos);
        assert_string_equal(session->opts.wanted_methods[SSH_KEX], fips_algos);
        SAFE_FREE(fips_algos);
        fips_algos = ssh_keep_fips_algos(SSH_HOSTKEYS, HOSTKEYALGORITHMS);
        assert_non_null(fips_algos);
        assert_string_equal(session->opts.wanted_methods[SSH_HOSTKEYS],
                fips_algos);
        SAFE_FREE(fips_algos);
        fips_algos = ssh_keep_fips_algos(SSH_HOSTKEYS, PUBKEYACCEPTEDTYPES);
        assert_non_null(fips_algos);
        assert_string_equal(session->opts.pubkey_accepted_types, fips_algos);
        SAFE_FREE(fips_algos);
        fips_algos = ssh_keep_fips_algos(SSH_MAC_C_S, MACS);
        assert_non_null(fips_algos);
        assert_string_equal(session->opts.wanted_methods[SSH_MAC_C_S],
                fips_algos);
        SAFE_FREE(fips_algos);
        fips_algos = ssh_keep_fips_algos(SSH_MAC_S_C, MACS);
        assert_non_null(fips_algos);
        assert_string_equal(session->opts.wanted_methods[SSH_MAC_S_C],
                fips_algos);
        SAFE_FREE(fips_algos);
    } else {
        assert_non_null(session->opts.wanted_methods[SSH_KEX]);
        assert_string_equal(session->opts.wanted_methods[SSH_KEX],
                KEXALGORITHMS);
        assert_non_null(session->opts.wanted_methods[SSH_HOSTKEYS]);
        assert_string_equal(session->opts.wanted_methods[SSH_HOSTKEYS],
                HOSTKEYALGORITHMS);
        assert_non_null(session->opts.pubkey_accepted_types);
        assert_string_equal(session->opts.pubkey_accepted_types,
                PUBKEYACCEPTEDTYPES);
        assert_non_null(session->opts.wanted_methods[SSH_MAC_S_C]);
        assert_string_equal(session->opts.wanted_methods[SSH_MAC_C_S], MACS);
        assert_non_null(session->opts.wanted_methods[SSH_MAC_S_C]);
        assert_string_equal(session->opts.wanted_methods[SSH_MAC_S_C], MACS);
    }
}

/**
 * @brief tests ssh_config_parse_file with Include directives from file
 */
static void torture_config_include_file(void **state)
{
    torture_config_include(state, LIBSSH_TESTCONFIG1, NULL);
}

/**
 * @brief tests ssh_config_parse_string with Include directives from string
 */
static void torture_config_include_string(void **state)
{
    torture_config_include(state, NULL, LIBSSH_TESTCONFIG_STRING1);
}

/**
 * @brief tests ssh_config_parse_file with recursive Include directives from file
 */
static void torture_config_include_recursive_file(void **state)
{
    _parse_config(*state, LIBSSH_TEST_RECURSIVE_INCLUDE, NULL, SSH_OK);
}

/**
 * @brief tests ssh_config_parse_string with Include directives from string
 */
static void torture_config_include_recursive_string(void **state)
{
    _parse_config(*state, NULL, LIBSSH_TEST_RECURSIVE_INCLUDE_STRING, SSH_OK);
}

/**
 * @brief tests ssh_config_parse_file with multiple Port settings.
 */
static void torture_config_double_ports_file(void **state)
{
    _parse_config(*state, LIBSSH_TESTCONFIG4, NULL, SSH_OK);
}

/**
 * @brief tests ssh_config_parse_string with multiple Port settings.
 */
static void torture_config_double_ports_string(void **state)
{
    _parse_config(*state, NULL, LIBSSH_TESTCONFIG_STRING4, SSH_OK);
}

static void torture_config_glob(void **state,
                                const char *file, const char *string)
{
#if defined(HAVE_GLOB) && defined(HAVE_GLOB_GL_FLAGS_MEMBER)
    int ret;
    char *v;
    ssh_session session = *state;

    _parse_config(session, file, string, SSH_OK);

    /* Test the variable presence */

    ret = ssh_options_get(session, SSH_OPTIONS_PROXYCOMMAND, &v);
    assert_true(ret == 0);
    assert_non_null(v);

    assert_string_equal(v, PROXYCMD);
    SSH_STRING_FREE_CHAR(v);

    ret = ssh_options_get(session, SSH_OPTIONS_IDENTITY, &v);
    assert_true(ret == 0);
    assert_non_null(v);

    assert_string_equal(v, ID_FILE);
    SSH_STRING_FREE_CHAR(v);
#endif /* HAVE_GLOB && HAVE_GLOB_GL_FLAGS_MEMBER */
}

static void torture_config_glob_file(void **state)
{
    torture_config_glob(state, LIBSSH_TESTCONFIG5, NULL);
}

static void torture_config_glob_string(void **state)
{
    torture_config_glob(state, NULL, LIBSSH_TESTCONFIG_STRING5);
}

/**
 * @brief Verify the new options are passed from configuration
 */
static void torture_config_new(void ** state,
                               const char *file, const char *string)
{
    ssh_session session = *state;

    _parse_config(session, file, string, SSH_OK);

    assert_string_equal(session->opts.knownhosts, USER_KNOWN_HOSTS);
    assert_string_equal(session->opts.global_knownhosts, GLOBAL_KNOWN_HOSTS);
    assert_int_equal(session->opts.timeout, 30);
    assert_string_equal(session->opts.bindaddr, BIND_ADDRESS);
#ifdef WITH_ZLIB
    assert_string_equal(session->opts.wanted_methods[SSH_COMP_C_S],
                        "zlib@openssh.com,none");
    assert_string_equal(session->opts.wanted_methods[SSH_COMP_S_C],
                        "zlib@openssh.com,none");
#else
    assert_string_equal(session->opts.wanted_methods[SSH_COMP_C_S],
                        "none");
    assert_string_equal(session->opts.wanted_methods[SSH_COMP_S_C],
                        "none");
#endif /* WITH_ZLIB */
    assert_int_equal(session->opts.StrictHostKeyChecking, 0);
    assert_int_equal(session->opts.gss_delegate_creds, 1);
    assert_string_equal(session->opts.gss_server_identity, "example.com");
    assert_string_equal(session->opts.gss_client_identity, "home.sweet");

    assert_int_equal(ssh_get_log_level(), SSH_LOG_TRACE);
    assert_int_equal(session->common.log_verbosity, SSH_LOG_TRACE);
}

static void torture_config_new_file(void **state)
{
    torture_config_new(state, LIBSSH_TESTCONFIG7, NULL);
}

static void torture_config_new_string(void **state)
{
    torture_config_new(state, NULL, LIBSSH_TESTCONFIG_STRING7);
}

/**
 * @brief Verify the authentication methods from configuration are effective
 */
static void torture_config_auth_methods(void **state,
                                        const char *file, const char *string)
{
    ssh_session session = *state;

    /* gradually disable all the methods based on different hosts */
    ssh_options_set(session, SSH_OPTIONS_HOST, "nogss");
    _parse_config(session, file, string, SSH_OK);
    assert_false(session->opts.flags & SSH_OPT_FLAG_GSSAPI_AUTH);
    assert_true(session->opts.flags & SSH_OPT_FLAG_KBDINT_AUTH);

    ssh_options_set(session, SSH_OPTIONS_HOST, "nokbd");
    _parse_config(session, file, string, SSH_OK);
    assert_false(session->opts.flags & SSH_OPT_FLAG_KBDINT_AUTH);

    ssh_options_set(session, SSH_OPTIONS_HOST, "nopass");
    _parse_config(session, file, string, SSH_OK);
    assert_false(session->opts.flags & SSH_OPT_FLAG_PASSWORD_AUTH);

    ssh_options_set(session, SSH_OPTIONS_HOST, "nopubkey");
    _parse_config(session, file, string, SSH_OK);
    assert_false(session->opts.flags & SSH_OPT_FLAG_PUBKEY_AUTH);

    /* no method should be left enabled */
    assert_int_equal(session->opts.flags, 0);

    /* gradually enable them again */
    torture_reset_config(session);
    ssh_options_set(session, SSH_OPTIONS_HOST, "gss");
    _parse_config(session, file, string, SSH_OK);
    assert_true(session->opts.flags & SSH_OPT_FLAG_GSSAPI_AUTH);
    assert_false(session->opts.flags & SSH_OPT_FLAG_KBDINT_AUTH);

    ssh_options_set(session, SSH_OPTIONS_HOST, "kbd");
    _parse_config(session, file, string, SSH_OK);
    assert_true(session->opts.flags & SSH_OPT_FLAG_KBDINT_AUTH);

    ssh_options_set(session, SSH_OPTIONS_HOST, "pass");
    _parse_config(session, file, string, SSH_OK);
    assert_true(session->opts.flags & SSH_OPT_FLAG_PASSWORD_AUTH);

    ssh_options_set(session, SSH_OPTIONS_HOST, "pubkey");
    _parse_config(session, file, string, SSH_OK);
    assert_true(session->opts.flags & SSH_OPT_FLAG_PUBKEY_AUTH);
}

/**
 * @brief Verify the authentication methods from configuration file
 * are effective
 */
static void torture_config_auth_methods_file(void **state)
{
    torture_config_auth_methods(state, LIBSSH_TESTCONFIG8, NULL);
}

/**
 * @brief Verify the authentication methods from configuration string
 * are effective
 */
static void torture_config_auth_methods_string(void **state)
{
    torture_config_auth_methods(state, NULL, LIBSSH_TESTCONFIG_STRING8);
}

/**
 * @brief Helper for checking hostname, username and port of ssh_jump_info_struct
 */
static void
helper_proxy_jump_check(struct ssh_iterator *jump,
                        const char *hostname,
                        const char *username,
                        const char *port)
{
    struct ssh_jump_info_struct *jis =
        ssh_iterator_value(struct ssh_jump_info_struct *, jump);

    assert_string_equal(jis->hostname, hostname);

    if (username != NULL) {
        assert_string_equal(jis->username, username);
    } else {
        assert_null(jis->username);
    }

    if (port != NULL) {
        int iport = strtol(port, NULL, 10);
        assert_int_equal(jis->port, iport);
    } else {
        assert_int_equal(jis->port, 22);
    }
}

/**
 * @brief Verify the configuration parser does not choke on unknown
 * or unsupported configuration options
 */
static void torture_config_unknown(void **state,
                                   const char *file, const char *string)
{
    ssh_session session = *state;
    int ret = 0;

    /* test corner cases */
    /* Without libssh proxy jump */
    torture_setenv("OPENSSH_PROXYJUMP", "1");
    _parse_config(session, file, string, SSH_OK);
    assert_string_equal(session->opts.ProxyCommand,
                        "ssh -W '[%h]:%p' many-spaces.com");
    assert_string_equal(session->opts.host, "equal.sign");

    ret = ssh_config_parse_file(session, "/etc/ssh/ssh_config");
    assert_true(ret == 0);
    ret = ssh_config_parse_file(session, GLOBAL_CLIENT_CONFIG);
    assert_true(ret == 0);
    torture_unsetenv("OPENSSH_PROXYJUMP");
}

/**
 * @brief Verify the configuration parser does not choke on unknown
 * or unsupported configuration options in configuration file
 */
static void torture_config_unknown_file(void **state)
{
    torture_config_unknown(state, LIBSSH_TESTCONFIG9, NULL);
}

/**
 * @brief Verify the configuration parser does not choke on unknown
 * or unsupported configuration options in configuration string
 */
static void torture_config_unknown_string(void **state)
{
    torture_config_unknown(state, NULL, LIBSSH_TESTCONFIG_STRING9);
}

/**
 * @brief Verify the configuration parser accepts Match keyword with
 * full OpenSSH syntax.
 */
static void torture_config_match(void **state,
                                 const char *file, const char *string)
{
    ssh_session session = *state;
    char *localuser = NULL;
    const char *config;
    char config_string[1024];

    /* Without any settings we should get all-matched.com hostname */
    torture_reset_config(session);
    ssh_options_set(session, SSH_OPTIONS_HOST, "unmatched");
    _parse_config(session, file, string, SSH_OK);
    assert_string_equal(session->opts.host, "all-matched.com");

    /* Hostname example does simple hostname matching */
    torture_reset_config(session);
    ssh_options_set(session, SSH_OPTIONS_HOST, "example");
    _parse_config(session, file, string, SSH_OK);
    assert_string_equal(session->opts.host, "example.com");

    /* We can match also both hosts from a comma separated list */
    torture_reset_config(session);
    ssh_options_set(session, SSH_OPTIONS_HOST, "example1");
    _parse_config(session, file, string, SSH_OK);
    assert_string_equal(session->opts.host, "exampleN");

    torture_reset_config(session);
    ssh_options_set(session, SSH_OPTIONS_HOST, "example2");
    _parse_config(session, file, string, SSH_OK);
    assert_string_equal(session->opts.host, "exampleN");

    /* We can match by user */
    torture_reset_config(session);
    ssh_options_set(session, SSH_OPTIONS_USER, "guest");
    _parse_config(session, file, string, SSH_OK);
    assert_string_equal(session->opts.host, "guest.com");

    /* We can combine two options on a single line to match both of them */
    torture_reset_config(session);
    ssh_options_set(session, SSH_OPTIONS_USER, "tester");
    ssh_options_set(session, SSH_OPTIONS_HOST, "testhost");
    _parse_config(session, file, string, SSH_OK);
    assert_string_equal(session->opts.host, "testhost.com");

    /* We can also negate conditions */
    torture_reset_config(session);
    ssh_options_set(session, SSH_OPTIONS_USER, "not-tester");
    ssh_options_set(session, SSH_OPTIONS_HOST, "testhost");
    _parse_config(session, file, string, SSH_OK);
    assert_string_equal(session->opts.host, "nonuser-testhost.com");

    /* In this part, we try various other config files and strings. */

    /* Match final is not completely supported, but should do quite much the
     * same as "match all". The trailing "all" is not mandatory. */
    config = "Match final all\n"
             "\tHostName final-all.com\n";
    if (file != NULL) {
        torture_write_file(file, config);
    } else {
        string = config;
    }
    torture_reset_config(session);
    _parse_config(session, file, string, SSH_OK);
    assert_string_equal(session->opts.host, "final-all.com");

    config = "Match final\n"
             "\tHostName final.com\n";
    if (file != NULL) {
        torture_write_file(file, config);
    } else {
        string = config;
    }
    torture_reset_config(session);
    _parse_config(session, file, string, SSH_OK);
    assert_string_equal(session->opts.host, "final.com");

    /* Match canonical is not completely supported, but should do quite
     * much the same as "match all". The trailing "all" is not mandatory. */
    config = "Match canonical all\n"
             "\tHostName canonical-all.com\n";
    if (file != NULL) {
        torture_write_file(file, config);
    } else {
        string = config;
    }
    torture_reset_config(session);
    _parse_config(session, file, string, SSH_OK);
    assert_string_equal(session->opts.host, "canonical-all.com");

    config = "Match canonical all\n"
             "\tHostName canonical.com\n";
    if (file != NULL) {
        torture_write_file(file, config);
    } else {
        string = config;
    }
    torture_reset_config(session);
    _parse_config(session, file, string, SSH_OK);
    assert_string_equal(session->opts.host, "canonical.com");

    localuser = ssh_get_local_username();
    assert_non_null(localuser);
    snprintf(config_string, sizeof(config_string),
             "Match localuser %s\n"
             "\tHostName otherhost\n",
             localuser);
    config = config_string;
    free(localuser);
    if (file != NULL) {
        torture_write_file(file, config);
    } else {
        string = config;
    }
    torture_reset_config(session);
    _parse_config(session, file, string, SSH_OK);
    assert_string_equal(session->opts.host, "otherhost");

    config = "Match exec true\n"
             "\tHostName execed-true.com\n";
    if (file != NULL) {
        torture_write_file(file, config);
    } else {
        string = config;
    }
    torture_reset_config(session);
    _parse_config(session, file, string, SSH_OK);
#ifndef WITH_EXEC
    /* The match exec is not supported on windows at this moment */
    assert_string_equal(session->opts.host, "otherhost");
#else
    assert_string_equal(session->opts.host, "execed-true.com");
#endif

    config = "Match !exec false\n"
             "\tHostName execed-false.com\n";
    if (file != NULL) {
        torture_write_file(file, config);
    } else {
        string = config;
    }
    torture_reset_config(session);
    _parse_config(session, file, string, SSH_OK);
#ifndef WITH_EXEC
    /* The match exec is not supported on windows at this moment */
    assert_string_equal(session->opts.host, "otherhost");
#else
    assert_string_equal(session->opts.host, "execed-false.com");
#endif

    config = "Match exec \"test 1 -eq 1\"\n"
             "\tHostName execed-arguments.com\n";
    if (file != NULL) {
        torture_write_file(file, config);
    } else {
        string = config;
    }
    torture_reset_config(session);
    _parse_config(session, file, string, SSH_OK);
#ifndef WITH_EXEC
    /* The match exec is not supported on windows at this moment */
    assert_string_equal(session->opts.host, "otherhost");
#else
    assert_string_equal(session->opts.host, "execed-arguments.com");
#endif

    /* Try to create some invalid configurations */
    /* Missing argument to Match*/
    config = "Match\n"
             "\tHost missing.com\n";
    if (file != NULL) {
        torture_write_file(file, config);
    } else {
        string = config;
    }
    torture_reset_config(session);
    ssh_options_set(session, SSH_OPTIONS_HOST, "unmatched");
    _parse_config(session, file, string, SSH_OK);
    assert_string_equal(session->opts.host, "unmatched");

    /* Missing argument to unsupported option originalhost */
    config = "Match originalhost\n"
             "\tHost originalhost.com\n";
    if (file != NULL) {
        torture_write_file(file, config);
    } else {
        string = config;
    }
    torture_reset_config(session);
    _parse_config(session, file, string, SSH_ERROR);

    /* Missing argument to option localuser */
    config = "Match localuser\n"
             "\tUser localuser2\n";
    if (file != NULL) {
        torture_write_file(file, config);
    } else {
        string = config;
    }
    torture_reset_config(session);
    _parse_config(session, file, string, SSH_ERROR);

    /* Missing argument to option user */
    config = "Match user\n"
             "\tUser user2\n";
    if (file != NULL) {
        torture_write_file(file, config);
    } else {
        string = config;
    }
    torture_reset_config(session);
    _parse_config(session, file, string, SSH_ERROR);

    /* Missing argument to option host */
    config = "Match host\n"
             "\tUser host2\n";
    if (file != NULL) {
        torture_write_file(file, config);
    } else {
        string = config;
    }
    torture_reset_config(session);
    _parse_config(session, file, string, SSH_ERROR);

    /* Missing argument to option exec */
    config = "Match exec\n"
             "\tUser exec\n";
    if (file != NULL) {
        torture_write_file(file, config);
    } else {
        string = config;
    }
    torture_reset_config(session);
    _parse_config(session, file, string, SSH_ERROR);

    /* Unknown argument to Match keyword */
    config = "Match tagged tag_name\n"
             "\tHostName never-matched.com\n"
             "Match all\n"
             "\tHostName config-host.com\n";
    if (file != NULL) {
        torture_write_file(file, config);
    } else {
        string = config;
    }
    torture_reset_config(session);
    ssh_options_set(session, SSH_OPTIONS_HOST, "example.com");
    _parse_config(session, file, string, SSH_OK);
    assert_string_equal(session->opts.host, "config-host.com");

    /* Missing argument to Match keyword */
    config = "Match\n"
             "\tHostName never-matched.com\n"
             "Match all\n"
             "\tHostName config-host.com\n";
    if (file != NULL) {
        torture_write_file(file, config);
    } else {
        string = config;
    }
    torture_reset_config(session);
    ssh_options_set(session, SSH_OPTIONS_HOST, "example.com");
    _parse_config(session, file, string, SSH_OK);
    assert_string_equal(session->opts.host, "config-host.com");
}

/**
 * @brief Verify the configuration parser accepts Match keyword with
 * full OpenSSH syntax through configuration file.
 */
static void torture_config_match_file(void **state)
{
    torture_config_match(state, LIBSSH_TESTCONFIG10, NULL);
}

/**
 * @brief Verify the configuration parser accepts Match keyword with
 * full OpenSSH syntax through configuration string.
 */
static void torture_config_match_string(void **state)
{
    torture_config_match(state, NULL, LIBSSH_TESTCONFIG_STRING10);
}

/**
 * @brief Verify we can parse ProxyJump configuration option
 */
static void torture_config_proxyjump(void **state,
                                     const char *file, const char *string)
{
    ssh_session session = *state;

    const char *config;


    /* Tests for libssh based proxyjump */
    /* Simplest version with just a hostname */
    torture_reset_config(session);
    ssh_options_set(session, SSH_OPTIONS_HOST, "simple");
    _parse_config(session, file, string, SSH_OK);
    helper_proxy_jump_check(session->opts.proxy_jumps->root,
                            "jumpbox",
                            NULL,
                            NULL);

    /* With username */
    torture_reset_config(session);
    ssh_options_set(session, SSH_OPTIONS_HOST, "user");
    _parse_config(session, file, string, SSH_OK);
    helper_proxy_jump_check(session->opts.proxy_jumps->root,
                            "jumpbox",
                            "user",
                            NULL);

    /* With port */
    torture_reset_config(session);
    ssh_options_set(session, SSH_OPTIONS_HOST, "port");
    _parse_config(session, file, string, SSH_OK);
    helper_proxy_jump_check(session->opts.proxy_jumps->root,
                            "jumpbox",
                            NULL,
                            "2222");

    /* Two step jump */
    torture_reset_config(session);
    ssh_options_set(session, SSH_OPTIONS_HOST, "two-step");
    _parse_config(session, file, string, SSH_OK);
    helper_proxy_jump_check(session->opts.proxy_jumps->root,
                            "second",
                            "u2",
                            "33");
    helper_proxy_jump_check(session->opts.proxy_jumps->root->next,
                            "first",
                            "u1",
                            "222");

    /* none */
    torture_reset_config(session);
    ssh_options_set(session, SSH_OPTIONS_HOST, "none");
    _parse_config(session, file, string, SSH_OK);
    assert_int_equal(ssh_list_count(session->opts.proxy_jumps), 0);

    /* If also ProxyCommand is specified, the first is applied */
    torture_reset_config(session);
    ssh_options_set(session, SSH_OPTIONS_HOST, "only-command");
    _parse_config(session, file, string, SSH_OK);
    assert_string_equal(session->opts.ProxyCommand, PROXYCMD);
    assert_int_equal(ssh_list_count(session->opts.proxy_jumps), 0);

    /* If also ProxyCommand is specified, the first is applied */
    torture_reset_config(session);
    SAFE_FREE(session->opts.ProxyCommand);
    ssh_options_set(session, SSH_OPTIONS_HOST, "only-jump");
    _parse_config(session, file, string, SSH_OK);
    assert_null(session->opts.ProxyCommand);
    helper_proxy_jump_check(session->opts.proxy_jumps->root,
                            "jumpbox",
                            NULL,
                            NULL);

    /* IPv6 address */
    torture_reset_config(session);
    ssh_options_set(session, SSH_OPTIONS_HOST, "ipv6");
    _parse_config(session, file, string, SSH_OK);
    helper_proxy_jump_check(session->opts.proxy_jumps->root,
                            "2620:52:0::fed",
                            NULL,
                            NULL);

    torture_reset_config(session);

    /* Tests for proxycommand based proxyjump */
    torture_setenv("OPENSSH_PROXYJUMP", "1");

    /* Simplest version with just a hostname */
    torture_reset_config(session);
    ssh_options_set(session, SSH_OPTIONS_HOST, "simple");
    _parse_config(session, file, string, SSH_OK);
    assert_string_equal(session->opts.ProxyCommand, "ssh -W '[%h]:%p' jumpbox");

    /* With username */
    torture_reset_config(session);
    ssh_options_set(session, SSH_OPTIONS_HOST, "user");
    _parse_config(session, file, string, SSH_OK);
    assert_string_equal(session->opts.ProxyCommand,
                        "ssh -l user -W '[%h]:%p' jumpbox");

    /* With port */
    torture_reset_config(session);
    ssh_options_set(session, SSH_OPTIONS_HOST, "port");
    _parse_config(session, file, string, SSH_OK);
    assert_string_equal(session->opts.ProxyCommand,
                        "ssh -p 2222 -W '[%h]:%p' jumpbox");

    /* Two step jump */
    torture_reset_config(session);
    ssh_options_set(session, SSH_OPTIONS_HOST, "two-step");
    _parse_config(session, file, string, SSH_OK);
    assert_string_equal(session->opts.ProxyCommand,
                        "ssh -l u1 -p 222 -J u2@second:33 -W '[%h]:%p' first");

    /* none */
    torture_reset_config(session);
    ssh_options_set(session, SSH_OPTIONS_HOST, "none");
    _parse_config(session, file, string, SSH_OK);
    assert_true(session->opts.ProxyCommand == NULL);

    /* If also ProxyCommand is specified, the first is applied */
    torture_reset_config(session);
    ssh_options_set(session, SSH_OPTIONS_HOST, "only-command");
    _parse_config(session, file, string, SSH_OK);
    assert_string_equal(session->opts.ProxyCommand, PROXYCMD);

    /* If also ProxyCommand is specified, the first is applied */
    torture_reset_config(session);
    ssh_options_set(session, SSH_OPTIONS_HOST, "only-jump");
    _parse_config(session, file, string, SSH_OK);
    assert_string_equal(session->opts.ProxyCommand,
                        "ssh -W '[%h]:%p' jumpbox");

    /* IPv6 address */
    torture_reset_config(session);
    ssh_options_set(session, SSH_OPTIONS_HOST, "ipv6");
    _parse_config(session, file, string, SSH_OK);
    assert_string_equal(session->opts.ProxyCommand,
                        "ssh -W '[%h]:%p' 2620:52:0::fed");


    /* Multiple @ is allowed in second jump */
    config = "Host allowed-hostname\n"
             "\tProxyJump localhost,user@principal.com@jumpbox:22\n";
    if (file != NULL) {
        torture_write_file(file, config);
    } else {
        string = config;
    }
    torture_reset_config(session);
    ssh_options_set(session, SSH_OPTIONS_HOST, "allowed-hostname");
    _parse_config(session, file, string, SSH_OK);
    assert_string_equal(session->opts.ProxyCommand,
                        "ssh -J user@principal.com@jumpbox:22 -W '[%h]:%p' localhost");

    /* Multiple @ is allowed */
    config = "Host allowed-hostname\n"
             "\tProxyJump user@principal.com@jumpbox:22\n";
    if (file != NULL) {
        torture_write_file(file, config);
    } else {
        string = config;
    }
    torture_reset_config(session);
    ssh_options_set(session, SSH_OPTIONS_HOST, "allowed-hostname");
    _parse_config(session, file, string, SSH_OK);
    assert_string_equal(session->opts.ProxyCommand,
                        "ssh -l user@principal.com -p 22 -W '[%h]:%p' jumpbox");
    torture_unsetenv("OPENSSH_PROXYJUMP");

    /* Tests for libssh based proxyjump */
    /* Multiple @ is allowed in second jump */
    config = "Host allowed-hostname\n"
             "\tProxyJump localhost,user@principal.com@jumpbox:22\n";
    if (file != NULL) {
        torture_write_file(file, config);
    } else {
        string = config;
    }
    torture_reset_config(session);
    ssh_options_set(session, SSH_OPTIONS_HOST, "allowed-hostname");
    _parse_config(session, file, string, SSH_OK);
    helper_proxy_jump_check(session->opts.proxy_jumps->root,
                            "jumpbox",
                            "user@principal.com",
                            "22");

    /* Multiple @ is allowed */
    config = "Host allowed-hostname\n"
             "\tProxyJump user@principal.com@jumpbox:22\n";
    if (file != NULL) {
        torture_write_file(file, config);
    } else {
        string = config;
    }
    torture_reset_config(session);
    ssh_options_set(session, SSH_OPTIONS_HOST, "allowed-hostname");
    _parse_config(session, file, string, SSH_OK);
    helper_proxy_jump_check(session->opts.proxy_jumps->root,
                            "jumpbox",
                            "user@principal.com",
                            "22");
    torture_reset_config(session);

    /* In this part, we try various other config files and strings. */
    torture_setenv("OPENSSH_PROXYJUMP", "1");

    /* Try to create some invalid configurations */
    /* Non-numeric port */
    config = "Host bad-port\n"
             "\tProxyJump jumpbox:22bad22\n";
    if (file != NULL) {
        torture_write_file(file, config);
    } else {
        string = config;
    }
    torture_reset_config(session);
    ssh_options_set(session, SSH_OPTIONS_HOST, "bad-port");
    _parse_config(session, file, string, SSH_ERROR);

    /* Braces mismatch in hostname */
    config = "Host mismatch\n"
             "\tProxyJump [::1\n";
    if (file != NULL) {
        torture_write_file(file, config);
    } else {
        string = config;
    }
    torture_reset_config(session);
    ssh_options_set(session, SSH_OPTIONS_HOST, "mismatch");
    _parse_config(session, file, string, SSH_ERROR);

    /* Bad host-port separator */
    config = "Host beef\n"
             "\tProxyJump [dead::beef]::22\n";
    if (file != NULL) {
        torture_write_file(file, config);
    } else {
        string = config;
    }
    torture_reset_config(session);
    ssh_options_set(session, SSH_OPTIONS_HOST, "beef");
    _parse_config(session, file, string, SSH_ERROR);

    /* Missing hostname */
    config = "Host no-host\n"
             "\tProxyJump user@:22\n";
    if (file != NULL) {
        torture_write_file(file, config);
    } else {
        string = config;
    }
    torture_reset_config(session);
    ssh_options_set(session, SSH_OPTIONS_HOST, "no-host");
    _parse_config(session, file, string, SSH_ERROR);

    /* Missing user */
    config = "Host no-user\n"
             "\tProxyJump @host:22\n";
    if (file != NULL) {
        torture_write_file(file, config);
    } else {
        string = config;
    }
    torture_reset_config(session);
    ssh_options_set(session, SSH_OPTIONS_HOST, "no-user");
    _parse_config(session, file, string, SSH_ERROR);

    /* Missing port */
    config = "Host no-port\n"
             "\tProxyJump host:\n";
    if (file != NULL) {
        torture_write_file(file, config);
    } else {
        string = config;
    }
    torture_reset_config(session);
    ssh_options_set(session, SSH_OPTIONS_HOST, "no-port");
    _parse_config(session, file, string, SSH_ERROR);

    /* Non-numeric port in second jump */
    config = "Host bad-port-2\n"
             "\tProxyJump localhost,jumpbox:22bad22\n";
    if (file != NULL) {
        torture_write_file(file, config);
    } else {
        string = config;
    }
    torture_reset_config(session);
    ssh_options_set(session, SSH_OPTIONS_HOST, "bad-port-2");
    _parse_config(session, file, string, SSH_ERROR);

    /* Braces mismatch in second jump */
    config = "Host mismatch\n"
             "\tProxyJump localhost,[::1:20\n";
    if (file != NULL) {
        torture_write_file(file, config);
    } else {
        string = config;
    }
    torture_reset_config(session);
    ssh_options_set(session, SSH_OPTIONS_HOST, "mismatch");
    _parse_config(session, file, string, SSH_ERROR);

    /* Bad host-port separator in second jump */
    config = "Host beef\n"
             "\tProxyJump localhost,[dead::beef]::22\n";
    if (file != NULL) {
        torture_write_file(file, config);
    } else {
        string = config;
    }
    torture_reset_config(session);
    ssh_options_set(session, SSH_OPTIONS_HOST, "beef");
    _parse_config(session, file, string, SSH_ERROR);

    /* Missing hostname in second jump */
    config = "Host no-host\n"
             "\tProxyJump localhost,user@:22\n";
    if (file != NULL) {
        torture_write_file(file, config);
    } else {
        string = config;
    }
    torture_reset_config(session);
    ssh_options_set(session, SSH_OPTIONS_HOST, "no-host");
    _parse_config(session, file, string, SSH_ERROR);

    /* Missing user in second jump */
    config = "Host no-user\n"
             "\tProxyJump localhost,@host:22\n";
    if (file != NULL) {
        torture_write_file(file, config);
    } else {
        string = config;
    }
    torture_reset_config(session);
    ssh_options_set(session, SSH_OPTIONS_HOST, "no-user");
    _parse_config(session, file, string, SSH_ERROR);

    /* Missing port in second jump */
    config = "Host no-port\n"
             "\tProxyJump localhost,host:\n";
    if (file != NULL) {
        torture_write_file(file, config);
    } else {
        string = config;
    }
    torture_reset_config(session);
    ssh_options_set(session, SSH_OPTIONS_HOST, "no-port");
    _parse_config(session, file, string, SSH_ERROR);

    torture_unsetenv("OPENSSH_PROXYJUMP");
}

/**
 * @brief Verify we can parse ProxyJump configuration option from file
 */
static void torture_config_proxyjump_file(void **state)
{
    torture_config_proxyjump(state, LIBSSH_TESTCONFIG11, NULL);
}

/**
 * @brief Verify we can parse ProxyJump configuration option from string
 */
static void torture_config_proxyjump_string(void **state)
{
    torture_config_proxyjump(state, NULL, LIBSSH_TESTCONFIG_STRING11);
}

/**
 * @brief Verify we can parse ControlPath configuration option
 */
static void torture_config_control_path(void **state,
                                        const char *file, const char *string)
{
    ssh_session session = *state;

    torture_reset_config(session);
    ssh_options_set(session, SSH_OPTIONS_HOST, "simple");
    _parse_config(session, file, string, SSH_OK);
    assert_null(session->opts.control_path);

    torture_reset_config(session);
    ssh_options_set(session, SSH_OPTIONS_HOST, "none");
    _parse_config(session, file, string, SSH_OK);
    assert_null(session->opts.control_path);
}

/**
 * @brief Verify we can parse ControlPath configuration option from string
 */
static void torture_config_control_path_string(void **state)
{
    torture_config_control_path(state, NULL, LIBSSH_TESTCONFIG_STRING17);
}

/**
 * @brief Verify we can parse ControlPath configuration option from file
 */
static void torture_config_control_path_file(void **state)
{
    torture_config_control_path(state, LIBSSH_TESTCONFIG17, NULL);
}

/**
 * @brief Verify we can parse ControlMaster configuration option
 */
static void torture_config_control_master(void **state,
                                          const char *file, const char *string)
{
    ssh_session session = *state;

    torture_reset_config(session);
    ssh_options_set(session, SSH_OPTIONS_HOST, "simple");
    _parse_config(session, file, string, SSH_OK);
    assert_int_equal(session->opts.control_master, SSH_CONTROL_MASTER_NO);

    torture_reset_config(session);
    ssh_options_set(session, SSH_OPTIONS_HOST, "none");
    _parse_config(session, file, string, SSH_OK);
    assert_int_equal(session->opts.control_master, SSH_CONTROL_MASTER_NO);
}

/**
 * @brief Verify we can parse ControlMaster configuration option from string
 */
static void torture_config_control_master_string(void **state)
{
    torture_config_control_master(state, NULL, LIBSSH_TESTCONFIG_STRING17);
}

/**
 * @brief Verify we can parse ControlMaster configuration option from file
 */
static void torture_config_control_master_file(void **state)
{
    torture_config_control_master(state, LIBSSH_TESTCONFIG17, NULL);
}

/**
 * @brief Verify the configuration parser handles all the possible
 * versions of RekeyLimit configuration option.
 */
static void torture_config_rekey(void **state,
                                 const char *file, const char *string)
{
    ssh_session session = *state;

    /* Default values */
    ssh_options_set(session, SSH_OPTIONS_HOST, "default");
    _parse_config(session, file, string, SSH_OK);
    assert_int_equal(session->opts.rekey_data, 0);
    assert_int_equal(session->opts.rekey_time, 0);

    /* 42 GB */
    torture_reset_config(session);
    ssh_options_set(session, SSH_OPTIONS_HOST, "data1");
    _parse_config(session, file, string, SSH_OK);
    assert_int_equal(session->opts.rekey_data,
            (uint64_t) 42 * 1024 * 1024 * 1024);
    assert_int_equal(session->opts.rekey_time, 0);

    /* 41 MB */
    torture_reset_config(session);
    ssh_options_set(session, SSH_OPTIONS_HOST, "data2");
    _parse_config(session, file, string, SSH_OK);
    assert_int_equal(session->opts.rekey_data, 31 * 1024 * 1024);
    assert_int_equal(session->opts.rekey_time, 0);

    /* 521 KB */
    torture_reset_config(session);
    ssh_options_set(session, SSH_OPTIONS_HOST, "data3");
    _parse_config(session, file, string, SSH_OK);
    assert_int_equal(session->opts.rekey_data, 521 * 1024);
    assert_int_equal(session->opts.rekey_time, 0);

    /* default 3D */
    torture_reset_config(session);
    ssh_options_set(session, SSH_OPTIONS_HOST, "time1");
    _parse_config(session, file, string, SSH_OK);
    assert_int_equal(session->opts.rekey_data, 0);
    assert_int_equal(session->opts.rekey_time, 3 * 24 * 60 * 60 * 1000);

    /* default 2h */
    torture_reset_config(session);
    ssh_options_set(session, SSH_OPTIONS_HOST, "time2");
    _parse_config(session, file, string, SSH_OK);
    assert_int_equal(session->opts.rekey_data, 0);
    assert_int_equal(session->opts.rekey_time, 2 * 60 * 60 * 1000);

    /* default 160m */
    torture_reset_config(session);
    ssh_options_set(session, SSH_OPTIONS_HOST, "time3");
    _parse_config(session, file, string, SSH_OK);
    assert_int_equal(session->opts.rekey_data, 0);
    assert_int_equal(session->opts.rekey_time, 160 * 60 * 1000);

    /* default 9600 [s] */
    torture_reset_config(session);
    ssh_options_set(session, SSH_OPTIONS_HOST, "time4");
    _parse_config(session, file, string, SSH_OK);
    assert_int_equal(session->opts.rekey_data, 0);
    assert_int_equal(session->opts.rekey_time, 9600 * 1000);

}

/**
 * @brief Verify the configuration parser handles all the possible
 * versions of RekeyLimit configuration option in file
 */
static void torture_config_rekey_file(void **state)
{
    torture_config_rekey(state, LIBSSH_TESTCONFIG12, NULL);
}

/**
 * @brief Verify the configuration parser handles all the possible
 * versions of RekeyLimit configuration option in string
 */
static void torture_config_rekey_string(void **state)
{
    torture_config_rekey(state, NULL, LIBSSH_TESTCONFIG_STRING12);
}

/**
 * @brief Remove substring from a string
 *
 * @param occurrence 0 means "remove the first occurrence"
 *                   1 means "remove the second occurrence" and so on
 */
static void helper_remove_substring(char *s, const char *subs, int occurrence) {
    char *p;
    /* remove the substring from the defaults */
    p = strstr(s, subs);
    assert_non_null(p);
    /* look for second occurrence */
    for (int i = 0; i < occurrence; i++) {
        p = strstr(p + 1, subs);
        assert_non_null(p);
    }
    memmove(p, p + strlen(subs), strlen(p + strlen(subs)) + 1);
}

/**
 * @brief test that openssh style '+' feature works
 */
static void torture_config_plus(void **state,
                                const char *file, const char *string)
{
    ssh_session session = *state;
    const char *def_hostkeys = ssh_kex_get_default_methods(SSH_HOSTKEYS);
    const char *fips_hostkeys = ssh_kex_get_fips_methods(SSH_HOSTKEYS);
    const char *def_ciphers = ssh_kex_get_default_methods(SSH_CRYPT_C_S);
    const char *fips_ciphers = ssh_kex_get_fips_methods(SSH_CRYPT_C_S);
    const char *def_kex = ssh_kex_get_default_methods(SSH_KEX);
    const char *fips_kex = ssh_kex_get_fips_methods(SSH_KEX);
    const char *def_mac = ssh_kex_get_default_methods(SSH_MAC_C_S);
    const char *fips_mac = ssh_kex_get_fips_methods(SSH_MAC_C_S);
    const char *hostkeys_added = ",ssh-rsa";
    const char *ciphers_added = ",aes128-cbc,aes256-cbc";
    const char *kex_added = ",diffie-hellman-group14-sha1,diffie-hellman-group1-sha1";
    const char *mac_added = ",hmac-sha1,hmac-sha1-etm@openssh.com";
    char *awaited = NULL;
    int rc;

    _parse_config(session, file, string, SSH_OK);

    /* check hostkeys */
    if (ssh_fips_mode()) {
        /* ssh-rsa is disabled in fips */
        assert_string_equal(session->opts.wanted_methods[SSH_HOSTKEYS], fips_hostkeys);
    } else {
        awaited = calloc(strlen(def_hostkeys) + strlen(hostkeys_added) + 1, 1);
        rc = snprintf(awaited, strlen(def_hostkeys) + strlen(hostkeys_added) + 1,
                      "%s%s", def_hostkeys, hostkeys_added);
        assert_int_equal(rc, strlen(def_hostkeys) + strlen(hostkeys_added));

        assert_string_equal(session->opts.wanted_methods[SSH_HOSTKEYS], awaited);
        free(awaited);
    }

    /* check ciphers */
    if (ssh_fips_mode()) {
        /* already all supported is in the list */
        assert_string_equal(session->opts.wanted_methods[SSH_CRYPT_C_S], fips_ciphers);
    } else {
        awaited = calloc(strlen(def_ciphers) + strlen(ciphers_added) + 1, 1);
        rc = snprintf(awaited, strlen(def_ciphers) + strlen(ciphers_added) + 1,
                      "%s%s", def_ciphers, ciphers_added);
        assert_int_equal(rc, strlen(def_ciphers) + strlen(ciphers_added));
        assert_string_equal(session->opts.wanted_methods[SSH_CRYPT_C_S], awaited);
        free(awaited);
    }

    /* check kex */
    if (ssh_fips_mode()) {
        /* sha1 is disabled in fips */
        assert_string_equal(session->opts.wanted_methods[SSH_KEX], fips_kex);
    } else {
        awaited = calloc(strlen(def_kex) + strlen(kex_added) + 1, 1);
        rc = snprintf(awaited, strlen(def_kex) + strlen(kex_added) + 1,
                      "%s%s", def_kex, kex_added);
        assert_int_equal(rc, strlen(def_kex) + strlen(kex_added));
        assert_string_equal(session->opts.wanted_methods[SSH_KEX], awaited);
        free(awaited);
    }

    /* check mac */
    if (ssh_fips_mode()) {
        /* the added algos are already in the fips_methods */
        assert_string_equal(session->opts.wanted_methods[SSH_MAC_C_S], fips_mac);
    } else {
        awaited = calloc(strlen(def_mac) + strlen(mac_added) + 1, 1);
        rc = snprintf(awaited, strlen(def_mac) + strlen(mac_added) + 1,
                      "%s%s", def_mac, mac_added);
        assert_int_equal(rc, strlen(def_mac) + strlen(mac_added));
        assert_string_equal(session->opts.wanted_methods[SSH_MAC_C_S], awaited);
        free(awaited);
    }
}

/**
 * @brief test that openssh style '+' feature works from file
 */
static void torture_config_plus_file(void **state)
{
    torture_config_plus(state, LIBSSH_TESTCONFIG14, NULL);
}

/**
 * @brief test that openssh style '+' feature works from string
 */
static void torture_config_plus_string(void **state)
{
    torture_config_plus(state, NULL, LIBSSH_TESTCONFIG_STRING14);
}

/**
 * @brief test that openssh style '-' feature works from string
 */
static void torture_config_minus(void **state,
                                 const char *file, const char *string)
{
    ssh_session session = *state;
    const char *def_hostkeys = ssh_kex_get_default_methods(SSH_HOSTKEYS);
    const char *fips_hostkeys = ssh_kex_get_fips_methods(SSH_HOSTKEYS);
    const char *def_ciphers = ssh_kex_get_default_methods(SSH_CRYPT_C_S);
    const char *fips_ciphers = ssh_kex_get_fips_methods(SSH_CRYPT_C_S);
    const char *def_kex = ssh_kex_get_default_methods(SSH_KEX);
    const char *fips_kex = ssh_kex_get_fips_methods(SSH_KEX);
    const char *def_mac = ssh_kex_get_default_methods(SSH_MAC_C_S);
    const char *fips_mac = ssh_kex_get_fips_methods(SSH_MAC_C_S);
    const char *hostkeys_removed = ",rsa-sha2-512,rsa-sha2-256";
    const char *ciphers_removed = ",aes256-ctr";
    const char *kex_removed = ",diffie-hellman-group18-sha512,diffie-hellman-group16-sha512";
    const char *fips_kex_removed = ",diffie-hellman-group16-sha512,diffie-hellman-group18-sha512";
    const char *mac_removed = "hmac-sha2-256-etm@openssh.com,";
    char *awaited = NULL;
    int rc;

    _parse_config(session, file, string, SSH_OK);

    /* check hostkeys */
    if (ssh_fips_mode()) {
        awaited = calloc(strlen(fips_hostkeys) + 1, 1);
        rc = snprintf(awaited, strlen(fips_hostkeys) + 1, "%s", fips_hostkeys);
        assert_int_equal(rc, strlen(fips_hostkeys));
    } else {
        awaited = calloc(strlen(def_hostkeys) + 1, 1);
        rc = snprintf(awaited, strlen(def_hostkeys) + 1, "%s", def_hostkeys);
        assert_int_equal(rc, strlen(def_hostkeys));
    }
    /* remove the substring from the defaults */
    helper_remove_substring(awaited, hostkeys_removed, 0);
    assert_string_equal(session->opts.wanted_methods[SSH_HOSTKEYS], awaited);
    free(awaited);

    /* check ciphers */
    if (ssh_fips_mode()) {
        awaited = calloc(strlen(fips_ciphers) + 1, 1);
        rc = snprintf(awaited, strlen(fips_ciphers) + 1, "%s", fips_ciphers);
        assert_int_equal(rc, strlen(fips_ciphers));
    } else {
        awaited = calloc(strlen(def_ciphers) + 1, 1);
        rc = snprintf(awaited, strlen(def_ciphers) + 1, "%s", def_ciphers);
        assert_int_equal(rc, strlen(def_ciphers));
    }
    /* remove the substring from the defaults */
    helper_remove_substring(awaited, ciphers_removed, 0);
    assert_string_equal(session->opts.wanted_methods[SSH_CRYPT_C_S], awaited);
    free(awaited);

    /* check kex */
    if (ssh_fips_mode()) {
        awaited = calloc(strlen(fips_kex) + 1, 1);
        rc = snprintf(awaited, strlen(fips_kex) + 1, "%s", fips_kex);
        assert_int_equal(rc, strlen(fips_kex));
        /* remove the substring from the defaults */
        helper_remove_substring(awaited, fips_kex_removed, 0);
    } else {
        awaited = calloc(strlen(def_kex) + 1, 1);
        rc = snprintf(awaited, strlen(def_kex) + 1, "%s", def_kex);
        assert_int_equal(rc, strlen(def_kex));
        /* remove the substring from the defaults */
        helper_remove_substring(awaited, kex_removed, 0);
    }
    assert_string_equal(session->opts.wanted_methods[SSH_KEX], awaited);
    free(awaited);

    /* check mac */
    if (ssh_fips_mode()) {
        awaited = calloc(strlen(fips_mac) + 1, 1);
        rc = snprintf(awaited, strlen(fips_mac) + 1, "%s", fips_mac);
        assert_int_equal(rc, strlen(fips_mac));
    } else {
        awaited = calloc(strlen(def_mac) + 1, 1);
        rc = snprintf(awaited, strlen(def_mac) + 1, "%s", def_mac);
        assert_int_equal(rc, strlen(def_mac));
    }
    /* remove the substring from the defaults */
    helper_remove_substring(awaited, mac_removed, 0);
    assert_string_equal(session->opts.wanted_methods[SSH_MAC_C_S], awaited);
    free(awaited);
}

/**
 * @brief test that openssh style '-' feature works from file
 */
static void torture_config_minus_file(void **state)
{
    torture_config_minus(state, LIBSSH_TESTCONFIG15, NULL);
}

/**
 * @brief test that openssh style '-' feature works from string
 */
static void torture_config_minus_string(void **state)
{
    torture_config_minus(state, NULL, LIBSSH_TESTCONFIG_STRING15);
}

/**
 * @brief test that openssh style '^' feature works from string
 */
static void torture_config_caret(void **state,
                                 const char *file, const char *string)
{
    ssh_session session = *state;
    const char *def_hostkeys = ssh_kex_get_default_methods(SSH_HOSTKEYS);
    const char *fips_hostkeys = ssh_kex_get_fips_methods(SSH_HOSTKEYS);
    const char *def_ciphers = ssh_kex_get_default_methods(SSH_CRYPT_C_S);
    const char *fips_ciphers = ssh_kex_get_fips_methods(SSH_CRYPT_C_S);
    const char *def_kex = ssh_kex_get_default_methods(SSH_KEX);
    const char *fips_kex = ssh_kex_get_fips_methods(SSH_KEX);
    const char *def_mac = ssh_kex_get_default_methods(SSH_MAC_C_S);
    const char *fips_mac = ssh_kex_get_fips_methods(SSH_MAC_C_S);
    const char *hostkeys_prio = "rsa-sha2-512,rsa-sha2-256";
    const char *ciphers_prio = "aes256-cbc,";
    const char *kex_prio = "diffie-hellman-group18-sha512,diffie-hellman-group16-sha512,";
    const char *fips_kex_prio = ",diffie-hellman-group16-sha512,diffie-hellman-group18-sha512";
    const char *mac_prio = "hmac-sha1,";
    char *awaited = NULL;
    int rc;

    _parse_config(session, file, string, SSH_OK);

    /* check hostkeys */
    /* +2 for the added comma and the \0 */
    if (ssh_fips_mode()) {
        awaited = calloc(strlen(hostkeys_prio) + strlen(fips_hostkeys) + 2, 1);
        rc = snprintf(awaited, strlen(hostkeys_prio) + strlen(fips_hostkeys) + 2,
                      "%s,%s", hostkeys_prio, fips_hostkeys);
        assert_int_equal(rc, strlen(hostkeys_prio) + strlen(fips_hostkeys) + 1);
    } else {
        awaited = calloc(strlen(def_hostkeys) + strlen(hostkeys_prio) + 2, 1);
        rc = snprintf(awaited, strlen(hostkeys_prio) + strlen(def_hostkeys) + 2,
                      "%s,%s", hostkeys_prio, def_hostkeys);
        assert_int_equal(rc, strlen(hostkeys_prio) + strlen(def_hostkeys) + 1);
    }

    /* remove the substring from the defaults */
    helper_remove_substring(awaited, hostkeys_prio, 1);
    /* remove the comma at the end of the list */
    awaited[strlen(awaited) - 1] = '\0';

    assert_string_equal(session->opts.wanted_methods[SSH_HOSTKEYS], awaited);
    free(awaited);

    /* check ciphers */
    if (ssh_fips_mode()) {
        awaited = calloc(strlen(ciphers_prio) + strlen(fips_ciphers) + 1, 1);
        rc = snprintf(awaited, strlen(ciphers_prio) + strlen(fips_ciphers) + 1,
                      "%s%s", ciphers_prio, fips_ciphers);
        assert_int_equal(rc, strlen(ciphers_prio) + strlen(fips_ciphers));
        /* remove the substring from the defaults */
        helper_remove_substring(awaited, ciphers_prio, 1);
    } else {
        /* + 2 because the '\0' and the comma */
        awaited = calloc(strlen(ciphers_prio) + strlen(def_ciphers) + 1, 1);
        rc = snprintf(awaited, strlen(ciphers_prio) + strlen(def_ciphers) + 1,
                      "%s%s", ciphers_prio, def_ciphers);
        assert_int_equal(rc, strlen(ciphers_prio) + strlen(def_ciphers));
    }

    assert_string_equal(session->opts.wanted_methods[SSH_CRYPT_C_S], awaited);
    free(awaited);

    /* check kex */
    if (ssh_fips_mode()) {
        awaited = calloc(strlen(kex_prio) + strlen(fips_kex) + 1, 1);
        rc = snprintf(awaited, strlen(kex_prio) + strlen(fips_kex) + 1,
                      "%s%s", kex_prio, fips_kex);
        assert_int_equal(rc, strlen(kex_prio) + strlen(fips_kex));
        /* remove the substring from the defaults */
        /* the default list has different order of these two algos than the fips
         * and because here is a braindead string substitution being done,
         * change the order and remove the first occurrence of it */
        helper_remove_substring(awaited, fips_kex_prio, 0);
    } else {
        awaited = calloc(strlen(kex_prio) + strlen(def_kex) + 1, 1);
        rc = snprintf(awaited, strlen(kex_prio) + strlen(def_kex) + 1,
                      "%s%s", kex_prio, def_kex);
        assert_int_equal(rc, strlen(def_kex) + strlen(kex_prio));
        /* remove the substring from the defaults */
        helper_remove_substring(awaited, kex_prio, 1);
    }

    assert_string_equal(session->opts.wanted_methods[SSH_KEX], awaited);
    free(awaited);

    /* check mac */
    if (ssh_fips_mode()) {
        awaited = calloc(strlen(mac_prio) + strlen(fips_mac) + 1, 1);
        rc = snprintf(awaited, strlen(mac_prio) + strlen(fips_mac) + 1, "%s%s", mac_prio, fips_mac);
        assert_int_equal(rc, strlen(mac_prio) + strlen(fips_mac));
        /* the fips list contains hmac-sha1 algo */
        helper_remove_substring(awaited, mac_prio, 1);
    } else {
        awaited = calloc(strlen(mac_prio) + strlen(def_mac) + 1, 1);
        /* the mac is not in default; it is added to the list */
        rc = snprintf(awaited, strlen(mac_prio) + strlen(def_mac) + 1, "%s%s", mac_prio, def_mac);
        assert_int_equal(rc, strlen(mac_prio) + strlen(def_mac));
    }
    assert_string_equal(session->opts.wanted_methods[SSH_MAC_C_S], awaited);
    free(awaited);
}

/**
 * @brief test that openssh style '^' feature works from file
 */
static void torture_config_caret_file(void **state)
{
    torture_config_caret(state, LIBSSH_TESTCONFIG16, NULL);
}

/**
 * @brief test that openssh style '^' feature works from string
 */
static void torture_config_caret_string(void **state)
{
    torture_config_caret(state, NULL, LIBSSH_TESTCONFIG_STRING16);
}

/**
 * @brief test PubkeyAcceptedKeyTypes helper function
 */
static void torture_config_pubkeytypes(void **state,
                                       const char *file, const char *string)
{
    ssh_session session = *state;
    char *fips_algos;

    _parse_config(session, file, string, SSH_OK);

    if (ssh_fips_mode()) {
        fips_algos = ssh_keep_fips_algos(SSH_HOSTKEYS, PUBKEYACCEPTEDTYPES);
        assert_non_null(fips_algos);
        assert_string_equal(session->opts.pubkey_accepted_types, fips_algos);
        SAFE_FREE(fips_algos);
    } else {
        assert_string_equal(session->opts.pubkey_accepted_types,
                PUBKEYACCEPTEDTYPES);
    }
}

/**
 * @brief test parsing PubkeyAcceptedKeyTypes from file
 */
static void torture_config_pubkeytypes_file(void **state)
{
    torture_config_pubkeytypes(state, LIBSSH_TEST_PUBKEYTYPES, NULL);
}

/**
 * @brief test parsing PubkeyAcceptedKeyTypes from string
 */
static void torture_config_pubkeytypes_string(void **state)
{
    torture_config_pubkeytypes(state, NULL, LIBSSH_TEST_PUBKEYTYPES_STRING);
}

/**
 * @brief test parsing PubkeyAcceptedKAlgorithms from file
 */
static void torture_config_pubkeyalgorithms_file(void **state)
{
    torture_config_pubkeytypes(state, LIBSSH_TEST_PUBKEYALGORITHMS, NULL);
}

/**
 * @brief test parsing PubkeyAcceptedAlgorithms from string
 */
static void torture_config_pubkeyalgorithms_string(void **state)
{
    torture_config_pubkeytypes(state, NULL, LIBSSH_TEST_PUBKEYALGORITHMS_STRING);
}

/**
 * @brief Verify the configuration parser handles
 * missing newline in the end
 */
static void torture_config_nonewlineend(void **state,
                                        const char *file, const char *string)
{
    _parse_config(*state, file, string, SSH_OK);
}

/**
 * @brief Verify the configuration parser handles
 * missing newline in the end of file
 */
static void torture_config_nonewlineend_file(void **state)
{
    torture_config_nonewlineend(state, LIBSSH_TEST_NONEWLINEEND, NULL);
}

/**
 * @brief Verify the configuration parser handles
 * missing newline in the end of string
 */
static void torture_config_nonewlineend_string(void **state)
{
    torture_config_nonewlineend(state, NULL, LIBSSH_TEST_NONEWLINEEND_STRING);
}

/**
 * @brief Verify the configuration parser handles
 * missing newline in the end
 */
static void torture_config_nonewlineoneline(void **state,
                                            const char *file,
                                            const char *string)
{
    _parse_config(*state, file, string, SSH_OK);
}

/**
 * @brief Verify the configuration parser handles
 * missing newline in the end of file
 */
static void torture_config_nonewlineoneline_file(void **state)
{
    torture_config_nonewlineend(state, LIBSSH_TEST_NONEWLINEONELINE, NULL);
}

/**
 * @brief Verify the configuration parser handles
 * missing newline in the end of string
 */
static void torture_config_nonewlineoneline_string(void **state)
{
    torture_config_nonewlineoneline(state,
            NULL, LIBSSH_TEST_NONEWLINEONELINE_STRING);
}

/* ssh_config_get_cmd() does these two things:
 *  * Strips leading whitespace
 *  * Terminate on the end of line
 */
static void torture_config_parser_get_cmd(void **state)
{
    char *p = NULL, *tok = NULL;
    char data[256];
#ifdef WITH_EXEC
    FILE *outfile = NULL, *infile = NULL;
    int pid;
    char buffer[256] = {0};
#endif
    (void)state;

    /* Ignore leading whitespace */
    strncpy(data, "  \t\t  string\n", sizeof(data));
    p = data;
    tok = ssh_config_get_cmd(&p);
    assert_string_equal(tok, "string");
    assert_int_equal(*p, '\0');

    /* but keeps the trailing whitespace */
    strncpy(data, "string  \t\t  \n", sizeof(data));
    p = data;
    tok = ssh_config_get_cmd(&p);
    assert_string_equal(tok, "string  \t\t  ");
    assert_int_equal(*p, '\0');

    /* should not drop the quotes and not split them into separate arguments */
    strncpy(data, "\"multi string\" something\n", sizeof(data));
    p = data;
    tok = ssh_config_get_cmd(&p);
    assert_string_equal(tok, "\"multi string\" something");
    assert_int_equal(*p, '\0');

    /* But it does not split tokens by whitespace
     * if they are not quoted, which is weird */
    strncpy(data, "multi string something\n", sizeof(data));
    p = data;
    tok = ssh_config_get_cmd(&p);
    assert_string_equal(tok, "multi string something");
    assert_int_equal(*p, '\0');

    /* Commands in quotes are not treated special */
    sprintf(data, "%s%s%s%s", "\"", SOURCEDIR "/tests/unittests/hello world.sh", "\" ", "\"hello libssh\"\n");
    printf("%s\n", data);
    p = data;
    tok = ssh_config_get_cmd(&p);
    assert_string_equal(tok, data);
    assert_int_equal(*p, '\0');

#ifdef WITH_EXEC
    /* Check if the command would get correctly executed
     * Use the script file "hello world.sh" to echo the first argument
     * Run as <= "/workdir/hello world.sh" "hello libssh" => */

    /* output to file and check wrong */
    outfile = fopen("output.log", "a+");
    assert_non_null(outfile);
    printf("the tok is %s\n", tok);

    pid = fork();
    if (pid == -1) {
        perror("fork");
    } else if (pid == 0) {
        ssh_execute_command(tok, fileno(outfile), fileno(outfile));
        /* Does not return */
    } else {
        /* parent
         * wait child process */
        wait(NULL);
        infile = fopen("output.log", "r");
        assert_non_null(infile);
        p = fgets(buffer, sizeof(buffer), infile);
        fclose(infile);
        remove("output.log");
        assert_non_null(p);
    }

    fclose(outfile);
    assert_string_equal(buffer, "hello libssh");
#endif /* WITH_EXEC */
}

/* ssh_config_get_token() should behave as expected
 *  * Strip leading whitespace
 *  * Return first token separated by whitespace or equal sign,
 *    respecting quotes!
 *  * Correctly treat escaped quotes inside of quotes.
 */
static void torture_config_parser_get_token(void **state)
{
    char *p = NULL, *tok = NULL;
    char data[256];

    (void) state;

    /* Ignore leading whitespace (from get_cmd() already */
    strncpy(data, "  \t\t  string\n", sizeof(data));
    p = data;
    tok = ssh_config_get_token(&p);
    assert_string_equal(tok, "string");
    assert_int_equal(*p, '\0');

    strncpy(data, "  \t\t  string", sizeof(data));
    p = data;
    tok = ssh_config_get_token(&p);
    assert_string_equal(tok, "string");
    assert_int_equal(*p, '\0');

    /* drops trailing whitespace */
    strncpy(data, "string  \t\t  \n", sizeof(data));
    p = data;
    tok = ssh_config_get_token(&p);
    assert_string_equal(tok, "string");
    assert_int_equal(*p, '\0');

    strncpy(data, "string  \t\t  ", sizeof(data));
    p = data;
    tok = ssh_config_get_token(&p);
    assert_string_equal(tok, "string");
    assert_int_equal(*p, '\0');

    /* Correctly handles tokens in quotes */
    strncpy(data, "\"multi string\" something\n", sizeof(data));
    p = data;
    tok = ssh_config_get_token(&p);
    assert_string_equal(tok, "multi string");
    assert_int_equal(*p, 's');
    tok = ssh_config_get_token(&p);
    assert_string_equal(tok, "something");
    assert_int_equal(*p, '\0');

    strncpy(data, "\"multi string\" something", sizeof(data));
    p = data;
    tok = ssh_config_get_token(&p);
    assert_string_equal(tok, "multi string");
    assert_int_equal(*p, 's');
    tok = ssh_config_get_token(&p);
    assert_string_equal(tok, "something");
    assert_int_equal(*p, '\0');

    /* Consistently splits unquoted strings */
    strncpy(data, "multi string something\n", sizeof(data));
    p = data;
    tok = ssh_config_get_token(&p);
    assert_string_equal(tok, "multi");
    assert_int_equal(*p, 's');
    tok = ssh_config_get_token(&p);
    assert_string_equal(tok, "string");
    assert_int_equal(*p, 's');
    tok = ssh_config_get_token(&p);
    assert_string_equal(tok, "something");
    assert_int_equal(*p, '\0');

    strncpy(data, "multi string something", sizeof(data));
    p = data;
    tok = ssh_config_get_token(&p);
    assert_string_equal(tok, "multi");
    assert_int_equal(*p, 's');
    tok = ssh_config_get_token(&p);
    assert_string_equal(tok, "string");
    assert_int_equal(*p, 's');
    tok = ssh_config_get_token(&p);
    assert_string_equal(tok, "something");
    assert_int_equal(*p, '\0');

    /* It is made to parse also option=value pairs as well */
    strncpy(data, "  key=value  \n", sizeof(data));
    p = data;
    tok = ssh_config_get_token(&p);
    assert_string_equal(tok, "key");
    assert_int_equal(*p, 'v');
    tok = ssh_config_get_token(&p);
    assert_string_equal(tok, "value");
    assert_int_equal(*p, '\0');

    strncpy(data, "  key=value  ", sizeof(data));
    p = data;
    tok = ssh_config_get_token(&p);
    assert_string_equal(tok, "key");
    assert_int_equal(*p, 'v');
    tok = ssh_config_get_token(&p);
    assert_string_equal(tok, "value");
    assert_int_equal(*p, '\0');

    /* spaces are allowed also around the equal sign */
    strncpy(data, "  key  =  value  \n", sizeof(data));
    p = data;
    tok = ssh_config_get_token(&p);
    assert_string_equal(tok, "key");
    assert_int_equal(*p, 'v');
    tok = ssh_config_get_token(&p);
    assert_string_equal(tok, "value");
    assert_int_equal(*p, '\0');

    strncpy(data, "  key  =  value  ", sizeof(data));
    p = data;
    tok = ssh_config_get_token(&p);
    assert_string_equal(tok, "key");
    assert_int_equal(*p, 'v');
    tok = ssh_config_get_token(&p);
    assert_string_equal(tok, "value");
    assert_int_equal(*p, '\0');

    /* correctly parses even key=value pairs with either one in quotes */
    strncpy(data, "  key=\"value with spaces\" \n", sizeof(data));
    p = data;
    tok = ssh_config_get_token(&p);
    assert_string_equal(tok, "key");
    assert_int_equal(*p, '\"');
    tok = ssh_config_get_token(&p);
    assert_string_equal(tok, "value with spaces");
    assert_int_equal(*p, '\0');

    strncpy(data, "  key=\"value with spaces\" ", sizeof(data));
    p = data;
    tok = ssh_config_get_token(&p);
    assert_string_equal(tok, "key");
    assert_int_equal(*p, '\"');
    tok = ssh_config_get_token(&p);
    assert_string_equal(tok, "value with spaces");
    assert_int_equal(*p, '\0');

    /* Only one equal sign is allowed */
    strncpy(data, "key==value\n", sizeof(data));
    p = data;
    tok = ssh_config_get_token(&p);
    assert_string_equal(tok, "key");
    assert_int_equal(*p, '=');
    tok = ssh_config_get_token(&p);
    assert_string_equal(tok, "");
    assert_int_equal(*p, 'v');
    tok = ssh_config_get_token(&p);
    assert_string_equal(tok, "value");
    assert_int_equal(*p, '\0');

    strncpy(data, "key==value", sizeof(data));
    p = data;
    tok = ssh_config_get_token(&p);
    assert_string_equal(tok, "key");
    assert_int_equal(*p, '=');
    tok = ssh_config_get_token(&p);
    assert_string_equal(tok, "");
    assert_int_equal(*p, 'v');
    tok = ssh_config_get_token(&p);
    assert_string_equal(tok, "value");
    assert_int_equal(*p, '\0');

    /* Unmatched quotes */
    strncpy(data, " \"value\n", sizeof(data));
    p = data;
    tok = ssh_config_get_token(&p);
    assert_string_equal(tok, "value");
    assert_int_equal(*p, '\0');

    strncpy(data, " \"value", sizeof(data));
    p = data;
    tok = ssh_config_get_token(&p);
    assert_string_equal(tok, "value");
    assert_int_equal(*p, '\0');

    /* Escaped quotes */
    strncpy(data, " \"value with \\\"escaped\\\" quotes\"   \n", sizeof(data));
    p = data;
    tok = ssh_config_get_token(&p);
    assert_string_equal(tok, "value with \"escaped\" quotes");
    assert_int_equal(*p, '\0');

    strncpy(data, "\\\"value with \\\"escaped\\\" quotes\\\"\n", sizeof(data));
    p = data;
    tok = ssh_config_get_token(&p);
    assert_string_equal(tok, "\\\"value");
    assert_int_equal(*p, 'w');
    tok = ssh_config_get_token(&p);
    assert_string_equal(tok, "with");
    assert_int_equal(*p, '\\');
    tok = ssh_config_get_token(&p);
    assert_string_equal(tok, "\\\"escaped\\\"");
    assert_int_equal(*p, 'q');
    tok = ssh_config_get_token(&p);
    assert_string_equal(tok, "quotes\\\"");
    assert_int_equal(*p, '\0');
}

/* match_pattern() sanity tests
 */
static void torture_config_match_pattern(void **state)
{
    int rv = 0;

    (void) state;

    /* Simple test "a" matches "a" */
    rv = match_pattern("a", "a");
    assert_int_equal(rv, 1);

    /* Simple test "a" does not match "b" */
    rv = match_pattern("a", "b");
    assert_int_equal(rv, 0);

    /* NULL arguments are correctly handled */
    rv = match_pattern("a", NULL);
    assert_int_equal(rv, 0);
    rv = match_pattern(NULL, "a");
    assert_int_equal(rv, 0);

    /* Simple wildcard ? is handled in pattern */
    rv = match_pattern("a", "?");
    assert_int_equal(rv, 1);
    rv = match_pattern("aa", "?");
    assert_int_equal(rv, 0);
    /* Wildcard in search string */
    rv = match_pattern("?", "a");
    assert_int_equal(rv, 0);
    rv = match_pattern("?", "?");
    assert_int_equal(rv, 1);

    /* Simple wildcard * is handled in pattern */
    rv = match_pattern("a", "*");
    assert_int_equal(rv, 1);
    rv = match_pattern("aa", "*");
    assert_int_equal(rv, 1);
    /* Wildcard in search string */
    rv = match_pattern("*", "a");
    assert_int_equal(rv, 0);
    rv = match_pattern("*", "*");
    assert_int_equal(rv, 1);

    /* More complicated patterns */
    rv = match_pattern("a", "*a");
    assert_int_equal(rv, 1);
    rv = match_pattern("a", "a*");
    assert_int_equal(rv, 1);
    rv = match_pattern("abababc", "*abc");
    assert_int_equal(rv, 1);
    rv = match_pattern("ababababca", "*abc");
    assert_int_equal(rv, 0);
    rv = match_pattern("ababababca", "*abc*");
    assert_int_equal(rv, 1);

    /* Multiple wildcards in row */
    rv = match_pattern("aa", "??");
    assert_int_equal(rv, 1);
    rv = match_pattern("bba", "??a");
    assert_int_equal(rv, 1);
    rv = match_pattern("aaa", "**a");
    assert_int_equal(rv, 1);
    rv = match_pattern("bbb", "**a");
    assert_int_equal(rv, 0);

    /* Consecutive asterisks do not make sense and do not need to recurse */
    rv = match_pattern("hostname", "**********pattern");
    assert_int_equal(rv, 0);
    rv = match_pattern("hostname", "pattern**********");
    assert_int_equal(rv, 0);
    rv = match_pattern("pattern", "***********pattern");
    assert_int_equal(rv, 1);
    rv = match_pattern("pattern", "pattern***********");
    assert_int_equal(rv, 1);

    rv = match_pattern("hostname", "*p*a*t*t*e*r*n*");
    assert_int_equal(rv, 0);
    rv = match_pattern("pattern", "*p*a*t*t*e*r*n*");
    assert_int_equal(rv, 1);

    /* Regular Expression Denial of Service */
    rv = match_pattern("aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa",
                       "*a*a*a*a*a*a*a*a*a*a*a*a*a*a*a*a");
    assert_int_equal(rv, 1);
    rv = match_pattern("ababababababababababababababababababababab",
                       "*a*b*a*b*a*b*a*b*a*b*a*b*a*b*a*b");
    assert_int_equal(rv, 1);

    /* A lot of backtracking */
    rv = match_pattern("aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaax",
                       "a*a*a*a*a*a*a*a*a*a*a*a*a*a*a*a*a*a*ax");
    assert_int_equal(rv, 1);

    /* Test backtracking: *a matches first 'a', fails on 'b', must backtrack */
    rv = match_pattern("axaxaxb", "*a*b");
    assert_int_equal(rv, 1);

    /* Test greedy consumption with suffix */
    rv = match_pattern("foo_bar_baz_bar", "*bar");
    assert_int_equal(rv, 1);

    /* Test exact suffix requirement (ensure no partial match acceptance) */
    rv = match_pattern("foobar_extra", "*bar");
    assert_int_equal(rv, 0);

    /* Test multiple distinct wildcards */
    rv = match_pattern("a_very_long_string_with_a_pattern", "*long*pattern");
    assert_int_equal(rv, 1);

    /* ? inside a * sequence */
    rv = match_pattern("abcdefg", "a*c?e*g");
    assert_int_equal(rv, 1);

    /* Consecutive mixed wildcards */
    rv = match_pattern("abc", "*?c");
    assert_int_equal(rv, 1);

    /* ? at the very end after * */
    rv = match_pattern("abc", "ab?");
    assert_int_equal(rv, 1);
    rv = match_pattern("abc", "ab*?");
    assert_int_equal(rv, 1);

    /* Consecutive stars should be collapsed or handled gracefully */
    rv = match_pattern("abc", "a**c");
    assert_int_equal(rv, 1);
    rv = match_pattern("abc", "***");
    assert_int_equal(rv, 1);

    /* Empty string handling */
    rv = match_pattern("", "*");
    assert_int_equal(rv, 1);
    rv = match_pattern("", "?");
    assert_int_equal(rv, 0);
    rv = match_pattern("", "");
    assert_int_equal(rv, 1);

    /* Pattern longer than string */
    rv = match_pattern("short", "short_but_longer");
    assert_int_equal(rv, 0);
}

/* Identity file can be specified multiple times in the configuration
 */
static void torture_config_identity(void **state)
{
    const char *id = NULL;
    const char *cert = NULL;
    struct ssh_iterator *it = NULL;
    ssh_session session = *state;

    _parse_config(session, NULL, LIBSSH_TESTCONFIG_STRING13, SSH_OK);

    /* The identities are first added to this temporary list before expanding */
    it = ssh_list_get_iterator(session->opts.identity_non_exp);
    assert_non_null(it);
    id = it->data;
    /* The identities are prepended to the list so we start with second one */
    assert_string_equal(id, "id_ecdsa_two");

    it = it->next;
    assert_non_null(it);
    id = it->data;
    assert_string_equal(id, "id_rsa_one");

    /* The certs are first added to this temporary list before expanding */
    it = ssh_list_get_iterator(session->opts.certificate_non_exp);
    assert_non_null(it);
    cert = it->data;
    /* The certs are coming as listed in the configuration file */
    assert_string_equal(cert, "id_rsa_one-cert.pub");

    it = it->next;
    assert_non_null(it);
    cert = it->data;
    assert_string_equal(cert, "id_ecdsa_two-cert.pub");
    /* and that is all */
    assert_null(it->next);
}

/* Make absolute path for config include
 */
static void torture_config_make_absolute_int(void **state, bool no_sshdir_fails)
{
    ssh_session session = *state;
    char *result = NULL;
#ifndef _WIN32
    char h[256] = {0};
    char *user = NULL;
    char *home = NULL;
    struct passwd *pw = getpwuid(getuid());
    assert_non_null(pw);
    user = strdup(pw->pw_name);
    assert_non_null(user);
    home = strdup(pw->pw_dir);
    assert_non_null(home);
#endif

    /* Absolute path already -- should not change in any case */
    result = ssh_config_make_absolute(session, "/etc/ssh/ssh_config.d/*.conf", 1);
    assert_string_equal(result, "/etc/ssh/ssh_config.d/*.conf");
    free(result);
    result = ssh_config_make_absolute(session, "/etc/ssh/ssh_config.d/*.conf", 0);
    assert_string_equal(result, "/etc/ssh/ssh_config.d/*.conf");
    free(result);

    /* Global is relative to /etc/ssh/ */
    result = ssh_config_make_absolute(session, "ssh_config.d/test.conf", 1);
    assert_string_equal(result, "/etc/ssh/ssh_config.d/test.conf");
    free(result);
    result = ssh_config_make_absolute(session, "./ssh_config.d/test.conf", 1);
    assert_string_equal(result, "/etc/ssh/./ssh_config.d/test.conf");
    free(result);

    /* User config is relative to sshdir -- here faked to /tmp/ssh/ */
    result = ssh_config_make_absolute(session, "my_config", 0);
    if (no_sshdir_fails) {
        assert_null(result);
    } else {
        /* The path depends on the PWD so lets skip checking the actual path here */
        assert_non_null(result);
    }
    free(result);

    /* User config is relative to sshdir -- here faked to /tmp/ssh/ */
    ssh_options_set(session, SSH_OPTIONS_SSH_DIR, "/tmp/ssh");
    result = ssh_config_make_absolute(session, "my_config", 0);
    assert_string_equal(result, "/tmp/ssh/my_config");
    free(result);

#ifndef _WIN32
    /* Tilde expansion works only in user config */
    result = ssh_config_make_absolute(session, "~/.ssh/config.d/*.conf", 0);
    snprintf(h, 256 - 1, "%s/.ssh/config.d/*.conf", home);
    assert_string_equal(result, h);
    free(result);

    snprintf(h, 256 - 1, "~%s/.ssh/config.d/*.conf", user);
    result = ssh_config_make_absolute(session, h, 0);
    snprintf(h, 256 - 1, "%s/.ssh/config.d/*.conf", home);
    assert_string_equal(result, h);
    free(result);

    /* in global config its just prefixed without expansion */
    result = ssh_config_make_absolute(session, "~/.ssh/config.d/*.conf", 1);
    assert_string_equal(result, "/etc/ssh/~/.ssh/config.d/*.conf");
    free(result);
    snprintf(h, 256 - 1, "~%s/.ssh/config.d/*.conf", user);
    result = ssh_config_make_absolute(session, h, 1);
    snprintf(h, 256 - 1, "/etc/ssh/~%s/.ssh/config.d/*.conf", user);
    assert_string_equal(result, h);
    free(result);
    free(home);
    free(user);
#endif
}

static void torture_config_make_absolute(void **state)
{
    torture_config_make_absolute_int(state, 0);
}

static void torture_config_make_absolute_no_sshdir(void **state)
{
    torture_config_make_absolute_int(state, 1);
}

static void torture_config_parse_uri(void **state)
{
    char *username = NULL;
    char *hostname = NULL;
    char *port = NULL;
    int rc;

    (void)state; /* unused */

    rc = ssh_config_parse_uri("localhost", &username, &hostname, &port, false);
    assert_return_code(rc, errno);
    assert_null(username);
    assert_string_equal(hostname, "localhost");
    SAFE_FREE(hostname);
    assert_null(port);

    rc = ssh_config_parse_uri("1.2.3.4", &username, &hostname, &port, false);
    assert_return_code(rc, errno);
    assert_null(username);
    assert_string_equal(hostname, "1.2.3.4");
    SAFE_FREE(hostname);
    assert_null(port);

    rc = ssh_config_parse_uri("1.2.3.4:2222", &username, &hostname, &port, false);
    assert_return_code(rc, errno);
    assert_null(username);
    assert_string_equal(hostname, "1.2.3.4");
    SAFE_FREE(hostname);
    assert_string_equal(port, "2222");
    SAFE_FREE(port);

    rc = ssh_config_parse_uri("[1:2:3::4]:2222", &username, &hostname, &port, false);
    assert_return_code(rc, errno);
    assert_null(username);
    assert_string_equal(hostname, "1:2:3::4");
    SAFE_FREE(hostname);
    assert_string_equal(port, "2222");
    SAFE_FREE(port);

    /* do not want port */
    rc = ssh_config_parse_uri("1:2:3::4", &username, &hostname, NULL, true);
    assert_return_code(rc, errno);
    assert_null(username);
    assert_string_equal(hostname, "1:2:3::4");
    SAFE_FREE(hostname);

    rc = ssh_config_parse_uri("user -name@", &username, NULL, NULL, true);
    assert_int_equal(rc, SSH_ERROR);
}

/* Complex ssh match configurations
 */
static void torture_config_match_complex(void **state)
{
    ssh_session session = *state;
    char *v = NULL;
    int ret;

    ssh_options_set(session, SSH_OPTIONS_HOST, "Bar");

    _parse_config(session, LIBSSH_TESTCONFIG_MATCH_COMPLEX, NULL, SSH_OK);

    /* Test the variable presence */
    ret = ssh_options_get(session, SSH_OPTIONS_HOST, &v);
    assert_return_code(ret, errno);
    assert_non_null(v);
#ifndef WITH_EXEC
    assert_string_equal(session->opts.host, "Bar");
#else
    assert_string_equal(v, "complex-match");
#endif
    ssh_string_free_char(v);
}

/* Missing value to LogLevel configuration option
 */
static void torture_config_loglevel_missing_value(void **state)
{
    ssh_session session = *state;

    ssh_options_set(session, SSH_OPTIONS_HOST, "Bar");

    _parse_config(session, LIBSSH_TESTCONFIG_LOGLEVEL_MISSING, NULL, SSH_OK);
}

static int before_connection(ssh_session jump_session, void *user)
{
    char *v = NULL;
    int ret;

    (void)user;

    /* During the connection, we force parsing the same configuration file
     * (would be normally parsed automatically during the connection itself)
     */
    ret = ssh_config_parse_file(jump_session, LIBSSH_TESTCONFIG_JUMP);
    assert_return_code(ret, errno);

    /* Test the variable presence */
    ret = ssh_options_get(jump_session, SSH_OPTIONS_HOST, &v);
    assert_return_code(ret, errno);
    assert_string_equal(v, "1xxxxxx");
    ssh_string_free_char(v);

    ret = ssh_options_get(jump_session, SSH_OPTIONS_USER, &v);
    assert_return_code(ret, errno);
    assert_string_equal(v, "ubuntu");
    ssh_string_free_char(v);

    assert_int_equal(jump_session->opts.port, 23);

    /* Fail the connection -- we are in unit tests so it would fail anyway */
    return 1;
}

static int verify_knownhost(ssh_session jump_session, void *user)
{
    (void)jump_session;
    (void)user;

    return 0;
}

static int authenticate(ssh_session jump_session, void *user)
{
    (void)jump_session;
    (void)user;

    return 0;
}
/* Reproducer for complex proxy jump
 */
static void torture_config_jump(void **state)
{
    ssh_session session = *state;
    struct ssh_jump_callbacks_struct c = {
        .before_connection = before_connection,
        .verify_knownhost = verify_knownhost,
        .authenticate = authenticate,
    };
    char *v = NULL;
    int ret;

    ssh_options_set(session, SSH_OPTIONS_HOST, "cisco-router");

    _parse_config(session, LIBSSH_TESTCONFIG_JUMP, NULL, SSH_OK);

    /* Test the variable presence */
    ret = ssh_options_get(session, SSH_OPTIONS_HOST, &v);
    assert_return_code(ret, errno);
    assert_string_equal(v, "xx.xxxxxxxxx");
    ssh_string_free_char(v);

    ret = ssh_options_get(session, SSH_OPTIONS_USER, &v);
    assert_return_code(ret, errno);
    assert_string_equal(v, "username");
    ssh_string_free_char(v);

    assert_int_equal(session->opts.port, 5555);

    /* At this point, the configuration file is not parsed for the jump host so
     * we are getting just the the hostname -- the port and username will get
     * pulled during the session connecting to this host */
    assert_int_equal(ssh_list_count(session->opts.proxy_jumps), 1);
    helper_proxy_jump_check(session->opts.proxy_jumps->root,
                            "ub-jumphost",
                            NULL,
                            NULL);

    /* Set up the callbacks -- they should verify we are going to connect to the
     * right host */
    ret = ssh_options_set(session, SSH_OPTIONS_PROXYJUMP_CB_LIST_APPEND, &c);
    assert_ssh_return_code(session, ret);

    ret = ssh_connect(session);
    assert_ssh_return_code_equal(session, ret, SSH_ERROR);

    printf("%s: EOF\n", __func__);
}

/* Invalid configuration files
 */
static void torture_config_invalid(void **state)
{
    ssh_session session = *state;

    ssh_options_set(session, SSH_OPTIONS_HOST, "Bar");

    /* non-regular file -- ignored (or missing on non-unix) so OK */
    _parse_config(session, "/dev/random", NULL, SSH_OK);

#ifndef _WIN32
    /* huge file -- ignored (or missing on non-unix) so OK */
    _parse_config(session, "/proc/kcore", NULL, SSH_OK);
#endif
}

int torture_run_tests(void)
{
    int rc;
    struct CMUnitTest tests[] = {
        cmocka_unit_test_setup_teardown(torture_config_include_file,
                                        setup, teardown),
        cmocka_unit_test_setup_teardown(torture_config_include_string,
                                        setup, teardown),
        cmocka_unit_test_setup_teardown(torture_config_include_recursive_file,
                                        setup, teardown),
        cmocka_unit_test_setup_teardown(torture_config_include_recursive_string,
                                        setup, teardown),
        cmocka_unit_test_setup_teardown(torture_config_double_ports_file,
                                        setup, teardown),
        cmocka_unit_test_setup_teardown(torture_config_double_ports_string,
                                        setup, teardown),
        cmocka_unit_test_setup_teardown(torture_config_glob_file,
                                        setup, teardown),
        cmocka_unit_test_setup_teardown(torture_config_glob_string,
                                        setup, teardown),
        cmocka_unit_test_setup_teardown(torture_config_new_file,
                                        setup, teardown),
        cmocka_unit_test_setup_teardown(torture_config_new_string,
                                        setup, teardown),
        cmocka_unit_test_setup_teardown(torture_config_auth_methods_file,
                                        setup, teardown),
        cmocka_unit_test_setup_teardown(torture_config_auth_methods_string,
                                        setup, teardown),
        cmocka_unit_test_setup_teardown(torture_config_unknown_file,
                                        setup, teardown),
        cmocka_unit_test_setup_teardown(torture_config_unknown_string,
                                        setup, teardown),
        cmocka_unit_test_setup_teardown(torture_config_match_file,
                                        setup, teardown),
        cmocka_unit_test_setup_teardown(torture_config_match_string,
                                        setup, teardown),
        cmocka_unit_test_setup_teardown(torture_config_proxyjump_file,
                                        setup, teardown),
        cmocka_unit_test_setup_teardown(torture_config_proxyjump_string,
                                        setup, teardown),
        cmocka_unit_test_setup_teardown(torture_config_control_path_file,
                                        setup, teardown),
        cmocka_unit_test_setup_teardown(torture_config_control_path_string,
                                        setup, teardown),
        cmocka_unit_test_setup_teardown(torture_config_control_master_file,
                                        setup, teardown),
        cmocka_unit_test_setup_teardown(torture_config_control_master_string,
                                        setup, teardown),
        cmocka_unit_test_setup_teardown(torture_config_rekey_file,
                                        setup, teardown),
        cmocka_unit_test_setup_teardown(torture_config_rekey_string,
                                        setup, teardown),
        cmocka_unit_test_setup_teardown(torture_config_plus_file,
                                        setup, teardown),
        cmocka_unit_test_setup_teardown(torture_config_plus_string,
                                        setup, teardown),
        cmocka_unit_test_setup_teardown(torture_config_minus_file,
                                        setup, teardown),
        cmocka_unit_test_setup_teardown(torture_config_minus_string,
                                        setup, teardown),
        cmocka_unit_test_setup_teardown(torture_config_caret_file,
                                        setup, teardown),
        cmocka_unit_test_setup_teardown(torture_config_caret_string,
                                        setup, teardown),
        cmocka_unit_test_setup_teardown(torture_config_pubkeytypes_file,
                                        setup, teardown),
        cmocka_unit_test_setup_teardown(torture_config_pubkeytypes_string,
                                        setup, teardown),
        cmocka_unit_test_setup_teardown(torture_config_pubkeyalgorithms_file,
                                        setup, teardown),
        cmocka_unit_test_setup_teardown(torture_config_pubkeyalgorithms_string,
                                        setup, teardown),
        cmocka_unit_test_setup_teardown(torture_config_nonewlineend_file,
                                        setup, teardown),
        cmocka_unit_test_setup_teardown(torture_config_nonewlineend_string,
                                        setup, teardown),
        cmocka_unit_test_setup_teardown(torture_config_nonewlineoneline_file,
                                        setup, teardown),
        cmocka_unit_test_setup_teardown(torture_config_nonewlineoneline_string,
                                        setup, teardown),
        cmocka_unit_test_setup_teardown(torture_config_parser_get_cmd,
                                        setup, teardown),
        cmocka_unit_test_setup_teardown(torture_config_parser_get_token,
                                        setup, teardown),
        cmocka_unit_test_setup_teardown(torture_config_match_pattern,
                                        setup, teardown),
        cmocka_unit_test_setup_teardown(torture_config_identity,
                                        setup, teardown),
        cmocka_unit_test_setup_teardown(torture_config_make_absolute,
                                        setup, teardown),
        cmocka_unit_test_setup_teardown(torture_config_make_absolute_no_sshdir,
                                        setup_no_sshdir, teardown),
        cmocka_unit_test_setup_teardown(torture_config_parse_uri,
                                        setup, teardown),
        cmocka_unit_test_setup_teardown(torture_config_match_complex,
                                        setup, teardown),
        cmocka_unit_test_setup_teardown(torture_config_loglevel_missing_value,
                                        setup,
                                        teardown),
        cmocka_unit_test_setup_teardown(torture_config_jump,
                                        setup,
                                        teardown),
        cmocka_unit_test_setup_teardown(torture_config_invalid,
                                        setup,
                                        teardown),
    };


    ssh_init();
    torture_filter_tests(tests);
    rc = cmocka_run_group_tests(tests,
            setup_config_files, teardown_config_files);
    ssh_finalize();
    return rc;
}
