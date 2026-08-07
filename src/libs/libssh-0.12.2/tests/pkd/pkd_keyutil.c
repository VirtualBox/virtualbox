/*
 * pkd_keyutil.c -- pkd test key utilities
 *
 * (c) 2014 Jon Simons
 */

#include "config.h"

#include <setjmp.h> // for cmocka
#include <stdarg.h> // for cmocka
#include <stdint.h> // for cmocka
#include <unistd.h> // for cmocka
#include <cmocka.h>

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "torture.h" // for ssh_fips_mode()

#include "pkd_client.h"
#include "pkd_keyutil.h"
#include "pkd_util.h"

void setup_rsa_key(void) {
    int rc = 0;
    if (access(LIBSSH_RSA_TESTKEY, F_OK) != 0) {
        rc = system_checked(OPENSSH_KEYGEN " -t rsa -q -N \"\" -f "
                            LIBSSH_RSA_TESTKEY);
    }
    assert_int_equal(rc, 0);
}

void setup_ed25519_key(void) {
    int rc = 0;
    if (access(LIBSSH_ED25519_TESTKEY, F_OK) != 0) {
        rc = system_checked(OPENSSH_KEYGEN " -t ed25519 -q -N \"\" -f "
                            LIBSSH_ED25519_TESTKEY);
    }
    assert_int_equal(rc, 0);
}

void setup_ecdsa_keys(void) {
    int rc = 0;

    if (access(LIBSSH_ECDSA_256_TESTKEY, F_OK) != 0) {
        rc = system_checked(OPENSSH_KEYGEN " -t ecdsa -b 256 -q -N \"\" -f "
                            LIBSSH_ECDSA_256_TESTKEY);
        assert_int_equal(rc, 0);
    }
    if (access(LIBSSH_ECDSA_384_TESTKEY, F_OK) != 0) {
        rc = system_checked(OPENSSH_KEYGEN " -t ecdsa -b 384 -q -N \"\" -f "
                            LIBSSH_ECDSA_384_TESTKEY);
        assert_int_equal(rc, 0);
    }
    if (access(LIBSSH_ECDSA_521_TESTKEY, F_OK) != 0) {
        rc = system_checked(OPENSSH_KEYGEN " -t ecdsa -b 521 -q -N \"\" -f "
                            LIBSSH_ECDSA_521_TESTKEY);
        assert_int_equal(rc, 0);
    }
}

void cleanup_rsa_key(void) {
    cleanup_key(LIBSSH_RSA_TESTKEY);
}

void cleanup_ed25519_key(void) {
    cleanup_key(LIBSSH_ED25519_TESTKEY);
}

void cleanup_ecdsa_keys(void) {
    cleanup_key(LIBSSH_ECDSA_256_TESTKEY);
    cleanup_key(LIBSSH_ECDSA_384_TESTKEY);
    cleanup_key(LIBSSH_ECDSA_521_TESTKEY);
}

void setup_openssh_client_keys(void) {
    int rc = 0;

    if (access(OPENSSH_CA_TESTKEY, F_OK) != 0) {
        rc = system_checked(OPENSSH_KEYGEN " -t rsa -q -N \"\" -f "
                            OPENSSH_CA_TESTKEY);
    }
    assert_int_equal(rc, 0);

    if (access(OPENSSH_RSA_TESTKEY, F_OK) != 0) {
        rc = system_checked(OPENSSH_KEYGEN " -t rsa -q -N \"\" -f "
                            OPENSSH_RSA_TESTKEY);
    }
    assert_int_equal(rc, 0);

    if (access(OPENSSH_RSA_TESTKEY "-cert.pub", F_OK) != 0) {
        rc = system_checked(OPENSSH_KEYGEN " -I ident -s " OPENSSH_CA_TESTKEY " "
                            OPENSSH_RSA_TESTKEY ".pub 2>/dev/null");
    }
    assert_int_equal(rc, 0);

    if (access(OPENSSH_RSA_TESTKEY "-sha256-cert.pub", F_OK) != 0) {
        rc = system_checked(OPENSSH_KEYGEN " -I ident -t rsa-sha2-256 "
                            "-s " OPENSSH_CA_TESTKEY " "
                            OPENSSH_RSA_TESTKEY ".pub 2>/dev/null");
    }
    assert_int_equal(rc, 0);

    if (access(OPENSSH_ECDSA256_TESTKEY, F_OK) != 0) {
        rc = system_checked(OPENSSH_KEYGEN " -t ecdsa -b 256 -q -N \"\" -f "
                            OPENSSH_ECDSA256_TESTKEY);
    }
    assert_int_equal(rc, 0);

    if (access(OPENSSH_ECDSA256_TESTKEY "-cert.pub", F_OK) != 0) {
        rc = system_checked(OPENSSH_KEYGEN " -I ident -s " OPENSSH_CA_TESTKEY " "
                            OPENSSH_ECDSA256_TESTKEY ".pub 2>/dev/null");
    }
    assert_int_equal(rc, 0);

    if (access(OPENSSH_ECDSA384_TESTKEY, F_OK) != 0) {
        rc = system_checked(OPENSSH_KEYGEN " -t ecdsa -b 384 -q -N \"\" -f "
                            OPENSSH_ECDSA384_TESTKEY);
    }
    assert_int_equal(rc, 0);

    if (access(OPENSSH_ECDSA384_TESTKEY "-cert.pub", F_OK) != 0) {
        rc = system_checked(OPENSSH_KEYGEN " -I ident -s " OPENSSH_CA_TESTKEY " "
                            OPENSSH_ECDSA384_TESTKEY ".pub 2>/dev/null");
    }
    assert_int_equal(rc, 0);

    if (access(OPENSSH_ECDSA521_TESTKEY, F_OK) != 0) {
        rc = system_checked(OPENSSH_KEYGEN " -t ecdsa -b 521 -q -N \"\" -f "
                            OPENSSH_ECDSA521_TESTKEY);
    }
    assert_int_equal(rc, 0);

    if (access(OPENSSH_ECDSA521_TESTKEY "-cert.pub", F_OK) != 0) {
        rc = system_checked(OPENSSH_KEYGEN " -I ident -s " OPENSSH_CA_TESTKEY " "
                            OPENSSH_ECDSA521_TESTKEY ".pub 2>/dev/null");
    }
    assert_int_equal(rc, 0);

    if (!ssh_fips_mode()) {

        if (access(OPENSSH_ED25519_TESTKEY, F_OK) != 0) {
            rc = system_checked(OPENSSH_KEYGEN " -t ed25519 -q -N \"\" -f "
                    OPENSSH_ED25519_TESTKEY);
        }
        assert_int_equal(rc, 0);

        if (access(OPENSSH_ED25519_TESTKEY "-cert.pub", F_OK) != 0) {
            rc = system_checked(OPENSSH_KEYGEN " -I ident -s " OPENSSH_CA_TESTKEY " "
                    OPENSSH_ED25519_TESTKEY ".pub 2>/dev/null");
        }
        assert_int_equal(rc, 0);
    }

#ifdef HAVE_SK_DUMMY
    setenv("SSH_SK_PROVIDER", SK_DUMMY_LIBRARY_PATH, 1);
    if (access(OPENSSH_ECDSA_SK_TESTKEY, F_OK) != 0) {
        rc = system_checked(OPENSSH_KEYGEN " -t ecdsa-sk -q -N \"\" -f "
                            OPENSSH_ECDSA_SK_TESTKEY);
    }
    assert_int_equal(rc, 0);

    if (access(OPENSSH_ED25519_SK_TESTKEY, F_OK) != 0) {
        rc = system_checked(OPENSSH_KEYGEN " -t ed25519-sk -q -N \"\" -f "
                            OPENSSH_ED25519_SK_TESTKEY);
    }
    assert_int_equal(rc, 0);
#endif
}

void cleanup_openssh_client_keys(void) {
    cleanup_key(OPENSSH_CA_TESTKEY);
    cleanup_key(OPENSSH_RSA_TESTKEY);
    cleanup_file(OPENSSH_RSA_TESTKEY "-sha256-cert.pub");
    cleanup_key(OPENSSH_ECDSA256_TESTKEY);
    cleanup_key(OPENSSH_ECDSA384_TESTKEY);
    cleanup_key(OPENSSH_ECDSA521_TESTKEY);
    if (!ssh_fips_mode()) {
        cleanup_key(OPENSSH_ED25519_TESTKEY);
    }
#ifdef HAVE_SK_DUMMY
    cleanup_key(OPENSSH_ECDSA_SK_TESTKEY);
    cleanup_key(OPENSSH_ED25519_SK_TESTKEY);
#endif
}

void setup_dropbear_client_keys(void)
{
    int rc = 0;
    if (access(DROPBEAR_RSA_TESTKEY, F_OK) != 0) {
        rc = system_checked(DROPBEAR_KEYGEN " -t rsa -f "
                            DROPBEAR_RSA_TESTKEY " 1>/dev/null 2>/dev/null");
    }
    assert_int_equal(rc, 0);
    if (access(DROPBEAR_ECDSA256_TESTKEY, F_OK) != 0) {
        rc = system_checked(DROPBEAR_KEYGEN " -t ecdsa -f "
                            DROPBEAR_ECDSA256_TESTKEY
                            " 1>/dev/null 2>/dev/null");
    }
    assert_int_equal(rc, 0);
    if (access(DROPBEAR_ED25519_TESTKEY, F_OK) != 0) {
        rc = system_checked(DROPBEAR_KEYGEN " -t ed25519 -f "
                            DROPBEAR_ED25519_TESTKEY
                            " 1>/dev/null 2>/dev/null");
    }
    assert_int_equal(rc, 0);
}

void cleanup_dropbear_client_keys(void)
{
    cleanup_key(DROPBEAR_RSA_TESTKEY);
    cleanup_key(DROPBEAR_ECDSA256_TESTKEY);
    cleanup_key(DROPBEAR_ED25519_TESTKEY);
}

void setup_putty_client_keys(void)
{
    int rc = 0;

    /* RSA Keys */
    if (access(PUTTY_RSA_TESTKEY, F_OK) != 0 ||
        access(PUTTY_RSA_PPK_TESTKEY, F_OK) != 0) {
        rc = system_checked(OPENSSH_KEYGEN " -t rsa -q -N \"\" -f "
                            PUTTY_RSA_TESTKEY);
        assert_int_equal(rc, 0);

        rc = system_checked(PUTTY_KEYGEN " " PUTTY_RSA_TESTKEY
                            " -O private -o " PUTTY_RSA_PPK_TESTKEY);
        assert_int_equal(rc, 0);
    }

    /* ECDSA 256 Keys */
    if (access(PUTTY_ECDSA256_TESTKEY, F_OK) != 0 ||
        access(PUTTY_ECDSA256_PPK_TESTKEY, F_OK) != 0) {
        rc = system_checked(OPENSSH_KEYGEN " -t ecdsa -b 256 -q -N \"\" -f "
                            PUTTY_ECDSA256_TESTKEY);
        assert_int_equal(rc, 0);

        rc = system_checked(PUTTY_KEYGEN " " PUTTY_ECDSA256_TESTKEY
                            " -O private -o " PUTTY_ECDSA256_PPK_TESTKEY);
        assert_int_equal(rc, 0);
    }

    /* ED25519 Keys */
    if (access(PUTTY_ED25519_TESTKEY, F_OK) != 0 ||
        access(PUTTY_ED25519_PPK_TESTKEY, F_OK) != 0) {
        rc = system_checked(OPENSSH_KEYGEN " -t ed25519 -q -N \"\" -f "
                            PUTTY_ED25519_TESTKEY);
        assert_int_equal(rc, 0);

        rc = system_checked(PUTTY_KEYGEN " " PUTTY_ED25519_TESTKEY
                            " -O private -o " PUTTY_ED25519_PPK_TESTKEY);
        assert_int_equal(rc, 0);
    }
}

void cleanup_putty_client_keys(void)
{
    cleanup_key(PUTTY_RSA_TESTKEY);
    cleanup_file(PUTTY_RSA_PPK_TESTKEY);

    cleanup_key(PUTTY_ECDSA256_TESTKEY);
    cleanup_file(PUTTY_ECDSA256_PPK_TESTKEY);
    
    cleanup_key(PUTTY_ED25519_TESTKEY);
    cleanup_file(PUTTY_ED25519_PPK_TESTKEY);
}