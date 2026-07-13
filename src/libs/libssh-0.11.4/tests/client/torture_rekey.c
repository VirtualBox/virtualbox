/*
 * This file is part of the SSH Library
 *
 * Copyright (c) 2018 by Red Hat, Inc.
 *
 * Authors: Jakub Jelen <jjelen@redhat.com>
 *
 * The SSH Library is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation; either version 2.1 of the License, or (at your
 * option) any later version.
 *
 * The SSH Library is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
 * or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU Lesser General Public
 * License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with the SSH Library; see the file COPYING.  If not, write to
 * the Free Software Foundation, Inc., 59 Temple Place - Suite 330, Boston,
 * MA 02111-1307, USA.
 */

#include "config.h"

#define LIBSSH_STATIC

#include "torture.h"
#include "libssh/sftp.h"
#include "libssh/libssh.h"
#include "libssh/priv.h"
#include "libssh/session.h"
#include "libssh/crypto.h"
#include "libssh/token.h"

#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <pwd.h>

#define KEX_RETRY 32

static uint64_t bytes = 2048; /* 2KB (more than the authentication phase) */

static int sshd_setup(void **state)
{
    torture_setup_sshd_server(state, false);

    return 0;
}

static int sshd_teardown(void **state)
{
    torture_teardown_sshd_server(state);

    return 0;
}

static int session_setup(void **state)
{
    struct torture_state *s = *state;
    int verbosity = torture_libssh_verbosity();
    struct passwd *pwd;
    bool b = false;
    int rc;

    pwd = getpwnam("bob");
    assert_non_null(pwd);

    rc = setuid(pwd->pw_uid);
    assert_return_code(rc, errno);

    s->ssh.session = ssh_new();
    assert_non_null(s->ssh.session);

    ssh_options_set(s->ssh.session, SSH_OPTIONS_LOG_VERBOSITY, &verbosity);
    ssh_options_set(s->ssh.session, SSH_OPTIONS_HOST, TORTURE_SSH_SERVER);

    /* Authenticate as alice with bob's pubkey */
    rc = ssh_options_set(s->ssh.session, SSH_OPTIONS_USER, TORTURE_SSH_USER_ALICE);
    assert_int_equal(rc, SSH_OK);

    /* Make sure no other configuration options from system will get used */
    rc = ssh_options_set(s->ssh.session, SSH_OPTIONS_PROCESS_CONFIG, &b);
    assert_ssh_return_code(s->ssh.session, rc);

    /* Make sure we do not interfere with another ssh-agent */
    unsetenv("SSH_AUTH_SOCK");
    unsetenv("SSH_AGENT_PID");

    return 0;
}

static int session_teardown(void **state)
{
    struct torture_state *s = *state;

    ssh_free(s->ssh.session);
    s->ssh.session = NULL;

    return 0;
}

/* Check that the default limits for rekeying are enforced.
 * the limits are too high for testsuite to verify so
 * we should be fine with checking the values in internal
 * structures
 */
static void torture_rekey_default(void **state)
{
    struct torture_state *s = *state;
    int rc;
    struct ssh_crypto_struct *c = NULL;

    /* Define preferred ciphers: */
    if (ssh_fips_mode()) {
        /* We do not have any FIPS allowed cipher with different block size */
        rc = ssh_options_set(s->ssh.session, SSH_OPTIONS_CIPHERS_C_S,
                             "aes128-gcm@openssh.com");
    } else {
        /* (out) C->S has 8B block */
        rc = ssh_options_set(s->ssh.session, SSH_OPTIONS_CIPHERS_C_S,
                             "chacha20-poly1305@openssh.com");
    }
    assert_ssh_return_code(s->ssh.session, rc);
    /* (in) S->C has 16B block */
    rc = ssh_options_set(s->ssh.session, SSH_OPTIONS_CIPHERS_S_C,
                         "aes128-cbc");
    assert_ssh_return_code(s->ssh.session, rc);

    rc = ssh_connect(s->ssh.session);
    assert_ssh_return_code(s->ssh.session, rc);

    c = s->ssh.session->current_crypto;
    /* The blocks limit is set correctly */
    /* For S->C (in) we have 16B block => 2**(L/4) blocks */
    assert_int_equal(c->in_cipher->max_blocks,
                     (uint64_t)1 << (2 * c->in_cipher->blocksize));
    if (ssh_fips_mode()) {
        /* We do not have any FIPS allowed cipher with different block size */
        assert_int_equal(c->in_cipher->max_blocks,
                         (uint64_t)1 << (2 * c->in_cipher->blocksize));
    } else {
        /* The C->S (out) we have 8B block => 1 GB limit */
        assert_int_equal(c->out_cipher->max_blocks,
                         ((uint64_t)1 << 30) / c->out_cipher->blocksize);
    }

    ssh_disconnect(s->ssh.session);
}

static void sanity_check_session_size(void **state, uint64_t rekey_limit)
{
    struct torture_state *s = *state;
    struct ssh_crypto_struct *c = NULL;

    c = s->ssh.session->current_crypto;
    assert_non_null(c);
    assert_int_equal(c->in_cipher->max_blocks,
                     rekey_limit / c->in_cipher->blocksize);
    assert_int_equal(c->out_cipher->max_blocks,
                     rekey_limit / c->out_cipher->blocksize);
    /* when strict kex is used, the newkeys reset the sequence number */
    if ((s->ssh.session->flags & SSH_SESSION_FLAG_KEX_STRICT) != 0) {
        assert_int_equal(c->out_cipher->packets, s->ssh.session->send_seq);
        assert_int_equal(c->in_cipher->packets, s->ssh.session->recv_seq);
    } else {
        /* Otherwise we have less encrypted packets than transferred
         * (first are not encrypted) */
        assert_true(c->out_cipher->packets < s->ssh.session->send_seq);
        assert_true(c->in_cipher->packets < s->ssh.session->recv_seq);
    }
}
static void sanity_check_session(void **state)
{
    sanity_check_session_size(state, bytes);
}

/* We lower the rekey limits manually and check that the rekey
 * really happens when sending data
 */
static void torture_rekey_send(void **state)
{
    struct torture_state *s = *state;
    int rc;
    char data[256];
    unsigned int i;
    struct ssh_crypto_struct *c = NULL;
    unsigned char *secret_hash = NULL;

    rc = ssh_options_set(s->ssh.session, SSH_OPTIONS_REKEY_DATA, &bytes);
    assert_ssh_return_code(s->ssh.session, rc);

    rc = ssh_connect(s->ssh.session);
    assert_ssh_return_code(s->ssh.session, rc);

    sanity_check_session(state);
    /* Copy the initial secret hash = session_id so we know we changed keys later */
    c = s->ssh.session->current_crypto;
    assert_non_null(c);
    secret_hash = malloc(c->digest_len);
    assert_non_null(secret_hash);
    memcpy(secret_hash, c->secret_hash, c->digest_len);

    /* OpenSSH can not rekey before authentication so authenticate here */
    rc = ssh_userauth_none(s->ssh.session, NULL);
    /* This request should return a SSH_REQUEST_DENIED error */
    if (rc == SSH_ERROR) {
        assert_int_equal(ssh_get_error_code(s->ssh.session), SSH_REQUEST_DENIED);
    }
    rc = ssh_userauth_list(s->ssh.session, NULL);
    assert_true(rc & SSH_AUTH_METHOD_PUBLICKEY);

    rc = ssh_userauth_publickey_auto(s->ssh.session, NULL, NULL);
    assert_int_equal(rc, SSH_AUTH_SUCCESS);

    /* send ignore packets of up to 1KB to trigger rekey. Send little bit more
     * to make sure it completes with all different ciphers */
    memset(data, 0, sizeof(data));
    memset(data, 'A', 128);
    for (i = 0; i < KEX_RETRY; i++) {
        ssh_send_ignore(s->ssh.session, data);
        ssh_handle_packets(s->ssh.session, 50);
    }

    /* The rekey limit was restored in the new crypto to the same value */
    c = s->ssh.session->current_crypto;
    assert_int_equal(c->in_cipher->max_blocks, bytes / c->in_cipher->blocksize);
    assert_int_equal(c->out_cipher->max_blocks, bytes / c->out_cipher->blocksize);
    /* Check that the secret hash is different than initially */
    assert_memory_not_equal(secret_hash, c->secret_hash, c->digest_len);
    free(secret_hash);

    ssh_disconnect(s->ssh.session);
}

#ifdef WITH_SFTP
static void session_setup_sftp(void **state)
{
    struct torture_state *s = *state;
    int rc;

    rc = ssh_connect(s->ssh.session);
    assert_ssh_return_code(s->ssh.session, rc);

    /* OpenSSH can not rekey before authentication so authenticate here */
    rc = ssh_userauth_none(s->ssh.session, NULL);
    /* This request should return a SSH_REQUEST_DENIED error */
    if (rc == SSH_ERROR) {
        assert_int_equal(ssh_get_error_code(s->ssh.session), SSH_REQUEST_DENIED);
    }
    rc = ssh_userauth_list(s->ssh.session, NULL);
    assert_true(rc & SSH_AUTH_METHOD_PUBLICKEY);

    rc = ssh_userauth_publickey_auto(s->ssh.session, NULL, NULL);
    assert_int_equal(rc, SSH_AUTH_SUCCESS);

    /* Initialize SFTP session */
    s->ssh.tsftp = torture_sftp_session(s->ssh.session);
    assert_non_null(s->ssh.tsftp);
}

static int session_setup_sftp_client(void **state)
{
    struct torture_state *s = *state;
    int rc;

    session_setup(state);

    rc = ssh_options_set(s->ssh.session, SSH_OPTIONS_REKEY_DATA, &bytes);
    assert_ssh_return_code(s->ssh.session, rc);

    session_setup_sftp(state);

    return 0;
}

#define MAX_XFER_BUF_SIZE 16384

/* To trigger rekey by receiving data, the easiest thing is probably to
 * use sftp
 */
static void torture_rekey_recv_size(void **state, uint64_t rekey_limit)
{
    struct torture_state *s = *state;
    struct ssh_crypto_struct *c = NULL;
    unsigned char *secret_hash = NULL;

    char libssh_tmp_file[] = "/tmp/libssh_sftp_test_XXXXXX";
    char buf[MAX_XFER_BUF_SIZE];
    ssize_t bytesread;
    ssize_t byteswritten;
    int fd;
    sftp_file file;
    mode_t mask;
    int rc;

    sanity_check_session_size(state, rekey_limit);
    /* Copy the initial secret hash = session_id so we know we changed keys later */
    c = s->ssh.session->current_crypto;
    assert_non_null(c);
    secret_hash = malloc(c->digest_len);
    assert_non_null(secret_hash);
    memcpy(secret_hash, c->secret_hash, c->digest_len);

    /* Download a file */
    file = sftp_open(s->ssh.tsftp->sftp, SSH_EXECUTABLE, O_RDONLY, 0);
    assert_non_null(file);

    mask = umask(S_IRWXO | S_IRWXG);
    fd = mkstemp(libssh_tmp_file);
    umask(mask);
    unlink(libssh_tmp_file);

    for (;;) {
        bytesread = sftp_read(file, buf, MAX_XFER_BUF_SIZE);
        if (bytesread == 0) {
                break; /* EOF */
        }
        assert_false(bytesread < 0);

        byteswritten = write(fd, buf, bytesread);
        assert_int_equal(byteswritten, bytesread);
    }

    rc = sftp_close(file);
    assert_int_equal(rc, SSH_NO_ERROR);
    close(fd);

    /* The rekey limit was restored in the new crypto to the same value */
    c = s->ssh.session->current_crypto;
    assert_int_equal(c->in_cipher->max_blocks,
                     rekey_limit / c->in_cipher->blocksize);
    assert_int_equal(c->out_cipher->max_blocks,
                     rekey_limit / c->out_cipher->blocksize);
    /* Check that the secret hash is different than initially */
    assert_memory_not_equal(secret_hash, c->secret_hash, c->digest_len);
    free(secret_hash);

    torture_sftp_close(s->ssh.tsftp);
    ssh_disconnect(s->ssh.session);
}

static void torture_rekey_recv(void **state)
{
    torture_rekey_recv_size(state, bytes);
}
#endif /* WITH_SFTP */

/* Rekey time requires rekey after specified time and is off by default.
 * Setting the time to small enough value and waiting, we should trigger
 * rekey on the first sent packet afterward.
 */
static void torture_rekey_time(void **state)
{
    struct torture_state *s = *state;
    int rc;
    char data[256];
    unsigned int i;
    uint32_t time = 3; /* 3 seconds */
    struct ssh_crypto_struct *c = NULL;
    unsigned char *secret_hash = NULL;

    rc = ssh_options_set(s->ssh.session, SSH_OPTIONS_REKEY_TIME, &time);
    assert_ssh_return_code(s->ssh.session, rc);
    /* The time is internally stored in microseconds */
    assert_int_equal(time * 1000, s->ssh.session->opts.rekey_time);

    rc = ssh_connect(s->ssh.session);
    assert_ssh_return_code(s->ssh.session, rc);

    /* Copy the initial secret hash = session_id so we know we changed keys later */
    c = s->ssh.session->current_crypto;
    secret_hash = malloc(c->digest_len);
    assert_non_null(secret_hash);
    memcpy(secret_hash, c->secret_hash, c->digest_len);

    /* OpenSSH can not rekey before authentication so authenticate here */
    rc = ssh_userauth_none(s->ssh.session, NULL);
    /* This request should return a SSH_REQUEST_DENIED error */
    if (rc == SSH_ERROR) {
        assert_int_equal(ssh_get_error_code(s->ssh.session), SSH_REQUEST_DENIED);
    }
    rc = ssh_userauth_list(s->ssh.session, NULL);
    assert_true(rc & SSH_AUTH_METHOD_PUBLICKEY);

    rc = ssh_userauth_publickey_auto(s->ssh.session, NULL, NULL);
    assert_int_equal(rc, SSH_AUTH_SUCCESS);

    /* Send some data. This should not trigger rekey yet */
    memset(data, 0, sizeof(data));
    memset(data, 'A', 8);
    for (i = 0; i < 3; i++) {
        ssh_send_ignore(s->ssh.session, data);
        ssh_handle_packets(s->ssh.session, 50);
    }

    /* Check that the secret hash is the same */
    c = s->ssh.session->current_crypto;
    assert_memory_equal(secret_hash, c->secret_hash, c->digest_len);

    /* Wait some more time */
    sleep(3);

    /* send some more data to trigger rekey and handle the
     * key exchange "in background" */
    for (i = 0; i < 8; i++) {
        ssh_send_ignore(s->ssh.session, data);
        ssh_handle_packets(s->ssh.session, 50);
    }

    /* Check that the secret hash is different than initially */
    c = s->ssh.session->current_crypto;
    assert_memory_not_equal(secret_hash, c->secret_hash, c->digest_len);
    free(secret_hash);

    ssh_disconnect(s->ssh.session);
}

/* We lower the rekey limits manually and check that the rekey
 * really happens when sending data
 */
static void torture_rekey_server_send(void **state)
{
    struct torture_state *s = *state;
    int rc;
    char data[256];
    unsigned int i;
    struct ssh_crypto_struct *c = NULL;
    unsigned char *secret_hash = NULL;
    const char *sshd_config = "RekeyLimit 2K none";

    torture_update_sshd_config(state, sshd_config);

    rc = ssh_connect(s->ssh.session);
    assert_ssh_return_code(s->ssh.session, rc);

    /* Copy the initial secret hash = session_id so we know we changed keys later */
    c = s->ssh.session->current_crypto;
    secret_hash = malloc(c->digest_len);
    assert_non_null(secret_hash);
    memcpy(secret_hash, c->secret_hash, c->digest_len);

    /* OpenSSH can not rekey before authentication so authenticate here */
    rc = ssh_userauth_none(s->ssh.session, NULL);
    /* This request should return a SSH_REQUEST_DENIED error */
    if (rc == SSH_ERROR) {
        assert_int_equal(ssh_get_error_code(s->ssh.session), SSH_REQUEST_DENIED);
    }
    rc = ssh_userauth_list(s->ssh.session, NULL);
    assert_true(rc & SSH_AUTH_METHOD_PUBLICKEY);

    rc = ssh_userauth_publickey_auto(s->ssh.session, NULL, NULL);
    assert_int_equal(rc, SSH_AUTH_SUCCESS);

    /* send ignore packets of up to 1KB to trigger rekey */
    memset(data, 0, sizeof(data));
    memset(data, 'A', 128);
    for (i = 0; i < 20; i++) {
        ssh_send_ignore(s->ssh.session, data);
        ssh_handle_packets(s->ssh.session, 50);
    }

    /* Check that the secret hash is different than initially */
    c = s->ssh.session->current_crypto;
    assert_memory_not_equal(secret_hash, c->secret_hash, c->digest_len);
    free(secret_hash);

    ssh_disconnect(s->ssh.session);
}

static void torture_rekey_different_kex(void **state)
{
    struct torture_state *s = *state;
    int rc;
    char data[256];
    unsigned int i;
    struct ssh_crypto_struct *c = NULL;
    unsigned char *secret_hash = NULL;
    size_t secret_hash_len = 0;
    const char *kex1 = "diffie-hellman-group14-sha256,curve25519-sha256,ecdh-sha2-nistp256";
    const char *kex2 = "diffie-hellman-group18-sha512,diffie-hellman-group16-sha512,ecdh-sha2-nistp521";

    /* Use short digest for initial key exchange */
    rc = ssh_options_set(s->ssh.session, SSH_OPTIONS_KEY_EXCHANGE, kex1);
    assert_ssh_return_code(s->ssh.session, rc);

    rc = ssh_options_set(s->ssh.session, SSH_OPTIONS_REKEY_DATA, &bytes);
    assert_ssh_return_code(s->ssh.session, rc);

    rc = ssh_connect(s->ssh.session);
    assert_ssh_return_code(s->ssh.session, rc);

    /* The blocks limit is set correctly */
    sanity_check_session(state);
    /* Copy the initial secret hash = session_id so we know we changed keys later */
    c = s->ssh.session->current_crypto;
    assert_non_null(c);
    secret_hash = malloc(c->digest_len);
    assert_non_null(secret_hash);
    memcpy(secret_hash, c->secret_hash, c->digest_len);
    secret_hash_len = c->digest_len;
    assert_int_equal(secret_hash_len, 32); /* SHA256 len */

    /* OpenSSH can not rekey before authentication so authenticate here */
    rc = ssh_userauth_none(s->ssh.session, NULL);
    /* This request should return a SSH_REQUEST_DENIED error */
    if (rc == SSH_ERROR) {
        assert_int_equal(ssh_get_error_code(s->ssh.session), SSH_REQUEST_DENIED);
    }
    rc = ssh_userauth_list(s->ssh.session, NULL);
    assert_true(rc & SSH_AUTH_METHOD_PUBLICKEY);

    rc = ssh_userauth_publickey_auto(s->ssh.session, NULL, NULL);
    assert_int_equal(rc, SSH_AUTH_SUCCESS);

    /* Now try to change preference of key exchange algorithm to something with larger digest */
    rc = ssh_options_set(s->ssh.session, SSH_OPTIONS_KEY_EXCHANGE, kex2);
    assert_ssh_return_code(s->ssh.session, rc);

    /* send ignore packets of up to 1KB to trigger rekey. Send little bit more
     * to make sure the rekey it completes with all different ciphers (paddings */
    memset(data, 0, sizeof(data));
    memset(data, 'A', 128);
    for (i = 0; i < KEX_RETRY; i++) {
        ssh_send_ignore(s->ssh.session, data);
        ssh_handle_packets(s->ssh.session, 1000);

        c = s->ssh.session->current_crypto;
        /* SHA256 len */
        if (c->digest_len != 32) {
            break;
        }
    }

    /* The rekey limit was restored in the new crypto to the same value */
    c = s->ssh.session->current_crypto;
    assert_int_equal(c->in_cipher->max_blocks, bytes / c->in_cipher->blocksize);
    assert_int_equal(c->out_cipher->max_blocks, bytes / c->out_cipher->blocksize);
    /* Check that the secret hash is different than initially */
    assert_int_equal(c->digest_len, 64); /* SHA512 len */
    assert_memory_not_equal(secret_hash, c->secret_hash, secret_hash_len);
    /* Session ID stays same after one rekey */
    assert_memory_equal(secret_hash, c->session_id, secret_hash_len);
    free(secret_hash);

    assert_int_equal(ssh_is_connected(s->ssh.session), 1);
    assert_int_equal(s->ssh.session->session_state, SSH_SESSION_STATE_AUTHENTICATED);

    ssh_disconnect(s->ssh.session);
}

static void torture_rekey_server_different_kex(void **state)
{
    struct torture_state *s = *state;
    int rc;
    char data[256];
    unsigned int i;
    struct ssh_crypto_struct *c = NULL;
    unsigned char *secret_hash = NULL;
    size_t secret_hash_len = 0;
    const char *sshd_config = "RekeyLimit 2K none";
    const char *kex1 = "diffie-hellman-group14-sha256,curve25519-sha256,ecdh-sha2-nistp256";
    const char *kex2 = "diffie-hellman-group18-sha512,diffie-hellman-group16-sha512";

    /* Use short digest for initial key exchange */
    rc = ssh_options_set(s->ssh.session, SSH_OPTIONS_KEY_EXCHANGE, kex1);
    assert_ssh_return_code(s->ssh.session, rc);

    torture_update_sshd_config(state, sshd_config);

    rc = ssh_connect(s->ssh.session);
    assert_ssh_return_code(s->ssh.session, rc);

    /* Copy the initial secret hash = session_id so we know we changed keys later */
    c = s->ssh.session->current_crypto;
    secret_hash = malloc(c->digest_len);
    assert_non_null(secret_hash);
    memcpy(secret_hash, c->secret_hash, c->digest_len);
    secret_hash_len = c->digest_len;
    assert_int_equal(secret_hash_len, 32); /* SHA256 len */

    /* OpenSSH can not rekey before authentication so authenticate here */
    rc = ssh_userauth_none(s->ssh.session, NULL);
    /* This request should return a SSH_REQUEST_DENIED error */
    if (rc == SSH_ERROR) {
        assert_int_equal(ssh_get_error_code(s->ssh.session), SSH_REQUEST_DENIED);
    }
    rc = ssh_userauth_list(s->ssh.session, NULL);
    assert_true(rc & SSH_AUTH_METHOD_PUBLICKEY);

    rc = ssh_userauth_publickey_auto(s->ssh.session, NULL, NULL);
    assert_int_equal(rc, SSH_AUTH_SUCCESS);

    /* Now try to change preference of key exchange algorithm to something with larger digest */
    rc = ssh_options_set(s->ssh.session, SSH_OPTIONS_KEY_EXCHANGE, kex2);
    assert_ssh_return_code(s->ssh.session, rc);

    /* send ignore packets of up to 1KB to trigger rekey. Send little bit more
     * to make sure the rekey it completes with all different ciphers (paddings */
    memset(data, 0, sizeof(data));
    memset(data, 'A', 128);
    for (i = 0; i < KEX_RETRY; i++) {
        ssh_send_ignore(s->ssh.session, data);
        ssh_handle_packets(s->ssh.session, 1000);

        c = s->ssh.session->current_crypto;
        /* SHA256 len */
        if (c->digest_len != 32) {
            break;
        }
    }

    /* Check that the secret hash is different than initially */
    c = s->ssh.session->current_crypto;
    assert_int_equal(c->digest_len, 64); /* SHA512 len */
    assert_memory_not_equal(secret_hash, c->secret_hash, secret_hash_len);
    /* Session ID stays same after one rekey */
    assert_memory_equal(secret_hash, c->session_id, secret_hash_len);
    free(secret_hash);

    ssh_disconnect(s->ssh.session);
}


#ifdef WITH_SFTP
static int session_setup_sftp_server(void **state)
{
    const char *sshd_config = "RekeyLimit 2K none";

    session_setup(state);

    torture_update_sshd_config(state, sshd_config);

    session_setup_sftp(state);

    return 0;
}

static void torture_rekey_server_recv(void **state)
{
    struct torture_state *s = *state;
    struct ssh_crypto_struct *c = NULL;
    unsigned char *secret_hash = NULL;
    char libssh_tmp_file[] = "/tmp/libssh_sftp_test_XXXXXX";
    char buf[MAX_XFER_BUF_SIZE];
    ssize_t bytesread;
    ssize_t byteswritten;
    int fd;
    sftp_file file;
    mode_t mask;
    int rc;

    /* Copy the initial secret hash = session_id so we know we changed keys later */
    c = s->ssh.session->current_crypto;
    secret_hash = malloc(c->digest_len);
    assert_non_null(secret_hash);
    memcpy(secret_hash, c->secret_hash, c->digest_len);

    /* Download a file */
    file = sftp_open(s->ssh.tsftp->sftp, SSH_EXECUTABLE, O_RDONLY, 0);
    assert_non_null(file);

    mask = umask(S_IRWXO | S_IRWXG);
    fd = mkstemp(libssh_tmp_file);
    umask(mask);
    unlink(libssh_tmp_file);

    for (;;) {
        bytesread = sftp_read(file, buf, MAX_XFER_BUF_SIZE);
        if (bytesread == 0) {
                break; /* EOF */
        }
        assert_false(bytesread < 0);

        byteswritten = write(fd, buf, bytesread);
        assert_int_equal(byteswritten, bytesread);
    }

    rc = sftp_close(file);
    assert_int_equal(rc, SSH_NO_ERROR);
    close(fd);

    /* Check that the secret hash is different than initially */
    c = s->ssh.session->current_crypto;
    assert_memory_not_equal(secret_hash, c->secret_hash, c->digest_len);
    free(secret_hash);

    torture_sftp_close(s->ssh.tsftp);
    ssh_disconnect(s->ssh.session);
}
#endif /* WITH_SFTP */

#ifdef WITH_ZLIB
/* This is disabled by OpenSSH since OpenSSH 7.4p1 */
#if (OPENSSH_VERSION_MAJOR == 7 && OPENSSH_VERSION_MINOR < 4) || OPENSSH_VERSION_MAJOR < 7
/* Compression can be funky to get right after rekey
 */
static void torture_rekey_send_compression(void **state)
{
    struct torture_state *s = *state;
    const char *comp = "zlib";
    int rc;

    rc = ssh_options_set(s->ssh.session, SSH_OPTIONS_COMPRESSION_C_S, comp);
    assert_ssh_return_code(s->ssh.session, rc);

    rc = ssh_options_set(s->ssh.session, SSH_OPTIONS_COMPRESSION_S_C, comp);
    assert_ssh_return_code(s->ssh.session, rc);

    torture_rekey_send(state);
}

#ifdef WITH_SFTP
static void torture_rekey_recv_compression(void **state)
{
    struct torture_state *s = *state;
    const char *comp = "zlib";
    int rc;

    rc = ssh_options_set(s->ssh.session, SSH_OPTIONS_COMPRESSION_C_S, comp);
    assert_ssh_return_code(s->ssh.session, rc);

    rc = ssh_options_set(s->ssh.session, SSH_OPTIONS_COMPRESSION_S_C, comp);
    assert_ssh_return_code(s->ssh.session, rc);

    torture_rekey_recv(state);
}
#endif /* WITH_SFTP */
#endif

/* Especially the delayed compression by openssh.
 */
static void torture_rekey_send_compression_delayed(void **state)
{
    struct torture_state *s = *state;
    const char *comp = "zlib@openssh.com";
    int rc;

    rc = ssh_options_set(s->ssh.session, SSH_OPTIONS_COMPRESSION_C_S, comp);
    assert_ssh_return_code(s->ssh.session, rc);

    rc = ssh_options_set(s->ssh.session, SSH_OPTIONS_COMPRESSION_S_C, comp);
    assert_ssh_return_code(s->ssh.session, rc);

    torture_rekey_send(state);
}

#ifdef WITH_SFTP
static void torture_rekey_recv_compression_delayed(void **state)
{
    struct torture_state *s = *state;
    const char *comp = "zlib@openssh.com";
    int rc;

    rc = ssh_options_set(s->ssh.session, SSH_OPTIONS_COMPRESSION_C_S, comp);
    assert_ssh_return_code(s->ssh.session, rc);

    rc = ssh_options_set(s->ssh.session, SSH_OPTIONS_COMPRESSION_S_C, comp);
    assert_ssh_return_code(s->ssh.session, rc);

    torture_rekey_recv(state);
}
#endif /* WITH_SFTP */
#endif /* WITH_ZLIB */

static void setup_server_for_good_guess(void *state)
{
    const char *default_sshd_config = "KexAlgorithms curve25519-sha256";
    const char *fips_sshd_config = "KexAlgorithms ecdh-sha2-nistp256";
    const char *sshd_config = default_sshd_config;

    if (ssh_fips_mode()) {
        sshd_config = fips_sshd_config;
    }
    /* This sets an only supported kex algorithm that we do not have as a first
    * option */
    torture_update_sshd_config(state, sshd_config);
}

static void torture_rekey_guess_send(void **state)
{
    struct torture_state *s = *state;

    setup_server_for_good_guess(state);

    /* Make the client send the first_kex_packet_follows flag during key
     * exchange as well as during the rekey */
    s->ssh.session->send_first_kex_follows = true;

    torture_rekey_send(state);
}

static void torture_rekey_guess_wrong_send(void **state)
{
    struct torture_state *s = *state;
    const char *sshd_config = "KexAlgorithms diffie-hellman-group14-sha256";

    /* This sets an only supported kex algorithm that we do not have as a first
     * option */
    torture_update_sshd_config(state, sshd_config);

    /* Make the client send the first_kex_packet_follows flag during key
     * exchange as well as during the rekey */
    s->ssh.session->send_first_kex_follows = true;

    torture_rekey_send(state);
}

#ifdef WITH_SFTP
static void torture_rekey_guess_recv(void **state)
{
    struct torture_state *s = *state;
    int rc;

    setup_server_for_good_guess(state);

    /* Make the client send the first_kex_packet_follows flag during key
     * exchange as well as during the rekey */
    s->ssh.session->send_first_kex_follows = true;

    rc = ssh_options_set(s->ssh.session, SSH_OPTIONS_REKEY_DATA, &bytes);
    assert_ssh_return_code(s->ssh.session, rc);

    session_setup_sftp(state);

    torture_rekey_recv(state);
}

static void torture_rekey_guess_wrong_recv(void **state)
{
    struct torture_state *s = *state;
    const char *sshd_config = "KexAlgorithms diffie-hellman-group14-sha256";
    int rc;

    /* This sets an only supported kex algorithm that we do not have as a first
     * option */
    torture_update_sshd_config(state, sshd_config);

    /* Make the client send the first_kex_packet_follows flag during key
     * exchange as well as during the rekey */
    s->ssh.session->send_first_kex_follows = true;

    rc = ssh_options_set(s->ssh.session, SSH_OPTIONS_REKEY_DATA, &bytes);
    assert_ssh_return_code(s->ssh.session, rc);

    session_setup_sftp(state);

    torture_rekey_recv(state);
}

static void torture_rekey_guess_all_combinations(void **state)
{
    struct torture_state *s = *state;
    char sshd_config[256] = "";
    char client_kex[256] = "";
    const char *supported = NULL;
    struct ssh_tokens_st *s_tok = NULL;
    uint64_t rekey_limit = 0;
    int rc, i, j;

    /* The rekey limit is 1/2 of the transferred file size so we will likely get
     * 2 rekeys per test, which still runs for acceptable time */
    rekey_limit = atoll(SSH_EXECUTABLE_SIZE);
    rekey_limit /= 2;

    if (ssh_fips_mode()) {
        supported = ssh_kex_get_fips_methods(SSH_KEX);
    } else {
        supported = ssh_kex_get_supported_method(SSH_KEX);
    }
    assert_non_null(supported);

    s_tok = ssh_tokenize(supported, ',');
    assert_non_null(s_tok);
    for (i = 0; s_tok->tokens[i]; i++) {
        /* Skip algorithms not supported by the OpenSSH server */
        if (strstr(OPENSSH_KEX, s_tok->tokens[i]) == NULL) {
            SSH_LOG(SSH_LOG_INFO, "Server: %s [skipping]", s_tok->tokens[i]);
            continue;
        }
        SSH_LOG(SSH_LOG_INFO, "Server: %s", s_tok->tokens[i]);
        snprintf(sshd_config,
                 sizeof(sshd_config),
                 "KexAlgorithms %s",
                 s_tok->tokens[i]);
        /* This sets an only supported kex algorithm that we do not have as
         * a first option in the client */
        torture_update_sshd_config(state, sshd_config);

        for (j = 0; s_tok->tokens[j]; j++) {
            if (i == j) {
                continue;
            }

            session_setup(state);
            /* Make the client send the first_kex_packet_follows flag during key
             * exchange as well as during the rekey */
            s->ssh.session->send_first_kex_follows = true;

            rc = ssh_options_set(s->ssh.session,
                                 SSH_OPTIONS_REKEY_DATA,
                                 &rekey_limit);
            assert_ssh_return_code(s->ssh.session, rc);

            /* Client kex preference will have the second of the pair and the
             * server one as a second to negotiate on the second attempt */
            snprintf(client_kex,
                     sizeof(client_kex),
                     "%s,%s",
                     s_tok->tokens[j],
                     s_tok->tokens[i]);
            SSH_LOG(SSH_LOG_INFO, "Client: %s", client_kex);
            rc = ssh_options_set(s->ssh.session,
                                 SSH_OPTIONS_KEY_EXCHANGE,
                                 client_kex);
            assert_ssh_return_code(s->ssh.session, rc);
            session_setup_sftp(state);
            torture_rekey_recv_size(state, rekey_limit);
            session_teardown(state);
        }
    }

    ssh_tokens_free(s_tok);
}
#endif /* WITH_SFTP */

int torture_run_tests(void) {
    int rc;
    struct CMUnitTest tests[] = {
        cmocka_unit_test_setup_teardown(torture_rekey_default,
                                        session_setup,
                                        session_teardown),
        cmocka_unit_test_setup_teardown(torture_rekey_time,
                                        session_setup,
                                        session_teardown),
#ifdef WITH_SFTP
        cmocka_unit_test_setup_teardown(torture_rekey_recv,
                                        session_setup_sftp_client,
                                        session_teardown),
#endif /* WITH_SFTP */
        cmocka_unit_test_setup_teardown(torture_rekey_send,
                                        session_setup,
                                        session_teardown),
        cmocka_unit_test_setup_teardown(torture_rekey_different_kex,
                                        session_setup,
                                        session_teardown),
#ifdef WITH_ZLIB
#if (OPENSSH_VERSION_MAJOR == 7 && OPENSSH_VERSION_MINOR < 4) || OPENSSH_VERSION_MAJOR < 7
        cmocka_unit_test_setup_teardown(torture_rekey_send_compression,
                                        session_setup,
                                        session_teardown),
#ifdef WITH_SFTP
        cmocka_unit_test_setup_teardown(torture_rekey_recv_compression,
                                        session_setup_sftp_client,
                                        session_teardown),
#endif /* WITH_SFTP */
#endif
        cmocka_unit_test_setup_teardown(torture_rekey_send_compression_delayed,
                                        session_setup,
                                        session_teardown),
#ifdef WITH_SFTP
        cmocka_unit_test_setup_teardown(torture_rekey_recv_compression_delayed,
                                        session_setup_sftp_client,
                                        session_teardown),
#endif /* WITH_SFTP */
#endif /* WITH_ZLIB */
        /* TODO verify the two rekey are possible and the states are not broken after rekey */

        cmocka_unit_test_setup_teardown(torture_rekey_server_different_kex,
                                        session_setup,
                                        session_teardown),
        /* Note, that these tests modify the sshd_config so follow-up tests
         * might get unexpected behavior if they do not update the server with
         * torture_update_sshd_config() too */
        cmocka_unit_test_setup_teardown(torture_rekey_server_send,
                                        session_setup,
                                        session_teardown),
        cmocka_unit_test_setup_teardown(torture_rekey_guess_send,
                                        session_setup,
                                        session_teardown),
        cmocka_unit_test_setup_teardown(torture_rekey_guess_wrong_send,
                                        session_setup,
                                        session_teardown),
#ifdef WITH_SFTP
        cmocka_unit_test_setup_teardown(torture_rekey_server_recv,
                                        session_setup_sftp_server,
                                        session_teardown),
        cmocka_unit_test_setup_teardown(torture_rekey_guess_recv,
                                        session_setup,
                                        session_teardown),
        cmocka_unit_test_setup_teardown(torture_rekey_guess_wrong_recv,
                                        session_setup,
                                        session_teardown),
        cmocka_unit_test(torture_rekey_guess_all_combinations),
#endif /* WITH_SFTP */
    };

    ssh_init();

    torture_filter_tests(tests);
    rc = cmocka_run_group_tests(tests, sshd_setup, sshd_teardown);

    ssh_finalize();

    return rc;
}
