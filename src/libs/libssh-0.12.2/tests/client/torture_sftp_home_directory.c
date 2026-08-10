#include "config.h"

#define LIBSSH_STATIC

#include "sftp.c"
#include "torture.h"

#include <errno.h>
#include <pwd.h>
#include <stdlib.h>
#include <sys/types.h>

static int
sshd_setup(void **state)
{
    /*
      The SFTP server used for testing is executed as a separate binary, which
      is making the uid_wrapper lose information about what user is used, and
      therefore, pwd is initialized to some bad value.
      If the embedded version using internal-sftp is used in sshd, it works ok.
     */
    setenv("TORTURE_SFTP_SERVER", "internal-sftp", 1);
    torture_setup_sshd_server(state, false);
    return 0;
}

static int
sshd_teardown(void **state)
{
    unsetenv("TORTURE_SFTP_SERVER");
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
torture_sftp_home_directory(void **state)
{
    struct torture_state *s = *state;
    struct torture_sftp *t = s->ssh.tsftp;
    struct passwd *pwd = NULL;
    char *home_path = NULL;
    int rc;

    rc = sftp_extension_supported(t->sftp, "home-directory", "1");
    if (!rc) {
        skip();
    }

    pwd = getpwnam(TORTURE_SSH_USER_ALICE);
    assert_non_null(pwd);

    /* testing for NULL sftp session */
    home_path = sftp_home_directory(NULL, NULL);
    assert_null(home_path);

    /* testing for ~ */
    /*
    home_path = sftp_home_directory(t->sftp, NULL);
    assert_non_null(home_path);
    assert_string_equal(home_path, pwd->pw_dir);
    SSH_STRING_FREE_CHAR(home_path);

    home_path = sftp_home_directory(t->sftp, "");
    assert_non_null(home_path);
    assert_string_equal(home_path, pwd->pw_dir);
    SSH_STRING_FREE_CHAR(home_path);
    */

    /*
      OpenSSH code handling this extension does not handle empty string for
      username. getpwnam() also does not handle empty string.
      PR in OpenSSH for fix:
      https://github.com/openssh/openssh-portable/pull/477/
    */

    /* testing for ~user */
    home_path = sftp_home_directory(t->sftp, pwd->pw_name);
    fprintf(stderr,
            "sftp error: %d, ssh error: %s\n",
            sftp_get_error(t->sftp),
            ssh_get_error(t->sftp->session));
    assert_non_null(home_path);
    assert_string_equal(home_path, pwd->pw_dir);
    SSH_STRING_FREE_CHAR(home_path);
}

int
torture_run_tests(void)
{
    int rc;
    struct CMUnitTest tests[] = {
        cmocka_unit_test_setup_teardown(torture_sftp_home_directory,
                                        session_setup,
                                        session_teardown)};

    ssh_init();

    torture_filter_tests(tests);
    rc = cmocka_run_group_tests(tests, sshd_setup, sshd_teardown);
    ssh_finalize();

    return rc;
}
