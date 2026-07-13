/*
 * This file is part of the SSH Library
 *
 * Copyright (c) 2012 by Aris Adamantiadis
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
#include "libssh/libssh.h"
#include "libssh/priv.h"
#include "libssh/session.h"

#include <sys/types.h>
#include <pwd.h>
#include <errno.h>

#define BUFLEN 4096
static char buffer[BUFLEN];

static int sshd_setup(void **state)
{
    torture_setup_sshd_server(state, false);

    return 0;
}

static int sshd_teardown(void **state) {
    torture_teardown_sshd_server(state);

    return 0;
}

static int session_setup(void **state)
{
    struct torture_state *s = *state;
    struct passwd *pwd;
    int rc;

    pwd = getpwnam("bob");
    assert_non_null(pwd);

    rc = setuid(pwd->pw_uid);
    assert_return_code(rc, errno);

    s->ssh.session = torture_ssh_session(s,
                                         TORTURE_SSH_SERVER,
                                         NULL,
                                         TORTURE_SSH_USER_ALICE,
                                         NULL);
    assert_non_null(s->ssh.session);

    return 0;
}

static int session_teardown(void **state)
{
    struct torture_state *s = *state;

    ssh_disconnect(s->ssh.session);
    ssh_free(s->ssh.session);

    return 0;
}

static void torture_channel_read_error(void **state) {
    struct torture_state *s = *state;
    ssh_session session = s->ssh.session;
    ssh_channel channel;
    int rc;
    int fd;
    int i;

    channel = ssh_channel_new(session);
    assert_non_null(channel);

    rc = ssh_channel_open_session(channel);
    assert_ssh_return_code(session, rc);

    rc = ssh_channel_request_exec(channel, "hexdump -C /dev/urandom");
    assert_ssh_return_code(session, rc);

    /* send crap and for server to send us a disconnect */
    fd = ssh_get_fd(session);
    assert_true(fd > 2);
    rc = write(fd, "AAAA", 4);
    assert_int_equal(rc, 4);

    for (i=0;i<20;++i){
        rc = ssh_channel_read(channel,buffer,sizeof(buffer),0);
        if (rc == SSH_ERROR)
            break;
    }
#if OPENSSH_VERSION_MAJOR == 6 && OPENSSH_VERSION_MINOR >= 7
    /* With openssh 6.7 this doesn't produce and error anymore */
    assert_ssh_return_code(session, rc);
#else
    assert_ssh_return_code_equal(session, rc, SSH_ERROR);
#endif

    ssh_channel_free(channel);
}

static void torture_channel_poll_timeout_valid(void **state) {
    struct torture_state *s = *state;
    ssh_session session = s->ssh.session;
    ssh_channel channel;
    int rc;

    channel = ssh_channel_new(session);
    assert_non_null(channel);

    rc = ssh_channel_open_session(channel);
    assert_ssh_return_code(session, rc);

    rc = ssh_channel_request_exec(channel, "echo -n ABCD");
    assert_ssh_return_code(session, rc);

    rc = ssh_channel_poll_timeout(channel, 500, 0);
    assert_int_equal(rc, strlen("ABCD"));
}

static void torture_channel_poll_timeout(void **state) {
    struct torture_state *s = *state;
    ssh_session session = s->ssh.session;
    ssh_channel channel;
    int rc;
    int fd;

    channel = ssh_channel_new(session);
    assert_non_null(channel);

    rc = ssh_channel_open_session(channel);
    assert_ssh_return_code(session, rc);

    fd = ssh_get_fd(session);
    assert_true(fd > 2);

    rc = ssh_channel_poll_timeout(channel, 500, 0);
    assert_int_equal(rc, SSH_OK);

    /* send crap and for server to send us a disconnect */
    rc = write(fd, "AAAA", 4);
    assert_int_equal(rc, 4);

    rc = ssh_channel_poll_timeout(channel, 500, 0);
    assert_int_equal(rc, SSH_ERROR);

    ssh_channel_free(channel);
}

/*
 * Check that the client can properly handle the error returned from the server
 * when the maximum number of sessions is exceeded.
 *
 * Related: T75, T239
 *
 */
static void torture_max_sessions(void **state)
{
    struct torture_state *s = *state;
    ssh_session session = s->ssh.session;
    char max_session_config[32] = {0};
#define MAX_CHANNELS 10
    ssh_channel channels[MAX_CHANNELS + 1];
    size_t i;
    int rc;

    snprintf(max_session_config,
             sizeof(max_session_config),
             "MaxSessions %u",
             MAX_CHANNELS);

    /* Update server configuration to limit number of sessions */
    torture_update_sshd_config(state, max_session_config);

    /* Open the maximum number of channel sessions */
    for (i = 0; i < MAX_CHANNELS; i++) {
        channels[i] = ssh_channel_new(session);
        assert_non_null(channels[i]);

        rc = ssh_channel_open_session(channels[i]);
        assert_ssh_return_code(session, rc);
    }

    /* Try to open an extra session and expect failure */
    channels[i] = ssh_channel_new(session);
    assert_non_null(channels[i]);

    rc = ssh_channel_open_session(channels[i]);
    assert_int_equal(rc, SSH_ERROR);

    /* Free the unused channel */
    ssh_channel_free(channels[i]);

    /* Close and free channels */
    for (i = 0; i < MAX_CHANNELS; i++) {
        ssh_channel_close(channels[i]);
        ssh_channel_free(channels[i]);
    }
#undef MAX_CHANNELS
}

static void torture_no_more_sessions(void **state)
{
    struct torture_state *s = *state;
    ssh_session session = s->ssh.session;
    ssh_channel channels[2];
    int rc;

    /* Open a channel session */
    channels[0] = ssh_channel_new(session);
    assert_non_null(channels[0]);

    rc = ssh_channel_open_session(channels[0]);
    assert_ssh_return_code(session, rc);

    /* Send no-more-sessions@openssh.com global request */
    rc = ssh_request_no_more_sessions(session);
    assert_ssh_return_code(session, rc);

    /* Try to open an extra session and expect failure */
    channels[1] = ssh_channel_new(session);
    assert_non_null(channels[1]);

    rc = ssh_channel_open_session(channels[1]);
    assert_int_equal(rc, SSH_ERROR);

    /* Free the unused channel */
    ssh_channel_free(channels[1]);

    /* Close and free open channel */
    ssh_channel_close(channels[0]);
    ssh_channel_free(channels[0]);
}

static void torture_channel_delayed_close(void **state)
{
    struct torture_state *s = *state;
    ssh_session session = s->ssh.session;
    ssh_channel channel;

    char request[256];
    char buff[256] = {0};

    int rc;
    int fd;

    snprintf(request, 256,
             "dd if=/dev/urandom of=/tmp/file bs=64000 count=2; hexdump -C /tmp/file");

    channel = ssh_channel_new(session);
    assert_non_null(channel);

    rc = ssh_channel_open_session(channel);
    assert_ssh_return_code(session, rc);

    fd = ssh_get_fd(session);
    assert_true(fd > 2);

    /* Make the request, read parts with close */
    rc = ssh_channel_request_exec(channel, request);
    assert_ssh_return_code(session, rc);

    do {
        rc = ssh_channel_read(channel, buff, 256, 0);
    } while(rc > 0);
    assert_ssh_return_code(session, rc);

    rc = ssh_channel_poll_timeout(channel, 500, 0);
    assert_int_equal(rc, SSH_EOF);

    ssh_channel_free(channel);

}

/* Ensure that calling 'ssh_channel_poll' on a freed channel does not lead to
 * segmentation faults. */
static void torture_freed_channel_poll(void **state)
{
    struct torture_state *s = *state;
    ssh_session session = s->ssh.session;
    ssh_channel channel;

    char request[256];
    int rc;

    snprintf(request, 256,
             "dd if=/dev/urandom of=/tmp/file bs=64000 count=2; hexdump -C /tmp/file");

    channel = ssh_channel_new(session);
    assert_non_null(channel);

    rc = ssh_channel_open_session(channel);
    assert_ssh_return_code(session, rc);

    /* Make the request, read parts with close */
    rc = ssh_channel_request_exec(channel, request);
    assert_ssh_return_code(session, rc);

    ssh_channel_free(channel);

    rc = ssh_channel_poll(channel, 0);
    assert_int_equal(rc, SSH_ERROR);
}

/* Ensure that calling 'ssh_channel_poll_timeout' on a freed channel does not
 * lead to segmentation faults. */
static void torture_freed_channel_poll_timeout(void **state)
{
    struct torture_state *s = *state;
    ssh_session session = s->ssh.session;
    ssh_channel channel;
    bool channel_freed = false;
    char request[256];
    char buff[256] = {0};
    int rc;

    snprintf(request, 256,
             "dd if=/dev/urandom of=/tmp/file bs=64000 count=2; hexdump -C /tmp/file");

    channel = ssh_channel_new(session);
    assert_non_null(channel);

    rc = ssh_channel_open_session(channel);
    assert_ssh_return_code(session, rc);

    /* Make the request, read parts with close */
    rc = ssh_channel_request_exec(channel, request);
    assert_ssh_return_code(session, rc);

    do {
        rc = ssh_channel_read(channel, buff, 256, 0);
    } while(rc > 0);
    assert_ssh_return_code(session, rc);

    /* when either of these conditions is met the call to ssh_channel_free will
     * actually free the channel so calling poll on that channel will be
     * use-after-free */
    if ((channel->flags & SSH_CHANNEL_FLAG_CLOSED_REMOTE) ||
        (channel->flags & SSH_CHANNEL_FLAG_NOT_BOUND)) {
        channel_freed = true;
    }
    ssh_channel_free(channel);

    if (!channel_freed) {
        rc = ssh_channel_poll_timeout(channel, 500, 0);
        assert_int_equal(rc, SSH_ERROR);
    }
}

/* Ensure that calling 'ssh_channel_read_nonblocking' on a freed channel does
 * not lead to segmentation faults. */
static void torture_freed_channel_read_nonblocking(void **state)
{
    struct torture_state *s = *state;
    ssh_session session = s->ssh.session;
    ssh_channel channel;

    char request[256];
    char buff[256] = {0};
    int rc;

    snprintf(request, 256,
             "dd if=/dev/urandom of=/tmp/file bs=64000 count=2; hexdump -C /tmp/file");

    channel = ssh_channel_new(session);
    assert_non_null(channel);

    rc = ssh_channel_open_session(channel);
    assert_ssh_return_code(session, rc);

    /* Make the request, read parts with close */
    rc = ssh_channel_request_exec(channel, request);
    assert_ssh_return_code(session, rc);

    ssh_channel_free(channel);

    rc = ssh_channel_read_nonblocking(channel, buff, 256, 0);
    assert_ssh_return_code_equal(session, rc, SSH_ERROR);
}

static void torture_channel_exit_status(void **state)
{
    struct torture_state *s = *state;
    ssh_session session = s->ssh.session;
    ssh_channel channel = NULL;
    char request[256];
    uint32_t exit_status = (uint32_t)-1;
    int rc;

    rc = snprintf(request, sizeof(request), "true");
    assert_return_code(rc, errno);

    channel = ssh_channel_new(session);
    assert_non_null(channel);

    rc = ssh_channel_open_session(channel);
    assert_ssh_return_code(session, rc);

    /* Make the request, read parts with close */
    rc = ssh_channel_request_exec(channel, request);
    assert_ssh_return_code(session, rc);

    exit_status = ssh_channel_get_exit_state(channel, &exit_status, NULL, NULL);
    assert_ssh_return_code(session, rc);
    assert_int_equal(exit_status, 0);
}

static void torture_channel_exit_signal(void **state)
{
    struct torture_state *s = *state;
    ssh_session session = s->ssh.session;
    ssh_channel channel = NULL;
    char request[256];
    uint32_t exit_status = (uint32_t)-1;
    char *exit_signal = NULL;
    int core_dumped = false;
    int rc;

    rc = snprintf(request, sizeof(request), "cat");
    assert_return_code(rc, errno);

    channel = ssh_channel_new(session);
    assert_non_null(channel);

    rc = ssh_channel_open_session(channel);
    assert_ssh_return_code(session, rc);

    /* Make the request, read parts with close */
    rc = ssh_channel_request_exec(channel, request);
    assert_ssh_return_code(session, rc);
    rc = ssh_channel_request_send_signal(channel, "TERM");
    assert_ssh_return_code(session, rc);

    rc = ssh_channel_get_exit_state(channel,
                                    &exit_status,
                                    &exit_signal,
                                    &core_dumped);

    assert_ssh_return_code(session, rc);
    assert_int_equal(exit_status, (uint32_t)-1);
    assert_string_equal(exit_signal, "TERM");
    SAFE_FREE(exit_signal);
}


/* Ensure that calling 'ssh_channel_get_exit_status' on a freed channel does not
 * lead to segmentation faults. */
static void torture_freed_channel_get_exit_status(void **state)
{
    struct torture_state *s = *state;
    ssh_session session = s->ssh.session;
    ssh_channel channel;
    bool channel_freed = false;
    char request[256];
    char buff[256] = {0};
    int rc;

    snprintf(request, 256,
             "dd if=/dev/urandom of=/tmp/file bs=64000 count=2; hexdump -C /tmp/file");

    channel = ssh_channel_new(session);
    assert_non_null(channel);

    rc = ssh_channel_open_session(channel);
    assert_ssh_return_code(session, rc);

    /* Make the request, read parts with close */
    rc = ssh_channel_request_exec(channel, request);
    assert_ssh_return_code(session, rc);

    do {
        rc = ssh_channel_read(channel, buff, 256, 0);
    } while(rc > 0);
    assert_ssh_return_code(session, rc);

    /* when either of these conditions is met the call to ssh_channel_free will
     * actually free the channel so calling poll on that channel will be
     * use-after-free */
    if ((channel->flags & SSH_CHANNEL_FLAG_CLOSED_REMOTE) ||
        (channel->flags & SSH_CHANNEL_FLAG_NOT_BOUND)) {
        channel_freed = true;
    }
    SSH_CHANNEL_FREE(channel);

    if (!channel_freed) {
        rc = ssh_channel_get_exit_status(channel);
        assert_ssh_return_code_equal(session, rc, SSH_ERROR);
    }
}

static void
torture_channel_read_stderr(void **state)
{
    struct torture_state *s = *state;
    ssh_session session = s->ssh.session;
    ssh_channel channel;
    int rc;

    channel = ssh_channel_new(session);
    assert_non_null(channel);

    rc = ssh_channel_open_session(channel);
    assert_ssh_return_code(session, rc);

    /* This writes to standard error "pipe" */
    rc = ssh_channel_request_exec(channel, "echo -n ABCD >&2");
    assert_ssh_return_code(session, rc);

    /* No data in stdout */
    rc = ssh_channel_read(channel, buffer, sizeof(buffer), 0);
    assert_int_equal(rc, 0);

    /* poll should say how much we can read */
    rc = ssh_channel_poll(channel, 1);
    assert_int_equal(rc, strlen("ABCD"));

    /* Everything in stderr */
    rc = ssh_channel_read(channel, buffer, sizeof(buffer), 1);
    assert_int_equal(rc, strlen("ABCD"));

    buffer[rc] = '\0';
    assert_string_equal("ABCD", buffer);

    ssh_channel_free(channel);
}

static void torture_pubkey_hash(void **state)
{
    struct torture_state *s = *state;
    ssh_session session = s->ssh.session;
    char *hash = NULL;
    char *hexa = NULL;
    int rc = 0;

    /* bad arguments */
    rc = ssh_get_pubkey_hash(session, NULL);
    assert_int_equal(rc, SSH_ERROR);

    rc = ssh_get_pubkey_hash(NULL, (unsigned char **)&hash);
    assert_int_equal(rc, SSH_ERROR);

    /* deprecated, but should be covered by tests! */
    rc = ssh_get_pubkey_hash(session, (unsigned char **)&hash);
    if (ssh_fips_mode()) {
        /* When in FIPS mode, expect the call to fail */
        assert_int_equal(rc, SSH_ERROR);
    } else {
        assert_int_equal(rc, MD5_DIGEST_LEN);

        hexa = ssh_get_hexa((unsigned char *)hash, rc);
        SSH_STRING_FREE_CHAR(hash);
        assert_string_equal(hexa,
                            "ee:80:7f:61:f9:d5:be:f1:96:86:cc:96:7a:db:7a:7b");

        SSH_STRING_FREE_CHAR(hexa);
    }
}

static void torture_openssh_banner_version(void **state)
{
    struct torture_state *s = *state;
    ssh_session session = s->ssh.session;

    int openssh_version = ssh_get_openssh_version(session);
    int cmake_openssh_version = SSH_VERSION_INT(OPENSSH_VERSION_MAJOR, OPENSSH_VERSION_MINOR, 0);

    assert_int_equal(openssh_version, cmake_openssh_version);
}


int torture_run_tests(void) {
    int rc;
    struct CMUnitTest tests[] = {
        cmocka_unit_test_setup_teardown(torture_channel_read_error,
                                        session_setup,
                                        session_teardown),
        cmocka_unit_test_setup_teardown(torture_channel_poll_timeout_valid,
                                        session_setup,
                                        session_teardown),
        cmocka_unit_test_setup_teardown(torture_channel_poll_timeout,
                                        session_setup,
                                        session_teardown),
        cmocka_unit_test_setup_teardown(torture_max_sessions,
                                        session_setup,
                                        session_teardown),
        cmocka_unit_test_setup_teardown(torture_no_more_sessions,
                                        session_setup,
                                        session_teardown),
        cmocka_unit_test_setup_teardown(torture_channel_delayed_close,
                                        session_setup,
                                        session_teardown),
        cmocka_unit_test_setup_teardown(torture_freed_channel_poll,
                                        session_setup,
                                        session_teardown),
        cmocka_unit_test_setup_teardown(torture_freed_channel_poll_timeout,
                                        session_setup,
                                        session_teardown),
        cmocka_unit_test_setup_teardown(torture_freed_channel_read_nonblocking,
                                        session_setup,
                                        session_teardown),
        cmocka_unit_test_setup_teardown(torture_channel_exit_status,
                                        session_setup,
                                        session_teardown),
        cmocka_unit_test_setup_teardown(torture_channel_exit_signal,
                                        session_setup,
                                        session_teardown),
        cmocka_unit_test_setup_teardown(torture_freed_channel_get_exit_status,
                                        session_setup,
                                        session_teardown),
        cmocka_unit_test_setup_teardown(torture_channel_read_stderr,
                                        session_setup,
                                        session_teardown),
        cmocka_unit_test_setup_teardown(torture_pubkey_hash,
                                        session_setup,
                                        session_teardown),
        cmocka_unit_test_setup_teardown(torture_openssh_banner_version,
                                        session_setup,
                                        session_teardown),
    };

    ssh_init();

    torture_filter_tests(tests);
    rc = cmocka_run_group_tests(tests, sshd_setup, sshd_teardown);

    ssh_finalize();

    return rc;
}
