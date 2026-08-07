/*
 * This is a regression test to make sure that sftp_read_packet times out
 * properly in blocking mode
 */

#define LIBSSH_STATIC

#include "config.h"

#include "sftp.c"
#include "torture.h"

#include <errno.h>
#include <libssh/socket.h>
#include <pwd.h>
#include <sys/types.h>

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

static int
session_teardown(void **state)
{
    struct torture_state *s = *state;

    torture_rmdirs(s->ssh.tsftp->testdir);
    torture_sftp_close(s->ssh.tsftp);
    ssh_disconnect(s->ssh.session);
    ssh_free(s->ssh.session);

    return 0;
}

static void
torture_sftp_packet_read(void **state)
{
    struct torture_state *s = *state;
    struct torture_sftp *t = s->ssh.tsftp;
    sftp_packet packet = NULL;

    int fds[2];
    int rc;

    /* creating blocking fd is the default pipe behaviour */
    rc = pipe(fds);
    assert_return_code(rc, errno);

    t->ssh->opts.timeout = 1;
    ssh_socket_set_fd(t->ssh->socket, fds[0]);

    /*
     * Making sure that the sftp_packet_read function times out and returns
     * NULL.
     */
    packet = sftp_packet_read(t->sftp);
    assert_null(packet);

    close(fds[0]);
    close(fds[1]);
}

int
torture_run_tests(void)
{
    int rc;
    struct CMUnitTest tests[] = {
        cmocka_unit_test_setup_teardown(torture_sftp_packet_read,
                                        session_setup,
                                        session_teardown),
    };

    ssh_init();

    torture_filter_tests(tests);
    rc = cmocka_run_group_tests(tests, sshd_setup, sshd_teardown);
    ssh_finalize();

    return rc;
}
