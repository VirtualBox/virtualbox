#include "config.h"

#define LIBSSH_STATIC

#include "sftp.c"
#include "torture.h"

#include <errno.h>
#include <pwd.h>
#include <sys/types.h>

static int sshd_setup(void **state)
{
    /*
     * The SFTP server used for testing is executed as a separate binary, which
     * is making the uid_wrapper lose information about what user is used, and
     * therefore, pwd is initialized to some bad value.
     * If the embedded version using internal-sftp is used in sshd, it works ok.
     */
    setenv("TORTURE_SFTP_SERVER", "internal-sftp", 1);
    torture_setup_sshd_server(state, false);
    return 0;
}

static int sshd_teardown(void **state)
{
    unsetenv("TORTURE_SFTP_SERVER");
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

static void torture_sftp_get_users_by_id(void **state)
{
    struct torture_state *s = *state;
    struct torture_sftp *t = s->ssh.tsftp;
    struct passwd *alice_pwd = NULL;
    struct passwd *bob_pwd = NULL;
    struct passwd *root_pwd = NULL;
    sftp_name_id_map users_map = NULL;
    int rc;

    rc = sftp_extension_supported(t->sftp,
                                  "users-groups-by-id@openssh.com",
                                  "1");
    if (rc == 0) {
        skip();
    }

    alice_pwd = getpwnam("alice");
    assert_non_null(alice_pwd);

    bob_pwd = getpwnam("bob");
    assert_non_null(bob_pwd);

    root_pwd = getpwnam("root");
    assert_non_null(root_pwd);

    /* test for null */
    rc = sftp_get_users_groups_by_id(t->sftp, NULL, NULL);
    assert_int_equal(rc, -1);

    /* test for 0 users */
    users_map = sftp_name_id_map_new(0);

    rc = sftp_get_users_groups_by_id(t->sftp, users_map, NULL);
    assert_int_equal(rc, 0);

    sftp_name_id_map_free(users_map);

    /* test for 3 users */
    users_map = sftp_name_id_map_new(3);

    users_map->ids[0] = alice_pwd->pw_uid;
    users_map->ids[1] = bob_pwd->pw_uid;
    users_map->ids[2] = root_pwd->pw_uid;

    rc = sftp_get_users_groups_by_id(t->sftp, users_map, NULL);
    assert_int_equal(rc, 0);
    assert_string_equal(users_map->names[0], "alice");
    assert_string_equal(users_map->names[1], "bob");
    assert_string_equal(users_map->names[2], "root");

    sftp_name_id_map_free(users_map);

    /* test for invalid uids */
    users_map = sftp_name_id_map_new(2);

    users_map->ids[0] = alice_pwd->pw_uid;
    users_map->ids[1] = 42; /* invalid uid */
    rc = sftp_get_users_groups_by_id(t->sftp, users_map, NULL);

    assert_int_equal(rc, 0);
    assert_string_equal(users_map->names[0], "alice");
    assert_string_equal(users_map->names[1], "");

    sftp_name_id_map_free(users_map);
}

static void torture_sftp_get_groups_by_id(void **state)
{
    struct torture_state *s = *state;
    struct torture_sftp *t = s->ssh.tsftp;
    struct passwd *alice_pwd = NULL;
    struct passwd *root_pwd = NULL;
    sftp_name_id_map groups_map = NULL;
    int rc;

    rc = sftp_extension_supported(t->sftp,
                                  "users-groups-by-id@openssh.com",
                                  "1");

    if (rc == 0) {
        skip();
    }

    alice_pwd = getpwnam("alice");
    assert_non_null(alice_pwd);

    root_pwd = getpwnam("root");
    assert_non_null(root_pwd);

    /* test for 2 groups */
    groups_map = sftp_name_id_map_new(2);

    groups_map->ids[0] = alice_pwd->pw_gid;
    groups_map->ids[1] = root_pwd->pw_gid;

    rc = sftp_get_users_groups_by_id(t->sftp, NULL, groups_map);
    assert_int_equal(rc, 0);
    assert_string_equal(groups_map->names[0], "users");
    assert_string_equal(groups_map->names[1], "root");

    sftp_name_id_map_free(groups_map);

    /* test for invalid gids */
    groups_map = sftp_name_id_map_new(2);

    groups_map->ids[0] = alice_pwd->pw_gid;
    groups_map->ids[1] = 42; /* invalid gid */

    rc = sftp_get_users_groups_by_id(t->sftp, NULL, groups_map);
    assert_int_equal(rc, 0);
    assert_string_equal(groups_map->names[0], "users");
    assert_string_equal(groups_map->names[1], "");

    sftp_name_id_map_free(groups_map);
}

static void torture_sftp_get_users_groups_by_id(void **state)
{
    struct torture_state *s = *state;
    struct torture_sftp *t = s->ssh.tsftp;
    struct passwd *alice_pwd = NULL;
    struct passwd *bob_pwd = NULL;
    struct passwd *root_pwd = NULL;
    sftp_name_id_map users_map = NULL;
    sftp_name_id_map groups_map = NULL;
    int rc;

    rc = sftp_extension_supported(t->sftp,
                                  "users-groups-by-id@openssh.com",
                                  "1");

    if (rc == 0) {
        skip();
    }

    alice_pwd = getpwnam("alice");
    assert_non_null(alice_pwd);

    bob_pwd = getpwnam("bob");
    assert_non_null(bob_pwd);

    root_pwd = getpwnam("root");
    assert_non_null(root_pwd);

    users_map = sftp_name_id_map_new(4);
    groups_map = sftp_name_id_map_new(3);

    users_map->ids[0] = alice_pwd->pw_uid;
    users_map->ids[1] = bob_pwd->pw_uid;
    users_map->ids[2] = root_pwd->pw_uid;
    users_map->ids[3] = 42; /* invalid uid */

    groups_map->ids[0] = alice_pwd->pw_gid;
    groups_map->ids[1] = root_pwd->pw_gid;
    groups_map->ids[2] = 42; /* invalid gid */

    rc = sftp_get_users_groups_by_id(t->sftp, users_map, groups_map);

    assert_int_equal(rc, 0);
    assert_string_equal(users_map->names[0], "alice");
    assert_string_equal(users_map->names[1], "bob");
    assert_string_equal(users_map->names[2], "root");
    assert_string_equal(users_map->names[3], "");
    assert_string_equal(groups_map->names[0], "users");
    assert_string_equal(groups_map->names[1], "root");
    assert_string_equal(groups_map->names[2], "");

    sftp_name_id_map_free(users_map);
    sftp_name_id_map_free(groups_map);
}

int torture_run_tests(void)
{
    int rc;
    struct CMUnitTest tests[] = {
        cmocka_unit_test_setup_teardown(torture_sftp_get_users_by_id,
                                        session_setup,
                                        session_teardown),
        cmocka_unit_test_setup_teardown(torture_sftp_get_groups_by_id,
                                        session_setup,
                                        session_teardown),
        cmocka_unit_test_setup_teardown(torture_sftp_get_users_groups_by_id,
                                        session_setup,
                                        session_teardown)};

    ssh_init();

    torture_filter_tests(tests);
    rc = cmocka_run_group_tests(tests, sshd_setup, sshd_teardown);

    ssh_finalize();

    return rc;
}
