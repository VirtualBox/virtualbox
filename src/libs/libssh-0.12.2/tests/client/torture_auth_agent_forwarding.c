#include "config.h"

#if !defined(_WIN32) || (defined(WITH_SERVER) && defined(HAVE_PTHREAD))

#define LIBSSH_STATIC

#include "torture.h"
#include <stdbool.h>
#include <stdlib.h> /* For calloc/free */

#include "libssh/callbacks.h"
#include "libssh/libssh.h"
#include "libssh/priv.h"

#include <errno.h>
#include <pwd.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h> /* usleep */

#ifndef UNIX_PATH_MAX
#define UNIX_PATH_MAX 108
#endif

/* struct to store the state of the test */
struct agent_callback_state {
    int called;
    ssh_session expected_session;
    ssh_channel created_channel;
};

/* Agent callback function that will be triggered when a channel open request is
 * received */
static ssh_channel agent_callback(ssh_session session, void *userdata)
{
    struct agent_callback_state *state =
        (struct agent_callback_state *)userdata;
    ssh_channel channel = NULL; /* Initialize to NULL */

    /* Increment call counter */
    state->called++;

    /* Verify session matches what we expect */
    assert_ptr_equal(session, state->expected_session);

    /* Create a new channel for agent forwarding */
    channel = ssh_channel_new(session);
    if (channel == NULL) {
        return NULL;
    }

    /* Make the channel non-blocking */
    ssh_channel_set_blocking(channel, 0);

    /* Store the channel for verification and later cleanup */
    state->created_channel = channel;

    return channel;
}

static int sshd_setup_agent_forwarding(void **state)
{
    int rc;

    /* Use the standard server setup function */
    torture_setup_sshd_server(state, false);

    /* Override the default configuration with our own, adding agent forwarding
     * support */
    rc = torture_update_sshd_config(state, "AllowAgentForwarding yes\n");
    assert_int_equal(rc, SSH_OK);

    return 0;
}

/* Only free the session - nothing else */
static int session_teardown(void **state)
{
    struct torture_state *s = *state;

    if (s != NULL && s->ssh.ssh.session != NULL) {
        /* Clean up callback resources first */
        if (s->ssh.ssh.cb_state != NULL) {
            struct agent_callback_state *cb_state = s->ssh.ssh.cb_state;

            /* Close and free any open channel from the callback */
            if (cb_state->created_channel != NULL) {
                ssh_channel_close(cb_state->created_channel);
                ssh_channel_free(cb_state->created_channel);
            }

            free(cb_state);
            s->ssh.ssh.cb_state = NULL;
        }

        if (s->ssh.ssh.callbacks != NULL) {
            free(s->ssh.ssh.callbacks);
            s->ssh.ssh.callbacks = NULL;
        }

        /* Disconnect and free the session */
        ssh_disconnect(s->ssh.ssh.session);
        ssh_free(s->ssh.ssh.session);
        s->ssh.ssh.session = NULL;
    }

    return 0;
}

static int torture_teardown_ssh_agent(void **state)
{
    struct torture_state *s = *state;
    int rc;

    if (s == NULL) {
        return 0;
    }

    /* Kill the SSH agent */
    rc = torture_cleanup_ssh_agent();
    assert_return_code(rc, errno);

    /* Use the standard teardown function which will properly clean up */
    torture_teardown_sshd_server(state);

    return 0;
}

/* Test function to verify if agent forwarding callback works */
static void torture_auth_agent_forwarding(void **state)
{
    struct torture_state *s = *state;
    struct agent_callback_state *cb_state;
    ssh_session session = NULL;
    ssh_channel channel = NULL; /* Initialize to NULL */
    int rc;
    int port = torture_server_port();
    char buffer[4096] = {0};
    int nbytes;
    int max_read_attempts = 10; /* Limit the number of read attempts */
    int read_count = 0;
    bool agent_available = false;
    bool agent_not_available_found = false;
    size_t exp_socket_len;

    /* The forwarded agent socket is created under the home directory, which
     * might easily extend the maximum unix domain socket path length.
     * If we see this, just skip the test as it will not work */
    exp_socket_len = strlen(BINARYDIR) +
                     strlen("/home/bob/.ssh/agent.1234567890.sshd.XXXXXXXXXX");
    if (exp_socket_len > UNIX_PATH_MAX) {
        SSH_LOG(SSH_LOG_WARNING,
                "The working directory is too long for agent forwarding to work"
                ": Skipping the test");
        skip();
    }

    assert_non_null(s);
    session = s->ssh.ssh.session;
    assert_non_null(session);

    /* Get our callback state */
    cb_state = (struct agent_callback_state *)s->ssh.ssh.cb_state;
    assert_non_null(cb_state);

    /* Set username */
    rc = ssh_options_set(session, SSH_OPTIONS_USER, TORTURE_SSH_USER_BOB);
    assert_ssh_return_code(session, rc);

    /* Set server address */
    rc = ssh_options_set(session, SSH_OPTIONS_HOST, TORTURE_SSH_SERVER);
    assert_ssh_return_code(session, rc);

    /* Set port */
    rc = ssh_options_set(session, SSH_OPTIONS_PORT, &port);
    assert_ssh_return_code(session, rc);

    /* Connect to server */
    rc = ssh_connect(session);
    assert_ssh_return_code(session, rc);

    /* Authenticate */
    rc = ssh_userauth_password(session, NULL, TORTURE_SSH_USER_BOB_PASSWORD);
    assert_int_equal(rc, SSH_AUTH_SUCCESS);

    /* Create a single channel that we'll use for all tests */
    channel = ssh_channel_new(session);
    assert_non_null(channel);

    rc = ssh_channel_open_session(channel);
    assert_ssh_return_code(session, rc);

    /* Request agent forwarding */
    rc = ssh_channel_request_auth_agent(channel);
    assert_ssh_return_code(session, rc);

    /* Running a command that will try to use the SSH agent */
    rc = ssh_channel_request_exec(
        channel,
        "echo 'Simple command'; "
        "echo 'ENV SSH_AUTH_SOCK=>['$SSH_AUTH_SOCK']<'; " /* Use boundary
                                                             markers */
        "ssh-add -l || echo 'Agent not available'; "
        "echo 'Done'"); /* Marker for command completion */
    assert_ssh_return_code(session, rc);

    /* Set to non-blocking mode with manual timeout implementation
     * This prevents the test from hanging indefinitely if there's an issue with
     * the channel communication. We implement our own timeout logic using a
     * counter and sleep, which gives the server time to process our request
     * while still ensuring the test will eventually terminate even if no EOF is
     * received.
     */
    ssh_channel_set_blocking(channel, 0);

    /* Read with safety counter to prevent infinite loops */
    while (!ssh_channel_is_eof(channel) && read_count < max_read_attempts) {
        nbytes = ssh_channel_read_nonblocking(channel,
                                              buffer,
                                              sizeof(buffer) - 1,
                                              0);

        if (nbytes > 0) {
            buffer[nbytes] = 0;
            ssh_log_hexdump("Read bytes:", (unsigned char *)buffer, nbytes);

            /* Process the command output to check for three key conditions:
             * 1. If SSH_AUTH_SOCK is properly set (meaning agent forwarding
             * works)
             * 2. If "Agent not available" message appears (indicating failure)
             * 3. If we've seen the "Done" marker (to know when to stop reading)
             */
            /* Check if SSH_AUTH_SOCK has a non-empty value by looking for
             * boundary markers with content between them */
            if (strstr(buffer, "ENV SSH_AUTH_SOCK=>[") != NULL &&
                strstr(buffer, "]<") != NULL &&
                strstr(buffer, "ENV SSH_AUTH_SOCK=>[]<") == NULL) {
                agent_available = true;
            }

            if (strstr(buffer, "Agent not available") != NULL) {
                agent_not_available_found = true;
            }

            if (strstr(buffer, "Done") != NULL) {
                break;
            }
        } else if (nbytes == SSH_ERROR) {
            break;
        } else if (nbytes == SSH_EOF) {
            break;
        }

        /* Short sleep between reads to avoid spinning */
        usleep(100000); /* 100ms */
        read_count++;
    }

    /* Trying to read from stderr as well */
    ssh_channel_read_nonblocking(channel, buffer, sizeof(buffer) - 1, 1);

    /* Close the channel */
    ssh_channel_send_eof(channel);
    ssh_channel_close(channel);
    ssh_channel_free(channel);

    /* Verify agent forwarding worked correctly */

    /* Verify callback was called exactly once */
    assert_int_equal(cb_state->called, 1);

    /* Verify "Agent not available" was not found
     * The agent should be available - we should never see "Agent not available"
     * output
     */
    assert_false(agent_not_available_found);

    /* Verify SSH_AUTH_SOCK is set */
    assert_true(agent_available);

    /* Any channel created in the callback is freed */
    if (cb_state->created_channel) {
        ssh_channel_close(cb_state->created_channel);
        ssh_channel_free(cb_state->created_channel);
        cb_state->created_channel = NULL;
    }
}

/* Session setup function that configures SSH agent */
static int session_setup(void **state)
{
    struct torture_state *s = *state;
    int verbosity = torture_libssh_verbosity();
    struct agent_callback_state *cb_state = NULL;
    struct ssh_callbacks_struct *callbacks = NULL;
    char key_path[1024];
    struct passwd *pw = NULL;
    int rc;

    /* Create a new session */
    s->ssh.ssh.session = ssh_new();
    assert_non_null(s->ssh.ssh.session);

    rc = ssh_options_set(s->ssh.ssh.session,
                         SSH_OPTIONS_LOG_VERBOSITY,
                         &verbosity);
    assert_int_equal(rc, SSH_OK);

    /* Create and initialize the callback state */
    cb_state = calloc(1, sizeof(struct agent_callback_state));
    assert_non_null(cb_state);

    cb_state->expected_session = s->ssh.ssh.session;
    cb_state->created_channel = NULL;

    /* Set up the callbacks */
    callbacks = calloc(1, sizeof(struct ssh_callbacks_struct));
    assert_non_null(callbacks);

    callbacks->userdata = cb_state;
    callbacks->channel_open_request_auth_agent_function = agent_callback;

    ssh_callbacks_init(callbacks);
    rc = ssh_set_callbacks(s->ssh.ssh.session, callbacks);
    assert_int_equal(rc, SSH_OK);

    /* Store callback state and callbacks */
    s->ssh.ssh.cb_state = cb_state;
    s->ssh.ssh.callbacks = callbacks;

    /* Set up SSH agent with Bob's key */
    pw = getpwnam("bob");
    assert_non_null(pw);
    snprintf(key_path, sizeof(key_path), "%s/.ssh/id_rsa", pw->pw_dir);
    rc = torture_setup_ssh_agent(s, key_path);
    assert_return_code(rc, errno);

    return 0;
}

/* Main test function */
int torture_run_tests(void)
{
    int rc;
    struct CMUnitTest tests[] = {
        cmocka_unit_test_setup_teardown(torture_auth_agent_forwarding,
                                        session_setup,
                                        session_teardown),
    };

    ssh_init();

    /* Simplify the CMocka test filter handling */
#if defined HAVE_CMOCKA_SET_TEST_FILTER
    cmocka_set_message_output(CM_OUTPUT_STDOUT);
#endif

    torture_filter_tests(tests);

    rc = cmocka_run_group_tests(tests,
                                sshd_setup_agent_forwarding,
                                torture_teardown_ssh_agent);

    ssh_finalize();

    return rc;
}

#endif
