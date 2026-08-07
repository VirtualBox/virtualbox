#define LIBSSH_STATIC

#include "config.h"

#include "torture.h"
#include "sftp.c"

#include <sys/types.h>
#include <pwd.h>
#include <errno.h>

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

static void torture_sftp_hardlink(void **state)
{
    struct torture_state *s = *state;
    struct torture_sftp *t = s->ssh.tsftp;

    char link_1[128] = {0};
    char link_2[128] = {0};
    int fd;
    int rc;

    snprintf(link_1, sizeof(link_1),
             "%s/libssh_sftp_hardlink_test_1", t->testdir);
    snprintf(link_2, sizeof(link_2),
             "%s/libssh_sftp_hardlink_test_2", t->testdir);

    fd = open(link_1, O_CREAT, S_IRWXU);
    assert_return_code(fd, errno);
    close(fd);

    rc = sftp_hardlink(t->sftp, link_1, link_2);
    assert_int_equal(rc, SSH_OK);

    /* check whether the file got associated with link_2 */ 
    rc = access(link_2, F_OK);
    assert_int_equal(rc, 0);

    unlink(link_1);
    unlink(link_2);

    /*
     * try to create a hardlink for a file that does not
     * exist, this should fail
     */
    rc = sftp_hardlink(t->sftp, link_1, link_2);
    assert_int_not_equal(rc, 0);
}

int torture_run_tests(void)
{
    int rc;
    struct CMUnitTest tests[] = {
        cmocka_unit_test_setup_teardown(torture_sftp_hardlink,
                                        session_setup,
                                        session_teardown)
    };

    ssh_init();

    torture_filter_tests(tests);
    rc = cmocka_run_group_tests(tests, sshd_setup, sshd_teardown);
    ssh_finalize();

    return rc;
}
