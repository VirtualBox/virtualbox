/*
 * This file is part of the SSH Library
 *
 * Copyright (c) 2024 Eshan Kelkar <eshankelkar@galorithm.com>
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
#include <libssh/libssh.h>
#include <libssh/sftp.h>
#include <libssh/sftp_priv.h>

#include <errno.h>
#include <fcntl.h>
#include <pwd.h>
#include <sys/types.h>

/* For the ability to access the members of the sftp_aio_struct in the test */
#include "sftp_aio.c"

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
    struct passwd *pwd = NULL;
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

    s->ssh.tsftp = torture_sftp_session(s->ssh.session);
    assert_non_null(s->ssh.tsftp);

    return 0;
}

static int session_teardown(void **state)
{
    struct torture_state *s = *state;

    torture_rmdirs(s->ssh.tsftp->testdir);
    torture_sftp_close(s->ssh.tsftp);
    ssh_disconnect(s->ssh.session);
    ssh_free(s->ssh.session);

    return 0;
}

/* Test that sftp_recv_response_msg() works properly in blocking mode */
static void torture_sftp_recv_response_msg_blocking(void **state)
{
    struct torture_state *s = *state;
    struct torture_sftp *t = s->ssh.tsftp;
    sftp_session sftp = t->sftp;
    sftp_file file = NULL;
    sftp_aio aio = NULL;
    sftp_message msg = NULL;
    ssize_t bytes_requested;
    int rc;

    /*
     * For sending an sftp request and obtaining its request id, this test uses
     * the sftp aio API
     */
    file = sftp_open(sftp, SSH_EXECUTABLE, O_RDONLY, 0);
    assert_non_null(file);

    /* Send an sftp read request */
    bytes_requested = sftp_aio_begin_read(file, 16, &aio);
    assert_int_equal(bytes_requested, 16);
    assert_non_null(aio);

    /* Wait for the response (blocking mode) */
    rc = sftp_recv_response_msg(sftp, aio->id, true, &msg);
    assert_int_equal(rc, SSH_OK);

    sftp_message_free(msg);
    sftp_aio_free(aio);
    sftp_close(file);
}

/* Test that sftp_recv_response_msg() works properly in non blocking mode */
static void torture_sftp_recv_response_msg_non_blocking(void **state)
{
    struct torture_state *s = *state;
    struct torture_sftp *t = s->ssh.tsftp;
    sftp_session sftp = t->sftp;
    sftp_message msg = NULL;
    sftp_file file = NULL;
    sftp_aio aio = NULL;
    ssize_t bytes_requested;
    int rc;

    /*
     * At this point, the sftp channel shouldn't contain any outstanding
     * responses.
     *
     * Hence, sftp_recv_response_msg() should return SSH_AGAIN immediately when
     * we try to receive a response for any request ID in non-blocking mode.
     */
    rc = sftp_recv_response_msg(sftp, 1984, false, &msg);
    assert_int_equal(rc, SSH_AGAIN);

    /*
     * Validate that after a response arrives in the sftp channel, trying to
     * receive the response in non-blocking mode works properly.
     *
     * For sending an sftp request and obtaining its request id, this test uses
     * the sftp aio API
     */
    file = sftp_open(sftp, SSH_EXECUTABLE, O_RDONLY, 0);
    assert_non_null(file);

    bytes_requested = sftp_aio_begin_read(file, 16, &aio);
    assert_int_equal(bytes_requested, 16);
    assert_non_null(aio);

    /* Poll the sftp channel for the response */
    rc = ssh_channel_poll_timeout(sftp->channel, 60000, 0);
    assert_int_not_equal(rc, SSH_ERROR);
    assert_int_not_equal(rc, SSH_EOF);
    assert_int_not_equal(rc, 0);

    /*
     * The response has arrived, trying to obtain it in non blocking mode
     * should work
     */
    rc = sftp_recv_response_msg(sftp, aio->id, false, &msg);
    assert_int_equal(rc, SSH_OK);

    sftp_message_free(msg);
    sftp_aio_free(aio);
    sftp_close(file);
}

int torture_run_tests(void)
{
    int rc;
    struct CMUnitTest tests[] = {
        cmocka_unit_test_setup_teardown(torture_sftp_recv_response_msg_blocking,
                                        session_setup,
                                        session_teardown),
        cmocka_unit_test_setup_teardown(
            torture_sftp_recv_response_msg_non_blocking,
            session_setup,
            session_teardown),
    };

    ssh_init();

    torture_filter_tests(tests);
    rc = cmocka_run_group_tests(tests, sshd_setup, sshd_teardown);
    ssh_finalize();

    return rc;
}
