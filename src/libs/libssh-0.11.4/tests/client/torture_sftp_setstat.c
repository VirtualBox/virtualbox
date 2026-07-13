#define LIBSSH_STATIC

#include "config.h"

#include "libssh/sftp.h"
#include "sftp.c"
#include "torture.h"

#include <errno.h>
#include <fcntl.h>
#include <pwd.h>
#include <sys/stat.h>
#include <sys/types.h>

static int
sshd_setup(void **state)
{
    /*
     * The OpenSSH invokes the sftp server command with execve(), which does
     * not inherit the environment variables (including LD_PRELOAD, which
     * is needed for the fs_wrapper). Using `internal-sftp` works around this,
     * keeping the old environment around.
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
session_setup_setstat(void **state)
{

    struct torture_state *s = *state;
    struct torture_sftp *t = NULL;
    struct passwd *pwd = NULL;
    static char name[128] = {0};
    const char *test_1 = "l&setstat_test\n";
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

    t = s->ssh.tsftp;

    snprintf(name, sizeof(name), "%s/libssh_sftp_setstat_test", t->testdir);
    torture_write_file(name, test_1);
    s->private_data = name;

    return 0;
}

static int
session_setup_lsetstat(void **state)
{

    struct torture_state *s = *state;
    struct torture_sftp *t = NULL;
    struct passwd *pwd = NULL;
    static char path[128] = {0};
    const char *test_1 = "lsetstat_test_1\n";
    int rc;

    char tmp_file[128] = {0};

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

    t = s->ssh.tsftp;

    rc = sftp_extension_supported(t->sftp, "lsetstat@openssh.com", "1");
    if (rc == 0) {
        skip();
    }

    snprintf(tmp_file, sizeof(tmp_file), "%s/newfile", t->testdir);
    torture_write_file(tmp_file, test_1);

    snprintf(path, sizeof(path), "%s/linkname", t->testdir);
    rc = symlink(tmp_file, path);
    assert_int_equal(rc, SSH_OK);
    s->private_data = path;

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

/*setstat tests*/
static void
torture_sftp_setstat_chown(void **state)
{
    struct torture_state *s = *state;
    struct torture_sftp *t = s->ssh.tsftp;
    struct sftp_attributes_struct attr;
    struct passwd *pwd = NULL;
    sftp_attributes tmp_attr = NULL;
    const char *name = (char *)s->private_data;
    int rc;

    ZERO_STRUCT(attr);

    pwd = getpwnam("alice");
    assert_non_null(pwd);

    attr.uid = pwd->pw_uid;
    attr.gid = pwd->pw_gid;
    attr.flags = SSH_FILEXFER_ATTR_UIDGID;

    rc = sftp_setstat(t->sftp, name, &attr);
    assert_int_equal(rc, SSH_OK);
    tmp_attr = sftp_stat(t->sftp, name);
    assert_non_null(tmp_attr);
    assert_int_equal(tmp_attr->uid, pwd->pw_uid);
    assert_int_equal(tmp_attr->gid, pwd->pw_gid);
    sftp_attributes_free(tmp_attr);
}

static void
torture_sftp_setstat_size(void **state)
{
    struct torture_state *s = *state;
    struct torture_sftp *t = s->ssh.tsftp;
    int rc;
    size_t len = 30;
    struct sftp_attributes_struct attr;
    struct stat sb;
    const char *name = (char *)s->private_data;

    ZERO_STRUCT(attr);
    attr.flags = SSH_FILEXFER_ATTR_SIZE;
    attr.size = len;
    rc = sftp_setstat(t->sftp, name, &attr);
    assert_int_equal(rc, SSH_OK);

    rc = stat(name, &sb);
    assert_int_equal(rc, SSH_OK);

    assert_int_equal(len, sb.st_size);
}

static void
torture_sftp_setstat_chmod(void **state)
{
    mode_t mode = S_IRUSR | S_IWUSR | S_IRGRP;
    struct torture_state *s = *state;
    struct torture_sftp *t = s->ssh.tsftp;
    int rc;
    struct sftp_attributes_struct attr;
    struct stat sb;
    const char *name = (char *)s->private_data;

    ZERO_STRUCT(attr);

    attr.flags = SSH_FILEXFER_ATTR_PERMISSIONS;
    attr.permissions = mode;

    rc = sftp_setstat(t->sftp, name, &attr);
    assert_int_equal(rc, SSH_OK);

    rc = stat(name, &sb);
    assert_int_equal(rc, SSH_OK);

    assert_int_equal(sb.st_mode & ACCESSPERMS, mode);
}

static void
torture_sftp_setstat_utimes(void **state)
{
    struct torture_state *s = *state;
    struct torture_sftp *t = s->ssh.tsftp;
    int rc;
    struct sftp_attributes_struct attr;
    struct stat sb;
    int atime = 10676, mtime = 13467;
    const char *name = (char *)s->private_data;

    ZERO_STRUCT(attr);

    attr.flags = SSH_FILEXFER_ATTR_ACMODTIME;
    attr.mtime = mtime;
    attr.atime = atime;

    rc = sftp_setstat(t->sftp, name, &attr);
    assert_int_equal(rc, SSH_OK);

    rc = stat(name, &sb);
    assert_int_equal(rc, SSH_OK);
    assert_int_equal(sb.st_mtime, mtime);
    assert_int_equal(sb.st_atime, atime);
}

static void
torture_sftp_setstat_negative(void **state)
{
    struct torture_state *s = *state;
    struct torture_sftp *t = s->ssh.tsftp;
    const char *name = (char *)s->private_data;
    int rc;
    struct sftp_attributes_struct attr;

    ZERO_STRUCT(attr);

    attr.flags = SSH_FILEXFER_ATTR_ACMODTIME | SSH_FILEXFER_ATTR_UIDGID |
                 SSH_FILEXFER_ATTR_PERMISSIONS | SSH_FILEXFER_ATTR_SIZE;

    /* testing null sftp */
    rc = sftp_setstat(NULL, name, &attr);
    assert_int_equal(rc, SSH_ERROR);

    /* testing non-existing file */
    rc = sftp_setstat(t->sftp, "not existing", &attr);
    assert_int_equal(rc, SSH_ERROR);

    /* testing null attributes */
    rc = sftp_setstat(t->sftp, name, NULL);
    assert_int_equal(rc, SSH_ERROR);
}

/*lsetstat tests*/
static void
torture_sftp_lsetstat_chown(void **state)
{
    struct torture_state *s = *state;
    struct torture_sftp *t = s->ssh.tsftp;
    struct passwd *pwd = NULL;
    const char *name = (char *)s->private_data;
    int rc;
    struct sftp_attributes_struct attr;
    sftp_attributes tmp_attr = NULL;

    ZERO_STRUCT(attr);

    pwd = getpwnam("alice");
    assert_non_null(pwd);

    attr.flags = SSH_FILEXFER_ATTR_UIDGID;
    attr.uid = pwd->pw_uid;
    attr.gid = pwd->pw_gid;
    rc = sftp_lsetstat(t->sftp, name, &attr);
    assert_int_equal(rc, SSH_OK);

    tmp_attr = sftp_lstat(t->sftp, name);
    assert_non_null(tmp_attr);
    assert_int_equal(tmp_attr->uid, pwd->pw_uid);
    assert_int_equal(tmp_attr->gid, pwd->pw_gid);
    sftp_attributes_free(tmp_attr);
}

static void
torture_sftp_lsetstat_utimes(void **state)
{
    struct torture_state *s = *state;
    struct torture_sftp *t = s->ssh.tsftp;
    const char *name = (char *)s->private_data;
    int rc;
    struct sftp_attributes_struct attr;
    struct stat sb;
    int atime = 10676, mtime = 13467;

    ZERO_STRUCT(attr);

    attr.flags = SSH_FILEXFER_ATTR_ACMODTIME;
    attr.mtime = mtime;
    attr.atime = atime;

    rc = sftp_lsetstat(t->sftp, name, &attr);
    assert_int_equal(rc, SSH_OK);

    rc = lstat(name, &sb);
    assert_int_equal(rc, SSH_OK);
    assert_int_equal(sb.st_mtime, mtime);
    assert_int_equal(sb.st_atime, atime);
}

static void
torture_sftp_lsetstat_negative(void **state)
{
    struct torture_state *s = *state;
    struct torture_sftp *t = s->ssh.tsftp;
    const char *name = (char *)s->private_data;
    int rc;
    struct sftp_attributes_struct attr;

    ZERO_STRUCT(attr);

    attr.flags = SSH_FILEXFER_ATTR_ACMODTIME | SSH_FILEXFER_ATTR_UIDGID;

    /* testing non-existing file */
    rc = sftp_lsetstat(t->sftp, "not existing", &attr);
    assert_int_equal(rc, SSH_ERROR);

    /* testing null attributes */
    rc = sftp_lsetstat(t->sftp, name, NULL);
    assert_int_equal(rc, SSH_ERROR);

    /* testing null sftp */
    rc = sftp_lsetstat(NULL, name, &attr);
    assert_int_equal(rc, SSH_ERROR);
}

int
torture_run_tests(void)
{
    int rc;
    struct CMUnitTest tests[] = {
        cmocka_unit_test_setup_teardown(torture_sftp_setstat_chown,
                                        session_setup_setstat,
                                        session_teardown),
        cmocka_unit_test_setup_teardown(torture_sftp_setstat_chmod,
                                        session_setup_setstat,
                                        session_teardown),
        cmocka_unit_test_setup_teardown(torture_sftp_setstat_utimes,
                                        session_setup_setstat,
                                        session_teardown),
        cmocka_unit_test_setup_teardown(torture_sftp_setstat_size,
                                        session_setup_setstat,
                                        session_teardown),
        cmocka_unit_test_setup_teardown(torture_sftp_setstat_negative,
                                        session_setup_setstat,
                                        session_teardown),
        cmocka_unit_test_setup_teardown(torture_sftp_lsetstat_utimes,
                                        session_setup_lsetstat,
                                        session_teardown),
        cmocka_unit_test_setup_teardown(torture_sftp_lsetstat_chown,
                                        session_setup_lsetstat,
                                        session_teardown),
        cmocka_unit_test_setup_teardown(torture_sftp_lsetstat_negative,
                                        session_setup_lsetstat,
                                        session_teardown)};

    ssh_init();

    torture_filter_tests(tests);
    rc = cmocka_run_group_tests(tests, sshd_setup, sshd_teardown);
    ssh_finalize();
    return rc;
}
