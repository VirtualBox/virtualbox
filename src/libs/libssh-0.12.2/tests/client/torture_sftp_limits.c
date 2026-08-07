#define LIBSSH_STATIC

#include "config.h"

#include "torture.h"
#include "sftp.c"

#include <sys/types.h>
#include <sys/resource.h>
#include <pwd.h>
#include <errno.h>

#if HAVE_VALGRIND_VALGRIND_H
 #include <valgrind/valgrind.h>
#endif

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

static void torture_sftp_limits(void **state)
{
    struct torture_state *s = *state;
    struct torture_sftp *t = s->ssh.tsftp;
    sftp_limits_t li = NULL;
    int rc;

    li = sftp_limits(t->sftp);
    assert_non_null(li);

    rc = sftp_extension_supported(t->sftp, "limits@openssh.com", "1");
    if (rc == 1) {
        /*
         * Tests are run against the OpenSSH server, hence we check for the
         * specific limits used by OpenSSH.
         */
        uint64_t openssh_max_packet_length = 256 * 1024;
        uint64_t openssh_max_read_length = openssh_max_packet_length - 1024;
        uint64_t openssh_max_write_length = openssh_max_packet_length - 1024;
        size_t vg = 0;

        assert_int_equal(li->max_packet_length, openssh_max_packet_length);
        assert_int_equal(li->max_read_length, openssh_max_read_length);
        assert_int_equal(li->max_write_length, openssh_max_write_length);

        /*
         * fds - File descriptors, w.r.to - With respect to
         *
         * Valgrind reserves some fds for itself and changes the rlimits
         * w.r.to fds for the process its inspecting. Due to this reservation
         * the rlimits w.r.to fds for our test may not be the same as the
         * rlimits w.r.to fds seen by OpenSSH server (which Valgrind isn't
         * inspecting).
         *
         * Valgrind changes the limits in such a way that after seeing the
         * changed limits, the test cannot predict the original unchanged
         * limits (which OpenSSH would be using). Hence, the test cannot
         * determine the correct value of "max_open_handles" that the OpenSSH
         * server should've sent.
         *
         * So if Valgrind is running our test, we don't provide any kind of
         * check for max_open_handles. Check for >= 0 is also not provided in
         * this case since that's always true for an uint64_t (an unsigned type)
         */
#if HAVE_VALGRIND_VALGRIND_H
        vg = RUNNING_ON_VALGRIND;
#endif

        if (vg == 0) {
            struct rlimit rlim = {0};
            uint64_t openssh_max_open_handles = 0;

            /*
             * Get the resource limit for max file descriptors that a process
             * can open. Since the client and the server run on the same machine
             * in case of tests, this limit should be same for both (except the
             * case when Valgrind runs the test)
             */
            rc = getrlimit(RLIMIT_NOFILE, &rlim);
            assert_int_equal(rc, 0);
            if (rlim.rlim_cur > 5) {
                /*
                 * Leaving file handles for stdout, stdin, stderr, syslog and
                 * a spare file handle, OpenSSH server allows the client to open
                 * at max (rlim.rlim_cur - 5) handles.
                 */
                openssh_max_open_handles = rlim.rlim_cur - 5;
            }

            assert_int_equal(li->max_open_handles, openssh_max_open_handles);
        }
    } else {
        /* Check for the default limits */
        assert_int_equal(li->max_packet_length, 34000);
        assert_int_equal(li->max_read_length, 32768);
        assert_int_equal(li->max_write_length, 32768);
        assert_int_equal(li->max_open_handles, 0);
    }

    sftp_limits_free(li);
}

static void torture_sftp_limits_negative(void **state)
{
    sftp_limits_t li = NULL;

    (void)state;
    li = sftp_limits(NULL);
    assert_null(li);
}

int torture_run_tests(void)
{
    int rc;
    struct CMUnitTest tests[] = {
        cmocka_unit_test_setup_teardown(torture_sftp_limits,
                                        session_setup,
                                        session_teardown),

        cmocka_unit_test_setup_teardown(torture_sftp_limits_negative,
                                        session_setup,
                                        session_teardown)
    };

    ssh_init();

    torture_filter_tests(tests);
    rc = cmocka_run_group_tests(tests, sshd_setup, sshd_teardown);
    ssh_finalize();

    return rc;
}
