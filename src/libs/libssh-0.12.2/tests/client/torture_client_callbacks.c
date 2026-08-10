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
#include "libssh/callbacks.h"

#include <sys/types.h>
#include <pwd.h>
#include <errno.h>

#define STATE_SUCCESS (1)
#define STATE_FAILURE (2)

struct callback_state
{
    int open_response;
    int request_response;
    ssh_session expected_session;
    ssh_channel expected_channel;
    struct ssh_channel_callbacks_struct *callback;
};

static void on_open_response(ssh_session session, ssh_channel channel, bool is_success, void *userdata)
{
    struct callback_state *state = (struct callback_state*)userdata;
    assert_ptr_equal(state->expected_session, session);
    assert_ptr_equal(state->expected_channel, channel);
    state->open_response = is_success ? STATE_SUCCESS : STATE_FAILURE;
}

static void on_request_response(ssh_session session, ssh_channel channel, void *userdata)
{
    struct callback_state *state = (struct callback_state*)userdata;
    assert_ptr_equal(state->expected_session, session);
    assert_ptr_equal(state->expected_channel, channel);
    state->request_response = STATE_SUCCESS;
}

static struct callback_state *set_callbacks(ssh_session session, ssh_channel channel)
{
    int rc;
    struct ssh_channel_callbacks_struct *cb;
    struct callback_state *cb_state = NULL;

    cb_state = (struct callback_state *)calloc(1,
            sizeof(struct callback_state));
    assert_non_null(cb_state);
    cb_state->expected_session = session;
    cb_state->expected_channel = channel;

    cb = (struct ssh_channel_callbacks_struct *)calloc(1,
            sizeof(struct ssh_channel_callbacks_struct));
    assert_non_null(cb);
    ssh_callbacks_init(cb);
    cb->userdata = cb_state;
    cb->channel_open_response_function = on_open_response;
    cb->channel_request_response_function = on_request_response;
    rc = ssh_set_channel_callbacks(channel, cb);
    assert_ssh_return_code(session, rc);

    /* Keep the reference so it can be cleaned up later */
    cb_state->callback = cb;
    return cb_state;
}

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

static void torture_open_success(void **state)
{
    struct torture_state *s = *state;
    ssh_session session = s->ssh.session;
    ssh_channel channel;
    int rc;
    struct callback_state *cb_state = NULL;

    channel = ssh_channel_new(session);
    assert_non_null(channel);

    cb_state = set_callbacks(session, channel);

    rc = ssh_channel_open_session(channel);
    assert_ssh_return_code(session, rc);

    assert_int_equal(STATE_SUCCESS, cb_state->open_response);

    ssh_channel_free(channel);
    free(cb_state->callback);
    free(cb_state);
}

static void torture_open_failure(void **state)
{
    struct torture_state *s = *state;
    ssh_session session = s->ssh.session;
    ssh_channel channel;
    int rc;
    struct callback_state *cb_state = NULL;

    channel = ssh_channel_new(session);
    assert_non_null(channel);

    cb_state = set_callbacks(session, channel);

    rc = ssh_channel_open_forward(channel, "0.0.0.0", 0, "0.0.0.0", 0);
    assert_ssh_return_code_equal(session, rc, SSH_ERROR);

    assert_int_equal(STATE_FAILURE, cb_state->open_response);

    ssh_channel_free(channel);
    free(cb_state->callback);
    free(cb_state);
}

static void torture_request_success(void **state)
{
    struct torture_state *s = *state;
    ssh_session session = s->ssh.session;
    ssh_channel channel;
    int rc;
    struct callback_state *cb_state = NULL;

    channel = ssh_channel_new(session);
    assert_non_null(channel);

    cb_state = set_callbacks(session, channel);

    rc = ssh_channel_open_session(channel);
    assert_ssh_return_code(session, rc);

    rc = ssh_channel_request_exec(channel, "echo -n ABCD");
    assert_ssh_return_code(session, rc);

    assert_int_equal(STATE_SUCCESS, cb_state->request_response);

    ssh_channel_free(channel);
    free(cb_state->callback);
    free(cb_state);
}

static void torture_request_failure(void **state)
{
    struct torture_state *s = *state;
    ssh_session session = s->ssh.session;
    ssh_channel channel;
    int rc;
    struct callback_state *cb_state = NULL;

    channel = ssh_channel_new(session);
    assert_non_null(channel);

    cb_state = set_callbacks(session, channel);

    rc = ssh_channel_open_session(channel);
    assert_ssh_return_code(session, rc);

    rc = ssh_channel_request_env(channel, "NOT_ACCEPTED", "VALUE");
    assert_ssh_return_code_equal(session, rc, SSH_ERROR);

    assert_int_equal(STATE_SUCCESS, cb_state->request_response);

    ssh_channel_free(channel);
    free(cb_state->callback);
    free(cb_state);
}

int torture_run_tests(void)
{
    int rc;
    struct CMUnitTest tests[] = {
        cmocka_unit_test_setup_teardown(torture_open_success,
                                        session_setup,
                                        session_teardown),
        cmocka_unit_test_setup_teardown(torture_open_failure,
                                        session_setup,
                                        session_teardown),
        cmocka_unit_test_setup_teardown(torture_request_success,
                                        session_setup,
                                        session_teardown),
        cmocka_unit_test_setup_teardown(torture_request_failure,
                                        session_setup,
                                        session_teardown),
    };

    ssh_init();

    torture_filter_tests(tests);
    rc = cmocka_run_group_tests(tests, sshd_setup, sshd_teardown);

    ssh_finalize();

    return rc;
}
