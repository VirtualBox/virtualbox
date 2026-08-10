#include "config.h"

#define LIBSSH_STATIC

#include "sftp.c"
#include "torture.h"

#include <pwd.h>
#include <sys/types.h>

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

static void torture_sftp_request_id_null(void **state)
{
    struct torture_state *s = *state;
    struct torture_sftp *t = s->ssh.tsftp;
    sftp_session sftp = t->sftp;
    int rc;

    rc = sftp_get_new_id(sftp, NULL);
    assert_int_equal(rc, SSH_ERROR);
}

static void torture_sftp_request_id_add(void **state)
{
    struct torture_state *s = *state;
    struct torture_sftp *t = s->ssh.tsftp;
    sftp_session sftp = t->sftp;
    uint32_t id1, id2;
    int rc;
    size_t count;

    /* The list of IDs should be empty at first */
    count = ssh_list_count(sftp->outstanding_ids);
    assert_int_equal(count, 0);

    /* Request a new ID */
    rc = sftp_get_new_id(sftp, &id1);
    assert_int_equal(rc, SSH_OK);

    /* Check that the list has one ID now */
    count = ssh_list_count(sftp->outstanding_ids);
    assert_int_equal(count, 1);

    /* Request another ID */
    rc = sftp_get_new_id(sftp, &id2);
    assert_int_equal(rc, SSH_OK);

    /* Check that the IDs differ */
    assert_int_not_equal(id1, id2);

    /* Check that the list has two IDs now */
    count = ssh_list_count(sftp->outstanding_ids);
    assert_int_equal(count, 2);
}

static void torture_sftp_request_id_remove(void **state)
{
    struct torture_state *s = *state;
    struct torture_sftp *t = s->ssh.tsftp;
    sftp_session sftp = t->sftp;
    sftp_attributes attr = NULL;
    size_t count;

    count = ssh_list_count(sftp->outstanding_ids);
    assert_int_equal(count, 0);

    /* We send a request and receive a response */
    attr = sftp_stat(sftp, SSH_EXECUTABLE);
    assert_non_null(attr);

    /* The number of outstanding requests should be back to 0 */
    count = ssh_list_count(sftp->outstanding_ids);
    assert_int_equal(count, 0);

    sftp_attributes_free(attr);
}

static void torture_sftp_request_id_unknown(void **state)
{
    struct torture_state *s = *state;
    struct torture_sftp *t = s->ssh.tsftp;
    sftp_session sftp = t->sftp;
    ssh_buffer buffer = NULL;
    sftp_message msg = NULL;
    uint32_t id = 0;
    int rc;
    size_t count;

    count = ssh_list_count(sftp->outstanding_ids);
    assert_int_equal(count, 0);

    buffer = ssh_buffer_new();
    assert_non_null(buffer);

    rc = ssh_buffer_pack(buffer, "ds", id, "/tmp");
    assert_int_equal(rc, SSH_OK);

    /* Send a request without saving the request ID */
    rc = sftp_packet_write(sftp, SSH_FXP_OPENDIR, buffer);
    assert_int_not_equal(rc, -1);
    SSH_BUFFER_FREE(buffer);

    /* An attempt to receive the response should fail */
    rc = sftp_recv_response_msg(sftp, id, true, &msg);
    assert_int_equal(rc, SSH_ERROR);
}

int torture_run_tests(void)
{
    int rc;
    struct CMUnitTest tests[] = {
        cmocka_unit_test_setup_teardown(torture_sftp_request_id_null,
                                        session_setup,
                                        session_teardown),
        cmocka_unit_test_setup_teardown(torture_sftp_request_id_add,
                                        session_setup,
                                        session_teardown),
        cmocka_unit_test_setup_teardown(torture_sftp_request_id_remove,
                                        session_setup,
                                        session_teardown),
        cmocka_unit_test_setup_teardown(torture_sftp_request_id_unknown,
                                        session_setup,
                                        session_teardown),
    };

    ssh_init();

    torture_filter_tests(tests);
    rc = cmocka_run_group_tests(tests, sshd_setup, sshd_teardown);
    ssh_finalize();

    return rc;
}
