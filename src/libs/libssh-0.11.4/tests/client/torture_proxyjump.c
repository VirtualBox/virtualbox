#include "config.h"

#define LIBSSH_STATIC

#include "torture.h"
#include <libssh/libssh.h>

#include <pwd.h>
#include <errno.h>
#include <fcntl.h>

static int
sshd_setup(void **state)
{
    torture_setup_sshd_server(state, false);

    return 0;
}

static int
sshd_teardown(void **state)
{
    torture_teardown_sshd_server(state);

    return 0;
}

static int
session_setup(void **state)
{
    struct torture_state *s = *state;
    int verbosity = torture_libssh_verbosity();
    struct passwd *pwd = NULL;
    int rc;
    bool b = false;

    pwd = getpwnam("bob");
    assert_non_null(pwd);

    rc = setuid(pwd->pw_uid);
    assert_return_code(rc, errno);

    s->ssh.session = ssh_new();
    assert_non_null(s->ssh.session);

    rc = ssh_options_set(s->ssh.session, SSH_OPTIONS_LOG_VERBOSITY, &verbosity);
    assert_ssh_return_code(s->ssh.session, rc);
    rc = ssh_options_set(s->ssh.session, SSH_OPTIONS_HOST, TORTURE_SSH_SERVER);
    assert_ssh_return_code(s->ssh.session, rc);
    rc = ssh_options_set(s->ssh.session,
                         SSH_OPTIONS_USER,
                         TORTURE_SSH_USER_ALICE);
    assert_ssh_return_code(s->ssh.session, rc);

    /* Make sure no other configuration options from system will get used */
    rc = ssh_options_set(s->ssh.session, SSH_OPTIONS_PROCESS_CONFIG, &b);
    assert_ssh_return_code(s->ssh.session, rc);

    unsetenv("SSH_AUTH_SOCK");
    unsetenv("SSH_AGENT_PID");

    return 0;
}

static int
session_teardown(void **state)
{
    struct torture_state *s = *state;

    ssh_disconnect(s->ssh.session);
    ssh_free(s->ssh.session);

    return 0;
}

static void
torture_proxyjump_single_jump(void **state)
{
    struct torture_state *s = *state;
    ssh_session session = s->ssh.session;
    char proxyjump_buf[500] = {0};
    const char *address = torture_server_address(AF_INET);
    int rc;
    socket_t fd;

    rc = snprintf(proxyjump_buf, sizeof(proxyjump_buf), "alice@%s:22", address);
    if (rc < 0 || rc >= (int)sizeof(proxyjump_buf)) {
        fail_msg("snprintf failed");
    }
    rc = ssh_options_set(session, SSH_OPTIONS_PROXYJUMP, proxyjump_buf);
    assert_ssh_return_code(session, rc);

    rc = ssh_connect(session);
    assert_ssh_return_code(session, rc);

    fd = ssh_get_fd(session);
    assert_int_not_equal(fd, SSH_INVALID_SOCKET);

    rc = fcntl(fd, F_GETFL);
    assert_int_equal(rc & O_RDWR, O_RDWR);

    rc = ssh_userauth_publickey_auto(session, NULL, NULL);
    assert_int_equal(rc, SSH_AUTH_SUCCESS);
}

static int
before_connection(ssh_session jump_session, void *user)
{
    (void)jump_session;
    (void)user;

    return 0;
}

static int
verify_knownhost(ssh_session jump_session, void *user)
{
    (void)jump_session;
    (void)user;

    return 0;
}

static int
authenticate(ssh_session jump_session, void *user)
{
    (void)user;

    return ssh_userauth_publickey_auto(jump_session, NULL, NULL);
}

static void
torture_proxyjump_multiple_jump(void **state)
{
    struct torture_state *s = *state;
    ssh_session session = s->ssh.session;
    char proxyjump_buf[500] = {0};
    const char *address = torture_server_address(AF_INET);
    int rc;
    socket_t fd;

    struct ssh_jump_callbacks_struct c = {
        .before_connection = before_connection,
        .verify_knownhost = verify_knownhost,
        .authenticate = authenticate
    };

    rc = snprintf(proxyjump_buf,
                  sizeof(proxyjump_buf),
                  "alice@%s:22,alice@%s:22",
                  address,
                  address);
    if (rc < 0 || rc >= (int)sizeof(proxyjump_buf)) {
        fail_msg("snprintf failed");
    }
    rc = ssh_options_set(session, SSH_OPTIONS_PROXYJUMP, proxyjump_buf);
    assert_ssh_return_code(session, rc);
    rc = ssh_options_set(session, SSH_OPTIONS_PROXYJUMP_CB_LIST_APPEND, &c);
    assert_ssh_return_code(session, rc);
    rc = ssh_options_set(session, SSH_OPTIONS_PROXYJUMP_CB_LIST_APPEND, &c);
    assert_ssh_return_code(session, rc);

    rc = ssh_connect(session);
    assert_ssh_return_code(session, rc);

    fd = ssh_get_fd(session);
    assert_int_not_equal(fd, SSH_INVALID_SOCKET);

    rc = fcntl(fd, F_GETFL);
    assert_int_equal(rc & O_RDWR, O_RDWR);

    rc = ssh_userauth_publickey_auto(session, NULL, NULL);
    assert_int_equal(rc, SSH_AUTH_SUCCESS);
}

static void
torture_proxyjump_invalid_jump(void **state)
{
    struct torture_state *s = *state;
    ssh_session session = s->ssh.session;
    char proxyjump_buf[500] = {0};
    const char *address = torture_server_address(AF_INET);
    int rc;

    rc = snprintf(proxyjump_buf,
                  sizeof(proxyjump_buf),
                  "doesnotexist@%s:54",
                  address);
    if (rc < 0 || rc >= (int)sizeof(proxyjump_buf)) {
        fail_msg("snprintf failed");
    }
    rc = ssh_options_set(session, SSH_OPTIONS_PROXYJUMP, proxyjump_buf);
    assert_ssh_return_code(session, rc);

    rc = ssh_connect(session);
    assert_ssh_return_code_equal(session, rc, SSH_ERROR);
}

int
torture_run_tests(void)
{
    int rc;
    struct CMUnitTest tests[] = {
        cmocka_unit_test_setup_teardown(torture_proxyjump_single_jump,
                                        session_setup,
                                        session_teardown),
        cmocka_unit_test_setup_teardown(torture_proxyjump_multiple_jump,
                                        session_setup,
                                        session_teardown),
        cmocka_unit_test_setup_teardown(torture_proxyjump_invalid_jump,
                                        session_setup,
                                        session_teardown),
    };

    ssh_init();

    torture_filter_tests(tests);
    rc = cmocka_run_group_tests(tests, sshd_setup, sshd_teardown);
    ssh_finalize();

    return rc;
}
