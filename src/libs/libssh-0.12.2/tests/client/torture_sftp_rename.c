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

static void torture_sftp_rename(void **state)
{
    struct torture_state *s = *state;
    struct torture_sftp *t = s->ssh.tsftp;

    char name_1[128] = {0};
    char name_2[128] = {0};

    int fd;
    int rc;

    snprintf(name_1, sizeof(name_1),
             "%s/libssh_sftp_rename_test_1", t->testdir);
    snprintf(name_2, sizeof(name_2),
             "%s/libssh_sftp_rename_test_2", t->testdir);

    fd = open(name_1, O_CREAT, S_IRWXU);
    assert_return_code(fd, errno);
    close(fd);

    /* try to rename an existing file */
    rc = sftp_rename(t->sftp, name_1, name_2);
    assert_int_equal(rc, SSH_OK);

    /* check whether any file with name_1 exists, it shouldn't */
    rc = access(name_1, F_OK);
    assert_int_not_equal(rc, 0);

    /* check whether file with name_2 exists, it should */
    rc = access(name_2, F_OK);
    assert_int_equal(rc, 0);

    unlink(name_2);

    /*
     * try to rename a file that does not exist,
     * this should fail (-ve test case)
     */
    rc = sftp_rename(t->sftp, name_1, name_2);
    assert_int_not_equal(rc, 0);
}


int torture_run_tests(void)
{
    int rc;
    struct CMUnitTest tests[] = {
        cmocka_unit_test_setup_teardown(torture_sftp_rename,
                                        session_setup,
                                        session_teardown)
    };

    ssh_init();

    torture_filter_tests(tests);
    rc = cmocka_run_group_tests(tests, sshd_setup, sshd_teardown);
    ssh_finalize();

    return rc;
}
