/*
 * This file is part of the SSH Library
 *
 * Copyright (c) 2013 by Andreas Schneider <asn@cryptomilk.org>
 *
 * The SSH Library is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation; either version 2.1 of the License, or (at your
 * option) any later version.
 *
 * The SSH Library is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
 * or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU Lesser General Public
 * License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with the SSH Library; see the file COPYING.  If not, write to
 * the Free Software Foundation, Inc., 59 Temple Place - Suite 330, Boston,
 * MA 02111-1307, USA.
 */

#include "config.h"

#define LIBSSH_STATIC

#include "torture.h"
#include <libssh/libssh.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <pwd.h>
#include <errno.h>
#include <termios.h>
#include <unistd.h>
#include <fcntl.h>
#include <pty.h>

static int sshd_setup(void **state)
{
    torture_setup_sshd_server(state, false);

    return 0;
}

static int sshd_teardown(void **state) {
    torture_teardown_sshd_server(state);

    return 0;
}

static int session_setup(void **state)
{
    struct torture_state *s = *state;
    struct passwd *pwd;
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

    return 0;
}

static int session_teardown(void **state)
{
    struct torture_state *s = *state;

    ssh_disconnect(s->ssh.session);
    ssh_free(s->ssh.session);

    return 0;
}

/* reads from the channel, expecting the given output */
static int check_channel_output(ssh_channel c, const char *expected)
{
    char buffer[4096] = {0};
    int nbytes, offset = 0;

    nbytes = ssh_channel_read(c, buffer, sizeof(buffer) - 1, 0);
    while (nbytes > 0) {
        buffer[offset + nbytes] = '\0';
        ssh_log_hexdump("Read bytes:",
                        (unsigned char *)buffer,
                        offset + nbytes);
        if (strstr(buffer, expected) != NULL)
        {
            return 1;
        }
        /* read on */
        offset = nbytes;
        nbytes = ssh_channel_read(c,
                                  buffer + offset,
                                  sizeof(buffer) - offset - 1,
                                  0);
    }
    return 0;
}

/* set explicit TTY modes and validate that the server uses them */
static void torture_request_pty_modes_translate_ocrnl(void **state)
{
    const unsigned char modes[] = {
        /* enable OCRNL */
        73, 0, 0, 0, 1,
        /* disable all other CR/NL handling */
        34, 0, 0, 0, 0,
        35, 0, 0, 0, 0,
        36, 0, 0, 0, 0,
        72, 0, 0, 0, 0,
        74, 0, 0, 0, 0,
        75, 0, 0, 0, 0,
        0, /* TTY_OP_END */
    };
    struct torture_state *s = *state;
    ssh_session session = s->ssh.session;
    ssh_channel c;
    int rc;
    int string_found = 0;

    c = ssh_channel_new(session);
    assert_non_null(c);

    rc = ssh_channel_open_session(c);
    assert_ssh_return_code(session, rc);

    rc = ssh_channel_request_pty_size_modes(c, "xterm", 80, 25, modes, sizeof(modes));
    assert_ssh_return_code(session, rc);

    rc = ssh_channel_request_exec(c, "/bin/echo -e '>TEST\\r\\n<'");
    assert_ssh_return_code(session, rc);

    /* expect 2 newline characters */
    string_found = check_channel_output(c, ">TEST\n\n<");
    assert_int_equal(string_found, 1);

    ssh_channel_close(c);
}

/* if stdin is a TTY, its modes are passed to the server */
static void torture_request_pty_modes_use_stdin_modes(void **state)
{
    struct torture_state *s = *state;
    ssh_session session = s->ssh.session;
    ssh_channel c;
    int rc;
    int string_found = 0;
    struct termios modes;
    int stdin_backup_fd = -1;
    int master_fd, slave_fd;

    c = ssh_channel_new(session);
    assert_non_null(c);

    rc = ssh_channel_open_session(c);
    assert_ssh_return_code(session, rc);

    /* stdin must be a TTY, so open one and replace the FD */
    stdin_backup_fd = dup(STDIN_FILENO);
    rc = openpty(&master_fd, &slave_fd, NULL, NULL, NULL);
    assert_int_equal(rc, 0);
    dup2(master_fd, STDIN_FILENO);
    assert_true(isatty(STDIN_FILENO));
    /* translate NL to CRNL on output to see a noticeable effect */
    memset(&modes, 0, sizeof(modes));
    tcgetattr(STDIN_FILENO, &modes);
    modes.c_oflag |= ONLCR;
    modes.c_iflag &= ~(ICRNL | INLCR | IGNCR);
    tcsetattr(STDIN_FILENO, TCSANOW, &modes);

    rc = ssh_channel_request_pty_size(c, "xterm", 80, 25);

    /* revert the changes to STDIN first! */
    dup2(stdin_backup_fd, STDIN_FILENO);
    close(stdin_backup_fd);
    close(master_fd);
    close(slave_fd);

    assert_ssh_return_code(session, rc);

    rc = ssh_channel_request_exec(c, "/bin/echo -e '>TEST\\r\\n<'");
    assert_ssh_return_code(session, rc);

    /* expect 2 carriage return characters + newline */
    string_found = check_channel_output(c, ">TEST\r\r\n<");
    assert_int_equal(string_found, 1);

    ssh_channel_close(c);
}

/* if stdin is NOT a TTY, default modes are passed to the server */
static void torture_request_pty_modes_use_default_modes(void **state)
{
    struct torture_state *s = *state;
    ssh_session session = s->ssh.session;
    ssh_channel c;
    int rc;
    int string_found = 0;
    int stdin_backup_fd = -1;

    c = ssh_channel_new(session);
    assert_non_null(c);

    rc = ssh_channel_open_session(c);
    assert_ssh_return_code(session, rc);

    /* stdin must not a TTY - change the FD to something else */
    stdin_backup_fd = dup(STDIN_FILENO);
    close(STDIN_FILENO);
    rc = open("/dev/null", O_RDONLY); // reuses FD 0 now
    assert_int_equal(rc, STDIN_FILENO);
    assert_false(isatty(STDIN_FILENO));

    rc = ssh_channel_request_pty_size(c, "xterm", 80, 25);

    /* revert the changes to STDIN first! */
    dup2(stdin_backup_fd, STDIN_FILENO);
    close(stdin_backup_fd);

    assert_ssh_return_code(session, rc);

    rc = ssh_channel_request_exec(c, "/bin/echo -e '>TEST\\r\\n<'");
    assert_ssh_return_code(session, rc);

    /* expect the CRLF translated to newline */
    string_found = check_channel_output(c, ">TEST\r\r\n<");
    assert_int_equal(string_found, 1);

    ssh_channel_close(c);
}

int torture_run_tests(void) {
    int rc;

    struct CMUnitTest tests[] = {
        cmocka_unit_test_setup_teardown(torture_request_pty_modes_translate_ocrnl,
                                        session_setup,
                                        session_teardown),
        cmocka_unit_test_setup_teardown(torture_request_pty_modes_use_stdin_modes,
                                        session_setup,
                                        session_teardown),
        cmocka_unit_test_setup_teardown(torture_request_pty_modes_use_default_modes,
                                        session_setup,
                                        session_teardown),
    };

    ssh_init();

    torture_filter_tests(tests);
    rc = cmocka_run_group_tests(tests, sshd_setup, sshd_teardown);

    ssh_finalize();
    return rc;
}

