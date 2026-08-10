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

struct server_thread_args {
    struct hostkey_state *h;
    bool should_accept;
};

static bool is_server_ready = false;
static pthread_mutex_t server_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t server_cond = PTHREAD_COND_INITIALIZER;

static bool client_callbacks_initialised = false;
static pthread_mutex_t client_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t client_cond = PTHREAD_COND_INITIALIZER;

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

    h = (struct hostkey_state *)malloc(sizeof(struct hostkey_state));
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

    /* Reset before every test */
    is_server_ready = false;
    client_callbacks_initialised = false;

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

static void *server_thread(void *arg)
{
    struct server_thread_args *args = (struct server_thread_args *)arg;
    struct hostkey_state *h = args->h;
    bool should_accept = args->should_accept;
    ssh_bind sshbind = NULL;
    ssh_session server = NULL;
    ssh_channel channel = NULL;
    ssh_event event = NULL;
    int rc;

    struct ssh_server_callbacks_struct server_cb = {
        .auth_password_function = auth_password_accept,
    };
    ssh_callbacks_init(&server_cb);

    /* Create server */
    sshbind = torture_ssh_bind(TEST_SERVER_HOST,
                               TEST_SERVER_PORT,
                               h->key_type,
                               h->hostkey_path);
    assert_non_null(sshbind);

    server = ssh_new();
    assert_non_null(server);

    rc = ssh_set_server_callbacks(server, &server_cb);
    assert_int_equal(rc, SSH_OK);

    /* Signal that the server is ready */
    pthread_mutex_lock(&server_mutex);
    is_server_ready = true;
    pthread_cond_signal(&server_cond);
    pthread_mutex_unlock(&server_mutex);

    rc = ssh_bind_accept(sshbind, server);
    assert_int_equal(rc, SSH_OK);

    rc = ssh_handle_key_exchange(server);
    assert_int_equal(rc, SSH_OK);

    /* Handle client connection */
    event = ssh_event_new();
    assert_non_null(event);

    rc = ssh_event_add_session(event, server);
    assert_int_equal(rc, SSH_OK);

    /* Poll until authentication is complete */
    while (server->session_state != SSH_SESSION_STATE_AUTHENTICATED) {
        rc = ssh_event_dopoll(event, -1);
        if (rc == SSH_ERROR) {
            break;
        }
    }

    /* Cleanup the event */
    ssh_event_free(event);

    /* Wait for client callbacks to be initialized before proceeding */
    pthread_mutex_lock(&client_mutex);
    while (!client_callbacks_initialised) {
        pthread_cond_wait(&client_cond, &client_mutex);
    }
    pthread_mutex_unlock(&client_mutex);

    channel = ssh_channel_new(server);
    assert_non_null(channel);

    rc = ssh_channel_open_reverse_forward(channel,
                                          TEST_DEST_HOST,
                                          TEST_DEST_PORT,
                                          TEST_ORIG_HOST,
                                          TEST_ORIG_PORT);
    if (should_accept) {
        assert_int_equal(rc, SSH_OK);
    } else {
        assert_int_equal(rc, SSH_ERROR);
    }

    ssh_channel_close(channel);
    ssh_channel_free(channel);
    ssh_bind_free(sshbind);
    ssh_free(server);

    return NULL;
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

static ssh_channel channel_forwarded_tcpip_callback(ssh_session session,
                                                    const char *dest_host,
                                                    int dest_port,
                                                    const char *orig_host,
                                                    int orig_port,
                                                    void *userdata)
{
    struct channel_data *channel_data = (struct channel_data *)userdata;
    ssh_channel channel = NULL;

    /* Record that we've seen a forwarded-tcpip request and store the parameters
     */
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

static void torture_forwarded_tcpip_callback(void **state, bool should_accept)
{
    int rc, event_rc;
    pthread_t server_pthread;
    ssh_session session = NULL;
    ssh_event event = NULL;
    struct channel_data channel_data;
    unsigned int server_port = TEST_SERVER_PORT;

    struct server_thread_args args = {
        .h = (struct hostkey_state *)*state,
        .should_accept = should_accept,
    };

    struct ssh_callbacks_struct client_cb = {
        .userdata = &channel_data,
        .channel_open_request_forwarded_tcpip_function =
            channel_forwarded_tcpip_callback,
    };
    ssh_callbacks_init(&client_cb);

    memset(&channel_data, 0, sizeof(channel_data));
    channel_data.should_accept = should_accept;

    rc = pthread_create(&server_pthread, NULL, server_thread, &args);
    assert_return_code(rc, errno);

    /* Wait for the server to be ready using condition variable */
    pthread_mutex_lock(&server_mutex);
    while (!is_server_ready) {
        pthread_cond_wait(&server_cond, &server_mutex);
    }
    pthread_mutex_unlock(&server_mutex);

    session =
        torture_ssh_session(NULL, "127.0.0.1", &server_port, "foo", "bar");
    assert_non_null(session);

    rc = ssh_set_callbacks(session, &client_cb);
    assert_int_equal(rc, SSH_OK);

    event = ssh_event_new();
    assert_non_null(event);

    rc = ssh_event_add_session(event, session);
    assert_int_equal(rc, SSH_OK);

    /* Signal that client callbacks are initialized */
    pthread_mutex_lock(&client_mutex);
    client_callbacks_initialised = true;
    pthread_cond_signal(&client_cond);
    pthread_mutex_unlock(&client_mutex);

    event_rc = SSH_OK;
    while (channel_data.req_seen != 1 && event_rc == SSH_OK) {
        event_rc = ssh_event_dopoll(event, -1);
    }

    /* Cleanup */
    ssh_event_free(event);
    ssh_free(session);

    rc = pthread_join(server_pthread, NULL);
    assert_int_equal(rc, 0);

    /* Verify forwarded-tcpip request parameters */
    assert_true(channel_data.req_seen);
    assert_string_equal(channel_data.dest_host, TEST_DEST_HOST);
    assert_int_equal(channel_data.dest_port, TEST_DEST_PORT);
    assert_string_equal(channel_data.orig_host, TEST_ORIG_HOST);
    assert_int_equal(channel_data.orig_port, TEST_ORIG_PORT);

    /* Free allocated memory */
    free(channel_data.dest_host);
    free(channel_data.orig_host);
}

static void torture_forwarded_tcpip_callback_success(void **state)
{
    torture_forwarded_tcpip_callback(state, true);
}

static void torture_forwarded_tcpip_callback_failure(void **state)
{
    torture_forwarded_tcpip_callback(state, false);
}

int torture_run_tests(void)
{
    int rc;
    const struct CMUnitTest tests[] = {
        cmocka_unit_test_setup_teardown(
            torture_forwarded_tcpip_callback_success,
            setup,
            teardown),
        cmocka_unit_test_setup_teardown(
            torture_forwarded_tcpip_callback_failure,
            setup,
            teardown),
    };

    rc = cmocka_run_group_tests(tests, NULL, NULL);
    return rc;
}
