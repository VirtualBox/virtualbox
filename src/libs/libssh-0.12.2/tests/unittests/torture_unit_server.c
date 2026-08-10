#include "config.h"

#define LIBSSH_STATIC

#include <errno.h>
#include <pthread.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <signal.h>

#include <libssh/libssh.h>
#include <libssh/bind.h>
#include "torture.h"
#include "torture_key.h"

#define TEST_SERVER_PORT 2222

#if 0
struct test_state {
    const char *hostkey;
    char *hostkey_path;
    enum ssh_keytypes_e key_type;
    int fd;
};

static int setup(void **state)
{
    struct test_state *ts = NULL;
    mode_t mask;
    int rc;

    ssh_threads_set_callbacks(ssh_threads_get_pthread());
    rc = ssh_init();
    if (rc != SSH_OK) {
        return -1;
    }

    ts = malloc(sizeof(struct test_state));
    assert_non_null(ts);

    ts->hostkey_path = strdup("/tmp/libssh_hostkey_XXXXXX");

    mask = umask(S_IRWXO | S_IRWXG);
    ts->fd = mkstemp(ts->hostkey_path);
    umask(mask);
    assert_return_code(ts->fd, errno);
    close(ts->fd);

    ts->key_type = SSH_KEYTYPE_ECDSA_P256;
    ts->hostkey = torture_get_testkey(ts->key_type, 0);

    torture_write_file(ts->hostkey_path, ts->hostkey);

    *state = ts;

    return 0;
}

static int teardown(void **state)
{
    struct test_state *ts = (struct test_state *)*state;

    unlink(ts->hostkey);
    free(ts->hostkey_path);
    free(ts);

    ssh_finalize();

    return 0;
}

/* TODO the signals are handled by cmocka so they are not testable her :( */
static void *int_thread(void *arg)
{
    usleep(1);
    kill(getpid(), SIGUSR1);
    return NULL;
}

static void *client_thread(void *arg)
{
    unsigned int test_port = TEST_SERVER_PORT;
    int rc;
    ssh_session session;
    ssh_channel channel;

    /* unused */
    (void)arg;

    usleep(200);
    session = torture_ssh_session(NULL, "localhost",
                                  &test_port,
                                  "foo", "bar");
    assert_non_null(session);

    channel = ssh_channel_new(session);
    assert_non_null(channel);

    rc = ssh_channel_open_session(channel);
    assert_int_equal(rc, SSH_OK);

    ssh_free(session);
    return NULL;
}

static void test_ssh_accept_interrupt(void **state)
{
    struct test_state *ts = (struct test_state *)*state;
    int rc;
    pthread_t client_pthread, interrupt_pthread;
    ssh_bind sshbind = NULL;
    ssh_session server;

    /* Create server */
    sshbind = torture_ssh_bind("localhost",
                               TEST_SERVER_PORT,
                               ts->key_type,
                               ts->hostkey_path);
    assert_non_null(sshbind);

    server = ssh_new();
    assert_non_null(server);

    /* Send interrupt in 1 second */
    rc = pthread_create(&interrupt_pthread, NULL, int_thread, NULL);
    assert_return_code(rc, errno);

    rc = pthread_join(interrupt_pthread, NULL);
    assert_int_equal(rc, 0);

    rc = ssh_bind_accept(sshbind, server);
    assert_int_equal(rc, SSH_ERROR);
    assert_int_equal(ssh_get_error_code(sshbind), SSH_EINTR);

    /* Get client to connect now */
    rc = pthread_create(&client_pthread, NULL, client_thread, NULL);
    assert_return_code(rc, errno);

    /* Now, try again */
    rc = ssh_bind_accept(sshbind, server);
    assert_int_equal(rc, SSH_OK);

    /* Cleanup */
    ssh_bind_free(sshbind);

    rc = pthread_join(client_pthread, NULL);
    assert_int_equal(rc, 0);
}
#endif


static void test_default_hostkey_paths(void **state)
{
    int rc;
    ssh_bind sshbind = NULL;

    /* state not used */
    (void)state;

    /* Create server */
    rc = ssh_init();
    assert_int_equal(rc, 0);

    sshbind = ssh_bind_new();
    assert_non_null(sshbind);

    /* This will fail because we don't have permission to import keys unless we run as root
     * TODO: Implement some filesystem wrapper, that would allow this check to pass by
     * reading the keys from some accessible test location */
    ssh_bind_listen(sshbind);

    assert_string_equal(sshbind->rsakey, "/etc/ssh/ssh_host_rsa_key");
    assert_string_equal(sshbind->ecdsakey, "/etc/ssh/ssh_host_ecdsa_key");
    assert_string_equal(sshbind->ed25519key, "/etc/ssh/ssh_host_ed25519_key");

    /* Cleanup */
    ssh_bind_free(sshbind);
    ssh_finalize();
}

int torture_run_tests(void)
{
    int rc;
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_default_hostkey_paths),
        /* Not working correctly the signals are not testable under cmocka
        cmocka_unit_test_setup_teardown(test_ssh_accept_interrupt,
                                        setup,
                                        teardown) */
    };

    rc = cmocka_run_group_tests(tests, NULL, NULL);
    return rc;
}
