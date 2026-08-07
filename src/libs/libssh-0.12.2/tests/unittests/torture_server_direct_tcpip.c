#include "config.h"

#define LIBSSH_STATIC

#include <errno.h>
#include <pthread.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/types.h>

#include "torture.h"
#include "torture_key.h"

#include <libssh/libssh.h>

#define TEST_SERVER_HOST "127.0.0.1"
#define TEST_SERVER_PORT 2222
#define TEST_DEST_HOST "127.0.0.1"
#define TEST_DEST_PORT 12345
#define TEST_ORIG_HOST "127.0.0.1"
#define TEST_ORIG_PORT 54321

struct hostkey_state {
    const char *hostkey;
    char *hostkey_path;
    enum ssh_keytypes_e key_type;
    int fd;
};

static int setup(void **state)
{
    struct hostkey_state *h = NULL;
    mode_t mask;
    int rc;

    ssh_threads_set_callbacks(ssh_threads_get_pthread());
    rc = ssh_init();
    if (rc != SSH_OK) {
        return -1;
    }

    h = malloc(sizeof(struct hostkey_state));
    assert_non_null(h);

    h->hostkey_path = strdup("/tmp/libssh_hostkey_XXXXXX");
    assert_non_null(h->hostkey_path);

    mask = umask(S_IRWXO | S_IRWXG);
    h->fd = mkstemp(h->hostkey_path);
    umask(mask);
    assert_return_code(h->fd, errno);
    close(h->fd);

    h->key_type = SSH_KEYTYPE_ECDSA_P256;
    h->hostkey = torture_get_testkey(h->key_type, 0);

    torture_write_file(h->hostkey_path, h->hostkey);

    *state = h;

    return 0;
}

static int teardown(void **state)
{
    struct hostkey_state *h = (struct hostkey_state *)*state;

    unlink(h->hostkey_path);
    free(h->hostkey_path);
    free(h);

    ssh_finalize();

    return 0;
}

static void *client_thread(void *arg)
{
    unsigned int test_port = TEST_SERVER_PORT;
    int rc;
    ssh_session session = NULL;
    ssh_channel channel = NULL;
    bool should_accept = *(bool *)arg;

    session =
        torture_ssh_session(NULL, TEST_SERVER_HOST, &test_port, "foo", "bar");
    assert_non_null(session);

    channel = ssh_channel_new(session);
    assert_non_null(channel);

    /* Open a direct-tcpip channel instead of a session channel */
    rc = ssh_channel_open_forward(channel,
                                  TEST_DEST_HOST,
                                  TEST_DEST_PORT,
                                  TEST_ORIG_HOST,
                                  TEST_ORIG_PORT);
    if (should_accept) {
        assert_int_equal(rc, SSH_OK);
    } else {
        assert_int_equal(rc, SSH_ERROR);
    }

    /* Close the channel and session */
    ssh_channel_close(channel);
    ssh_channel_free(channel);
    ssh_free(session);

    return NULL;
}

static int auth_password_accept(ssh_session session,
                                const char *user,
                                const char *password,
                                void *userdata)
{
    /* unused */
    (void)session;
    (void)user;
    (void)password;
    (void)userdata;

    return SSH_AUTH_SUCCESS;
}

struct channel_data {
    /* Whether the callback should accept the channel open request */
    bool should_accept;

    int req_seen;
    char *dest_host;
    uint32_t dest_port;
    char *orig_host;
    uint32_t orig_port;
};

static ssh_channel channel_direct_tcpip_callback(ssh_session session,
                                                 const char *dest_host,
                                                 int dest_port,
                                                 const char *orig_host,
                                                 int orig_port,
                                                 void *userdata)
{
    struct channel_data *channel_data = userdata;
    ssh_channel channel = NULL;

    /* Record that we've seen a direct-tcpip request and store the parameters */
    channel_data->req_seen = 1;
    channel_data->dest_host = strdup(dest_host);
    channel_data->dest_port = dest_port;
    channel_data->orig_host = strdup(orig_host);
    channel_data->orig_port = orig_port;

    /* Create and return a new channel for this request */
    if (channel_data->should_accept) {
        channel = ssh_channel_new(session);
    }
    return channel;
}

static void torture_ssh_channel_direct_tcpip(void **state, int should_accept)
{
    struct hostkey_state *h = (struct hostkey_state *)*state;
    int rc, event_rc;
    pthread_t client_pthread;
    ssh_bind sshbind = NULL;
    ssh_session server = NULL;
    ssh_event event = NULL;

    struct channel_data channel_data;
    struct ssh_server_callbacks_struct server_cb = {
        .userdata = &channel_data,
        .auth_password_function = auth_password_accept,
        .channel_open_request_direct_tcpip_function =
            channel_direct_tcpip_callback,
    };

    memset(&channel_data, 0, sizeof(channel_data));
    ssh_callbacks_init(&server_cb);

    /* Create server */
    sshbind = torture_ssh_bind(TEST_SERVER_HOST,
                               TEST_SERVER_PORT,
                               h->key_type,
                               h->hostkey_path);
    assert_non_null(sshbind);

    channel_data.should_accept = should_accept;

    /* Get client to connect */
    rc = pthread_create(&client_pthread,
                        NULL,
                        client_thread,
                        &channel_data.should_accept);
    assert_return_code(rc, errno);

    server = ssh_new();
    assert_non_null(server);

    rc = ssh_bind_accept(sshbind, server);
    assert_int_equal(rc, SSH_OK);

    /* Handle client connection */
    ssh_set_server_callbacks(server, &server_cb);

    rc = ssh_handle_key_exchange(server);
    assert_int_equal(rc, SSH_OK);

    event = ssh_event_new();
    assert_non_null(event);

    ssh_event_add_session(event, server);

    event_rc = SSH_OK;
    while (!channel_data.req_seen && event_rc == SSH_OK) {
        event_rc = ssh_event_dopoll(event, -1);
    }

    /* Cleanup */
    ssh_event_free(event);
    ssh_free(server);
    ssh_bind_free(sshbind);

    rc = pthread_join(client_pthread, NULL);
    assert_int_equal(rc, 0);

    /* Verify direct-tcpip request parameters */
    assert_true(channel_data.req_seen);
    assert_string_equal(channel_data.dest_host, TEST_DEST_HOST);
    assert_int_equal(channel_data.dest_port, TEST_DEST_PORT);
    assert_string_equal(channel_data.orig_host, TEST_ORIG_HOST);
    assert_int_equal(channel_data.orig_port, TEST_ORIG_PORT);

    /* Free allocated memory */
    free(channel_data.dest_host);
    free(channel_data.orig_host);
}

static void torture_ssh_channel_direct_tcpip_success(void **state)
{
    torture_ssh_channel_direct_tcpip(state, true);
}

static void torture_ssh_channel_direct_tcpip_failure(void **state)
{
    torture_ssh_channel_direct_tcpip(state, false);
}

int torture_run_tests(void)
{
    int rc;
    const struct CMUnitTest tests[] = {
        cmocka_unit_test_setup_teardown(
            torture_ssh_channel_direct_tcpip_success,
            setup,
            teardown),
        cmocka_unit_test_setup_teardown(
            torture_ssh_channel_direct_tcpip_failure,
            setup,
            teardown),
    };

    rc = cmocka_run_group_tests(tests, NULL, NULL);
    return rc;
}
