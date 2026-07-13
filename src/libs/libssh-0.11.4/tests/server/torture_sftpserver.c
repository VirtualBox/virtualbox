/*
 * This file is part of the SSH Library
 *
 * Copyright (c) 2018 by Red Hat, Inc.
 *
 * Author: Anderson Toshiyuki Sasaki <ansasaki@redhat.com>
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
#include "libssh/sftp.h"

#define LIBSSH_STATIC

#include <sys/types.h>
#include <sys/stat.h>
#include <errno.h>
#include <pwd.h>

#include "torture.h"
#include "torture_key.h"
#include "libssh/libssh.h"
#include "libssh/priv.h"
#include "libssh/session.h"

#include "test_server.h"
#include "default_cb.h"

#define TORTURE_KNOWN_HOSTS_FILE "libssh_torture_knownhosts"

const char template[] = "temp_dir_XXXXXX";

struct test_server_st {
    struct torture_state *state;
    struct server_state_st *ss;
    char *cwd;
    char *temp_dir;
};

void sftp_handle_session_cb(ssh_event event,
                            ssh_session session,
                            struct server_state_st *state);

static void free_test_server_state(void **state)
{
    struct test_server_st *tss = *state;

    torture_free_state(tss->state);
    SAFE_FREE(tss);
}

static int setup_default_server(void **state)
{
    struct torture_state *s;
    struct server_state_st *ss;
    struct test_server_st *tss;

    char ed25519_hostkey[1024] = {0};
    char rsa_hostkey[1024];
    char ecdsa_hostkey[1024];
    //char trusted_ca_pubkey[1024];

    char sshd_path[1024];
    char log_file[1024];
    int rc;

    char pid_str[1024];

    pid_t pid;

    assert_non_null(state);

    tss = (struct test_server_st*)calloc(1, sizeof(struct test_server_st));
    assert_non_null(tss);

    torture_setup_socket_dir((void **)&s);
    assert_non_null(s->socket_dir);

    /* Set the default interface for the server */
    setenv("SOCKET_WRAPPER_DEFAULT_IFACE", "10", 1);
    setenv("PAM_WRAPPER", "1", 1);

    snprintf(sshd_path,
             sizeof(sshd_path),
             "%s/sshd",
             s->socket_dir);

    rc = mkdir(sshd_path, 0755);
    assert_return_code(rc, errno);

    snprintf(log_file,
             sizeof(log_file),
             "%s/sshd/log",
             s->socket_dir);

    snprintf(ed25519_hostkey,
             sizeof(ed25519_hostkey),
             "%s/sshd/ssh_host_ed25519_key",
             s->socket_dir);
    torture_write_file(ed25519_hostkey,
                       torture_get_openssh_testkey(SSH_KEYTYPE_ED25519, 0));

    snprintf(rsa_hostkey,
             sizeof(rsa_hostkey),
             "%s/sshd/ssh_host_rsa_key",
             s->socket_dir);
    torture_write_file(rsa_hostkey, torture_get_testkey(SSH_KEYTYPE_RSA, 0));

    snprintf(ecdsa_hostkey,
             sizeof(ecdsa_hostkey),
             "%s/sshd/ssh_host_ecdsa_key",
             s->socket_dir);
    torture_write_file(ecdsa_hostkey,
                       torture_get_testkey(SSH_KEYTYPE_ECDSA_P521, 0));

    /* Create default server state */
    ss = (struct server_state_st *)calloc(1, sizeof(struct server_state_st));
    assert_non_null(ss);

    ss->address = strdup("127.0.0.10");
    assert_non_null(ss->address);

    ss->port = 22;

    ss->ecdsa_key = strdup(ecdsa_hostkey);
    assert_non_null(ss->ecdsa_key);

    ss->ed25519_key = strdup(ed25519_hostkey);
    assert_non_null(ss->ed25519_key);

    ss->rsa_key = strdup(rsa_hostkey);
    assert_non_null(ss->rsa_key);

    ss->host_key = NULL;

    /* Use default username and password (set in default_handle_session_cb) */
    ss->expected_username = NULL;
    ss->expected_password = NULL;

    /* not to mix up the client and server messages */
    ss->verbosity = torture_libssh_verbosity();
    ss->log_file = strdup(log_file);

    ss->auth_methods = SSH_AUTH_METHOD_PASSWORD | SSH_AUTH_METHOD_PUBLICKEY;

#ifdef WITH_PCAP
    ss->with_pcap = 1;
    ss->pcap_file = strdup(s->pcap_file);
    assert_non_null(ss->pcap_file);
#endif

    /* TODO make configurable */
    ss->max_tries = 3;
    ss->error = 0;

    tss->state = s;
    tss->ss = ss;

    /* Use the default session handling function */
    ss->handle_session = sftp_handle_session_cb;
    assert_non_null(ss->handle_session);

    /* Do not use global configuration */
    ss->parse_global_config = false;

    /* Start the server using the default values */
    pid = fork_run_server(ss, free_test_server_state, &tss);
    if (pid < 0) {
        fail();
    }

    snprintf(pid_str, sizeof(pid_str), "%d", pid);

    torture_write_file(s->srv_pidfile, (const char *)pid_str);

    setenv("SOCKET_WRAPPER_DEFAULT_IFACE", "21", 1);
    unsetenv("PAM_WRAPPER");

    /* Wait until the sshd is ready to accept connections */
    rc = torture_wait_for_daemon(5);
    assert_int_equal(rc, 0);

    *state = tss;

    return 0;
}

static int teardown_default_server(void **state)
{
    struct torture_state *s;
    struct server_state_st *ss;
    struct test_server_st *tss;

    tss = *state;
    assert_non_null(tss);

    s = tss->state;
    assert_non_null(s);

    ss = tss->ss;
    assert_non_null(ss);

    /* This function can be reused */
    torture_teardown_sshd_server((void **)&s);

    free_server_state(tss->ss);
    SAFE_FREE(tss->ss);
    SAFE_FREE(tss);

    return 0;
}

static int session_setup(void **state)
{
    struct test_server_st *tss = *state;
    struct torture_state *s;
    int verbosity = torture_libssh_verbosity();
    char template2[] = "/tmp/ssh_torture_XXXXXX";
    char *cwd = NULL;
    char *tmp_dir = NULL;
    char *p = NULL;
    bool b = false;
    int rc;

    assert_non_null(tss);

    /* Make sure we do not test the agent */
    unsetenv("SSH_AUTH_SOCK");

    cwd = torture_get_current_working_dir();
    assert_non_null(cwd);

    tmp_dir = torture_make_temp_dir(template);
    p = mkdtemp(template2);
    assert_non_null(p);
    assert_non_null(tmp_dir);

    tss->cwd = cwd;
    tss->temp_dir = tmp_dir;

    s = tss->state;
    assert_non_null(s);

    s->ssh.session = ssh_new();
    assert_non_null(s->ssh.session);

    s->ssh.tsftp = (struct torture_sftp*)calloc(1, sizeof(struct torture_sftp));
    assert_non_null(s->ssh.tsftp);
    s->ssh.tsftp->testdir = strdup(p);
    assert_non_null(s->ssh.tsftp->testdir);

    rc = ssh_options_set(s->ssh.session, SSH_OPTIONS_LOG_VERBOSITY, &verbosity);
    assert_ssh_return_code(s->ssh.session, rc);
    rc = ssh_options_set(s->ssh.session, SSH_OPTIONS_HOST, TORTURE_SSH_SERVER);
    assert_ssh_return_code(s->ssh.session, rc);
    /* Make sure no other configuration options from system will get used */
    rc = ssh_options_set(s->ssh.session, SSH_OPTIONS_PROCESS_CONFIG, &b);
    assert_ssh_return_code(s->ssh.session, rc);

    return 0;
}

static int session_setup_sftp(void **state)
{
    struct test_server_st *tss = *state;
    struct torture_state *s = NULL;
    struct torture_sftp *tsftp = NULL;
    ssh_session session = NULL;
    sftp_session sftp = NULL;
    int rc;

    assert_non_null(tss);

    rc = session_setup(state);
    assert_int_equal(rc, 0);

    s = tss->state;
    assert_non_null(s);

    session = s->ssh.session;
    assert_non_null(session);

    rc = ssh_options_set(session, SSH_OPTIONS_USER, SSHD_DEFAULT_USER);
    assert_int_equal(rc, SSH_OK);

    rc = ssh_connect(session);
    assert_int_equal(rc, SSH_OK);

    rc = ssh_userauth_none(session, NULL);
    /* This request should return a SSH_REQUEST_DENIED error */
    if (rc == SSH_AUTH_ERROR) {
        assert_int_equal(ssh_get_error_code(session), SSH_REQUEST_DENIED);
    }
    rc = ssh_userauth_list(session, NULL);
    assert_true(rc & SSH_AUTH_METHOD_PASSWORD);

    rc = ssh_userauth_password(session, NULL, SSHD_DEFAULT_PASSWORD);
    assert_int_equal(rc, SSH_AUTH_SUCCESS);

    ssh_get_issue_banner(session);

    /* init sftp session */
    tsftp = s->ssh.tsftp;

    sftp = sftp_new(session);
    assert_non_null(sftp);
    tsftp->sftp = sftp;

    rc = sftp_init(sftp);
    assert_int_equal(rc, SSH_OK);

    return 0;
}

static int session_teardown(void **state)
{
    struct test_server_st *tss = *state;
    struct torture_state *s;
    int rc = 0;

    assert_non_null(tss);

    s = tss->state;
    assert_non_null(s);

    SAFE_FREE(s->ssh.tsftp->testdir);
    sftp_free(s->ssh.tsftp->sftp);
    SAFE_FREE(s->ssh.tsftp);

    ssh_disconnect(s->ssh.session);
    ssh_free(s->ssh.session);

    rc = torture_change_dir(tss->cwd);
    assert_int_equal(rc, 0);

    rc = torture_rmdirs(tss->temp_dir);
    assert_int_equal(rc, 0);

    SAFE_FREE(tss->temp_dir);
    SAFE_FREE(tss->cwd);

    return 0;
}

static void torture_server_establish_sftp(void **state)
{
    struct test_server_st *tss = *state;
    struct torture_state *s;
    struct torture_sftp *tsftp;
    ssh_session session;
    sftp_session sftp;
    int rc;

    assert_non_null(tss);

    s = tss->state;
    assert_non_null(s);

    session = s->ssh.session;
    assert_non_null(session);

    /* TODO: implement proper pam authentication in callback */
    /* Using the default user for the server */
    rc = ssh_options_set(session, SSH_OPTIONS_USER, SSHD_DEFAULT_USER);
    assert_int_equal(rc, SSH_OK);

    rc = ssh_connect(session);
    assert_int_equal(rc, SSH_OK);

    rc = ssh_userauth_none(session, NULL);
    /* This request should return a SSH_REQUEST_DENIED error */
    if (rc == SSH_AUTH_ERROR) {
        assert_int_equal(ssh_get_error_code(session), SSH_REQUEST_DENIED);
    }
    rc = ssh_userauth_list(session, NULL);
    assert_true(rc & SSH_AUTH_METHOD_PASSWORD);

    /* TODO: implement proper pam authentication in callback */
    /* Using the default password for the server */
    rc = ssh_userauth_password(session, NULL, SSHD_DEFAULT_PASSWORD);
    assert_int_equal(rc, SSH_AUTH_SUCCESS);

    ssh_get_issue_banner(session);

    /* init sftp session */
    tsftp = s->ssh.tsftp;

    printf("in establish before sftp_new\n");
    sftp = sftp_new(session);
    assert_non_null(sftp);

    rc = sftp_init(sftp);
    assert_int_equal(rc, SSH_OK);

    tsftp->sftp = sftp;
}

static void torture_server_test_sftp_function(void **state)
{
    struct test_server_st *tss = *state;
    struct torture_state *s;
    struct torture_sftp *tsftp;
    ssh_session session;
    sftp_session sftp;
    int rc;
    char *rv_str;
    sftp_dir dir;

    char data[65535] = {0};
    sftp_file source;
    sftp_file to;
    int read_len;
    int write_len;

    assert_non_null(tss);

    s = tss->state;
    assert_non_null(s);

    session = s->ssh.session;
    assert_non_null(session);

    rc = ssh_options_set(session, SSH_OPTIONS_USER, SSHD_DEFAULT_USER);
    assert_int_equal(rc, SSH_OK);

    rc = ssh_connect(session);
    assert_int_equal(rc, SSH_OK);

    rc = ssh_userauth_none(session, NULL);
    /* This request should return a SSH_REQUEST_DENIED error */
    if (rc == SSH_AUTH_ERROR) {
        assert_int_equal(ssh_get_error_code(session), SSH_REQUEST_DENIED);
    }
    rc = ssh_userauth_list(session, NULL);
    assert_true(rc & SSH_AUTH_METHOD_PASSWORD);

    /* TODO: implement proper pam authentication in callback */
    /* Using the default password for the server */
    rc = ssh_userauth_password(session, NULL, SSHD_DEFAULT_PASSWORD);
    assert_int_equal(rc, SSH_AUTH_SUCCESS);

    /* init sftp session */
    tsftp = s->ssh.tsftp;
    sftp = sftp_new(session);
    assert_non_null(sftp);
    tsftp->sftp = sftp;

    rc = sftp_init(sftp);
    assert_int_equal(rc, SSH_OK);

    /* symbol link */
    rc = sftp_symlink(sftp, "/tmp/this_is_the_link", "/tmp/sftp_symlink_test");
    assert_int_equal(rc, SSH_OK);

    rv_str = sftp_readlink(sftp, "/tmp/sftp_symlink_test");
    assert_non_null(rv_str);
    ssh_string_free_char(rv_str);

    rc = sftp_unlink(sftp, "/tmp/sftp_symlink_test");
    assert_int_equal(rc, SSH_OK);

    /* open and close dir */
    dir = sftp_opendir(sftp, "./");
    assert_non_null(dir);

    rc = sftp_closedir(dir);
    assert_int_equal(rc, SSH_OK);

    /* file read and write */
    source = sftp_open(sftp, "/usr/bin/ssh", O_RDONLY, 0);
    assert_non_null(source);

    to = sftp_open(sftp, "ssh-copy", O_WRONLY | O_CREAT, 0700);
    assert_non_null(to);

    read_len = sftp_read(source, data, 4096);
    write_len = sftp_write(to, data, read_len);
    assert_int_equal(write_len, read_len);

    rc = sftp_close(source);
    assert_int_equal(rc, SSH_OK);

    rc = sftp_close(to);
    assert_int_equal(rc, SSH_OK);

    rc = sftp_unlink(sftp, "ssh-copy");
    assert_int_equal(rc, SSH_OK);
}

static void torture_server_sftp_open_read_write(void **state)
{
    struct test_server_st *tss = *state;
    struct torture_state *s;
    struct torture_sftp *tsftp;
    sftp_session sftp;
    ssh_session session;
    sftp_attributes a = NULL;
    sftp_file new_file = NULL;
    char tmp_file[PATH_MAX] = {0};
    char data[10] = "0123456789";
    char read_data[10] = {0};
    struct stat sb;
    int rc, write_len, read_len;

    assert_non_null(tss);

    s = tss->state;
    assert_non_null(s);

    session = s->ssh.session;
    assert_non_null(session);

    tsftp = s->ssh.tsftp;
    assert_non_null(tsftp);

    sftp = tsftp->sftp;
    assert_non_null(sftp);

    snprintf(tmp_file, sizeof(tmp_file), "%s/newfile", tss->temp_dir);

    /*
     * Create a new file
     */
    new_file = sftp_open(sftp, tmp_file, O_WRONLY | O_CREAT, 0751);
    assert_non_null(new_file);

    /* Write should work ok */
    write_len = sftp_write(new_file, data, sizeof(data));
    assert_int_equal(write_len, sizeof(data));

    /* Reading should fail */
    read_len = sftp_read(new_file, read_data, sizeof(read_data));
    assert_int_equal(read_len, SSH_ERROR);

    rc = sftp_close(new_file);
    assert_ssh_return_code(session, rc);

    /* Verify locally the mode is correct */
    rc = stat(tmp_file, &sb);
    assert_int_equal(rc, 0);
    assert_int_equal(sb.st_mode, S_IFREG | 0751);
    assert_int_equal(sb.st_size, sizeof(data)); /* 10b written */

    /* Remote stat */
    a = sftp_stat(sftp, tmp_file);
    assert_non_null(a);
    assert_int_equal(a->permissions, S_IFREG | 0751);
    assert_int_equal(a->size, sizeof(data)); /* 10b written */
    assert_int_equal(a->type, SSH_FILEXFER_TYPE_REGULAR);
    sftp_attributes_free(a);

    /*
     * Now, lets try O_APPEND, mode is ignored
     */
    new_file = sftp_open(sftp, tmp_file, O_WRONLY | O_APPEND, 0);
    assert_non_null(new_file);

    /* fstat is not implemented */
    a = sftp_fstat(new_file);
    assert_null(a);

    /* Write should work ok */
    write_len = sftp_write(new_file, data, sizeof(data));
    assert_int_equal(write_len, sizeof(data));

    /* Reading should fail */
    read_len = sftp_read(new_file, read_data, sizeof(read_data));
    assert_int_equal(read_len, SSH_ERROR);

    rc = sftp_close(new_file);
    assert_ssh_return_code(session, rc);

    /*
     * Now, lets try read+write, mode is ignored
     */
    new_file = sftp_open(sftp, tmp_file, O_RDWR, 0);
    assert_non_null(new_file);

    /* Reading should work */
    read_len = sftp_read(new_file, read_data, sizeof(read_data));
    assert_int_equal(read_len, sizeof(read_data));
    assert_int_equal(sizeof(read_data), sizeof(data)); /* sanity */
    assert_memory_equal(read_data, data, sizeof(data));

    rc = sftp_seek(new_file, 20);
    assert_ssh_return_code(session, rc);

    /* Write should work also ok */
    write_len = sftp_write(new_file, data, sizeof(data));
    assert_int_equal(write_len, sizeof(data));

    rc = sftp_close(new_file);
    assert_ssh_return_code(session, rc);

    /* Remove the file */
    rc = sftp_unlink(sftp, tmp_file);
    assert_ssh_return_code(session, rc);

    /* again: the file does not exist anymore so we should fail now */
    rc = sftp_unlink(sftp, tmp_file);
    assert_int_equal(rc, SSH_ERROR);

    /*
     * Now, lets try read+write+create
     */
    new_file = sftp_open(sftp, tmp_file, O_RDWR | O_CREAT, 0700);
    assert_non_null(new_file);

    /* Reading should not fail but return no data */
    read_len = sftp_read(new_file, read_data, sizeof(read_data));
    assert_int_equal(read_len, 0);

    rc = sftp_close(new_file);
    assert_ssh_return_code(session, rc);

    /* be nice */
    rc = sftp_unlink(sftp, tmp_file);
    assert_ssh_return_code(session, rc);

    /* null flags should be invalid */
    /* but there is no way in libssh client to force null flags so skip this
    new_file = sftp_open(sftp, tmp_file, 0, 0700);
    assert_null(new_file);
    */

    /* Only O_CREAT is invalid on file which does not exist. Read is implicit */
    new_file = sftp_open(sftp, tmp_file, O_CREAT, 0700);
    assert_null(new_file);
}

static void torture_server_sftp_mkdir(void **state)
{
    struct test_server_st *tss = *state;
    struct torture_state *s;
    struct torture_sftp *tsftp;
    sftp_session sftp;
    ssh_session session;
    sftp_file new_file = NULL;
    char tmp_dir[PATH_MAX] = {0};
    char tmp_file[PATH_MAX] = {0};
    sftp_attributes a = NULL;
    struct stat sb;
    int rc;

    assert_non_null(tss);

    s = tss->state;
    assert_non_null(s);

    session = s->ssh.session;
    assert_non_null(session);

    tsftp = s->ssh.tsftp;
    assert_non_null(tsftp);

    sftp = tsftp->sftp;
    assert_non_null(sftp);

    snprintf(tmp_dir, sizeof(tmp_dir), "%s/newdir", tss->temp_dir);

    /* create a test dir */
    rc = sftp_mkdir(sftp, tmp_dir, 0751);
    assert_ssh_return_code(session, rc);

    /* try the same path again -- we should get an error */
    rc = sftp_mkdir(sftp, tmp_dir, 0751);
    assert_int_equal(rc, SSH_ERROR);

    /* Verify locally the mode is correct */
    rc = stat(tmp_dir, &sb);
    assert_int_equal(rc, 0);
    assert_int_equal(sb.st_mode, S_IFDIR | 0751);

    /* Remote stat */
    a = sftp_stat(sftp, tmp_dir);
    assert_non_null(a);
    assert_int_equal(a->permissions, S_IFDIR | 0751);
    assert_int_equal(a->type, SSH_FILEXFER_TYPE_DIRECTORY);
    sftp_attributes_free(a);

    snprintf(tmp_file, sizeof(tmp_file), "%s/newdir/newfile", tss->temp_dir);

    /* create a file in there */
    new_file = sftp_open(sftp, tmp_file, O_WRONLY | O_CREAT, 0700);
    assert_non_null(new_file);
    rc = sftp_close(new_file);
    assert_ssh_return_code(session, rc);

    /* remove of non-empty directory fails */
    rc = sftp_rmdir(sftp, tmp_dir);
    assert_int_equal(rc, SSH_ERROR);

    /* Unlink can not remove directory either */
    rc = sftp_unlink(sftp, tmp_dir);
    assert_int_equal(rc, SSH_ERROR);

    /* Remove the file */
    rc = sftp_unlink(sftp, tmp_file);
    assert_int_equal(rc, SSH_OK);

    /* Now it should work */
    rc = sftp_rmdir(sftp, tmp_dir);
    assert_ssh_return_code(session, rc);
}

static void torture_server_sftp_realpath(void **state)
{
    struct test_server_st *tss = *state;
    struct torture_state *s;
    struct torture_sftp *tsftp;
    sftp_session sftp;
    ssh_session session;
    char path[PATH_MAX] = {0};
    char exp_path[PATH_MAX] = {0};
    char *new_path = NULL;

    assert_non_null(tss);

    s = tss->state;
    assert_non_null(s);

    session = s->ssh.session;
    assert_non_null(session);

    tsftp = s->ssh.tsftp;
    assert_non_null(tsftp);

    sftp = tsftp->sftp;
    assert_non_null(sftp);

    /* first try with the empty string, which should be equivalent to CWD */
    new_path = sftp_canonicalize_path(sftp, path);
    assert_non_null(new_path);
    assert_string_equal(new_path, tss->cwd);
    ssh_string_free_char(new_path);

    /* now, lets try some more complicated paths relative to the CWD */
    snprintf(path, sizeof(path), "%s/.././%s",
             tss->temp_dir, tss->temp_dir);
    new_path = sftp_canonicalize_path(sftp, path);
    assert_non_null(new_path);
    snprintf(exp_path, sizeof(exp_path), "%s/%s",
             tss->cwd, tss->temp_dir);
    assert_string_equal(new_path, exp_path);
    ssh_string_free_char(new_path);

    /* and this one does not exists, which is an error */
    snprintf(path, sizeof(path), "%s/.././%s/nodir",
             tss->temp_dir, tss->temp_dir);
    new_path = sftp_canonicalize_path(sftp, path);
    assert_null(new_path);
    ssh_string_free_char(new_path);
}

static void torture_server_sftp_symlink(void **state)
{
    struct test_server_st *tss = *state;
    struct torture_state *s;
    struct torture_sftp *tsftp;
    sftp_session sftp;
    ssh_session session;
    sftp_file new_file = NULL;
    char tmp_dir[PATH_MAX] = {0};
    char tmp_file[PATH_MAX] = {0};
    char path[PATH_MAX] = {0};
    char abs_path[PATH_MAX] = {0};
    char data[42] = "012345678901234567890123456789012345678901";
    char *new_path = NULL;
    sftp_attributes a = NULL;
    sftp_dir dir;
    int write_len, num_files = 0;
    int rc;

    assert_non_null(tss);

    s = tss->state;
    assert_non_null(s);

    session = s->ssh.session;
    assert_non_null(session);

    tsftp = s->ssh.tsftp;
    assert_non_null(tsftp);

    sftp = tsftp->sftp;
    assert_non_null(sftp);

    /* create a test dir */
    snprintf(tmp_dir, sizeof(tmp_dir), "%s/newdir", tss->temp_dir);
    rc = sftp_mkdir(sftp, tmp_dir, 0751);
    assert_ssh_return_code(session, rc);

    /* create a file in there */
    snprintf(tmp_file, sizeof(tmp_file), "%s/%s/newdir/newfile",
             tss->cwd, tss->temp_dir);
    new_file = sftp_open(sftp, tmp_file, O_WRONLY | O_CREAT, 0700);
    assert_non_null(new_file);
    write_len = sftp_write(new_file, data, sizeof(data));
    assert_int_equal(write_len, sizeof(data));
    rc = sftp_close(new_file);
    assert_ssh_return_code(session, rc);

    /* now, lets create a (relative) symlink to the new file */
    snprintf(path, sizeof(path), "%s/newdir/linkname", tss->temp_dir);
    rc = sftp_symlink(sftp, tmp_file, path);
    assert_ssh_return_code(session, rc);

    /* when the destination exists, it should fail */
    rc = sftp_symlink(sftp, tmp_dir, tmp_file);
    assert_int_equal(rc, SSH_ERROR);

    /* now, there are different versions of stat that follow symlinks or not */
    /* lstat should not follow the symlink and show information about the link
     * itself */
    a = sftp_lstat(sftp, path);
    assert_non_null(a);
    assert_int_not_equal(a->size, sizeof(data));
    assert_int_equal(a->type, SSH_FILEXFER_TYPE_SYMLINK);
    sftp_attributes_free(a);

    /* readlink should give us more information about the target of the symlink
     */
    new_path = sftp_readlink(sftp, path);
    assert_non_null(new_path);
    snprintf(abs_path, sizeof(abs_path), "%s/%s/newdir/newfile",
             tss->cwd, tss->temp_dir);
    assert_string_equal(new_path, abs_path);
    ssh_string_free_char(new_path);

    /* stat should follow the symlink and show information about the link
     * target */
    a = sftp_stat(sftp, path);
    assert_non_null(a);
    assert_int_equal(a->size, sizeof(data));
    assert_int_equal(a->permissions, S_IFREG | 0700);
    assert_int_equal(a->type, SSH_FILEXFER_TYPE_REGULAR);
    sftp_attributes_free(a);

    /* on non-existing path, they fail */
    a = sftp_lstat(sftp, "non-existing");
    assert_null(a);
    a = sftp_stat(sftp, "non-existing");
    assert_null(a);

    /**** readdir ****/
    dir = sftp_opendir(sftp, tmp_dir);
    assert_non_null(dir);
    while ((a = sftp_readdir(sftp, dir))) {
        if (strcmp(a->name, ".") != 0 &&
            strcmp(a->name, "..") != 0 &&
            strcmp(a->name, "newfile") != 0 &&
            strcmp(a->name, "linkname") != 0) {
            /* There is a file we did not create */
            assert_true(false);
        }

        num_files++;
        sftp_attributes_free(a);
    }
    assert_int_equal(num_files, 4);
    rc = sftp_dir_eof(dir);
    assert_int_equal(rc, 1);
    rc = sftp_closedir(dir);
    assert_ssh_return_code(session, rc);

    /* now, remove the target of the link, the stat should not handle that,
     * while lstat should keep working */
    rc = sftp_unlink(sftp, tmp_file);
    assert_int_equal(rc, SSH_OK);

    a = sftp_lstat(sftp, path);
    assert_non_null(a);
    assert_int_not_equal(a->size, sizeof(data));
    sftp_attributes_free(a);

    a = sftp_stat(sftp, path);
    assert_null(a);

    /* readlink works ok on broken symlinks */
    new_path = sftp_readlink(sftp, path);
    assert_non_null(new_path);
    snprintf(abs_path, sizeof(abs_path), "%s/%s/newdir/newfile",
             tss->cwd, tss->temp_dir);
    assert_string_equal(new_path, abs_path);
    ssh_string_free_char(new_path);

    /* readlink should fail on directories */
    new_path = sftp_readlink(sftp, tmp_dir);
    assert_null(new_path);
    /* readlink should fail on or on non-existing files */
    new_path = sftp_readlink(sftp, tmp_file);
    assert_null(new_path);

    /* Clean up symlink */
    rc = sftp_unlink(sftp, path);
    assert_int_equal(rc, SSH_OK);
    /* Clean up temporary directory */
    rc = sftp_rmdir(sftp, tmp_dir);
    assert_int_equal(rc, SSH_OK);
}

static void torture_server_sftp_extended(void **state)
{
    struct test_server_st *tss = *state;
    struct torture_state *s;
    struct torture_sftp *tsftp;
    sftp_session sftp;
    ssh_session session;
    sftp_file new_file = NULL;
    char tmp_dir[PATH_MAX] = {0};
    char tmp_file[PATH_MAX] = {0};
    sftp_statvfs_t st = NULL;
    int rc;

    assert_non_null(tss);

    s = tss->state;
    assert_non_null(s);

    session = s->ssh.session;
    assert_non_null(session);

    tsftp = s->ssh.tsftp;
    assert_non_null(tsftp);

    sftp = tsftp->sftp;
    assert_non_null(sftp);

    /* create a test dir */
    snprintf(tmp_dir, sizeof(tmp_dir), "%s/newdir", tss->temp_dir);
    rc = sftp_mkdir(sftp, tmp_dir, 0751);
    assert_ssh_return_code(session, rc);

    /* create a file in there */
    snprintf(tmp_file, sizeof(tmp_file), "%s/%s/newdir/newfile",
             tss->cwd, tss->temp_dir);
    new_file = sftp_open(sftp, tmp_file, O_WRONLY | O_CREAT, 0700);
    assert_non_null(new_file);

    /* extended fstatvsf is not advertised nor supported now but calling this
     * message will keep hanging the server. The extension protocol says that
     * the clients can not request extension that are not supported by the
     * server so before doing this, we should use sftp_extension_supported()
     * anyway */
    /* st = sftp_fstatvfs(new_file);
    assert_null(st); */

    /* close */
    rc = sftp_close(new_file);
    assert_ssh_return_code(session, rc);

    /* extended statvsf */
    st = sftp_statvfs(sftp, tmp_file);
    assert_non_null(st);
    /* probably hard to check more */
    sftp_statvfs_free(st);

    /* Clean up temporary directory */
    rc = sftp_unlink(sftp, tmp_file);
    assert_int_equal(rc, SSH_OK);
    rc = sftp_rmdir(sftp, tmp_dir);
    assert_int_equal(rc, SSH_OK);
}

static void
torture_server_sftp_setstat(void **state)
{

    char name[128] = {0};
    char data[10] = "0123456789";
    int rc;
    size_t len;
    int atime = 10676, mtime = 13467;
    mode_t mode = S_IRUSR | S_IWUSR | S_IRGRP;

    struct passwd *pwd = NULL;
    struct test_server_st *tss = *state;
    struct torture_state *s = NULL;
    struct torture_sftp *tsftp = NULL;
    struct sftp_attributes_struct attr;
    sftp_attributes tmp_attr = NULL;

    sftp_session sftp = NULL;
    ssh_session session = NULL;
    sftp_file new_file = NULL;

    pwd = getpwnam("alice");
    assert_non_null(pwd);

    assert_non_null(tss);

    s = tss->state;
    assert_non_null(s);

    session = s->ssh.session;
    assert_non_null(session);

    tsftp = s->ssh.tsftp;
    assert_non_null(tsftp);

    sftp = tsftp->sftp;
    assert_non_null(sftp);
    assert_non_null(tsftp->testdir);
    snprintf(name, sizeof(name), "%s/server_setstat_test", tsftp->testdir);
    new_file = sftp_open(sftp, name, O_WRONLY | O_CREAT, 0700);
    assert_non_null(new_file);
    len = sftp_write(new_file, data, sizeof(data));
    assert_int_equal(len, sizeof(data));
    rc = sftp_close(new_file);
    assert_int_equal(rc, SSH_OK);

    ZERO_STRUCT(attr);
    attr.flags = SSH_FILEXFER_ATTR_SIZE | SSH_FILEXFER_ATTR_PERMISSIONS |
                 SSH_FILEXFER_ATTR_UIDGID | SSH_FILEXFER_ATTR_ACMODTIME;

    attr.size = len;
    attr.uid = pwd->pw_uid;
    attr.gid = pwd->pw_gid;
    attr.permissions = mode;
    attr.atime = atime;
    attr.mtime = mtime;

    rc = sftp_setstat(sftp, name, &attr);
    assert_int_equal(rc, SSH_OK);

    assert_int_equal(rc, SSH_OK);

    tmp_attr = sftp_stat(sftp, name);
    assert_non_null(tmp_attr);

    assert_int_equal(tmp_attr->uid, pwd->pw_uid);
    assert_int_equal(tmp_attr->gid, pwd->pw_gid);

    assert_int_equal(len, tmp_attr->size);
    assert_int_equal(tmp_attr->permissions & ACCESSPERMS, mode);
    assert_int_equal(tmp_attr->mtime, mtime);
    assert_int_equal(tmp_attr->atime, atime);

    /*negative tests*/
    rc = sftp_setstat(sftp, "not existing", &attr);
    assert_int_equal(rc, SSH_ERROR);
    sftp_unlink(sftp, name);
    sftp_attributes_free(tmp_attr);
}

/* The max number of handles is 256 in sftpserver.h -- keep in sync! */
#define SFTP_HANDLES 256
static void torture_server_sftp_handles_exhaustion(void **state)
{
    struct test_server_st *tss = *state;
    struct torture_state *s = NULL;
    struct torture_sftp *tsftp = NULL;
    char name[128] = {0};
    sftp_file handle, handles[SFTP_HANDLES] = {0};
    sftp_session sftp = NULL;
    int rc;

    assert_non_null(tss);

    s = tss->state;
    assert_non_null(s);

    tsftp = s->ssh.tsftp;
    assert_non_null(tsftp);

    sftp = tsftp->sftp;
    assert_non_null(sftp);

    /* Occupy all handles */
    for (int i = 0; i < SFTP_HANDLES; i++) {
        snprintf(name, sizeof(name), "%s/fn%d", tsftp->testdir, i);
        handles[i] = sftp_open(sftp, name, O_WRONLY | O_CREAT, 0700);
        assert_non_null(handles[i]);
    }

    /* Next handle should fail, but not crash or OOB */
    snprintf(name, sizeof(name), "%s/failfn", tsftp->testdir);
    handle = sftp_open(sftp, name, O_WRONLY | O_CREAT, 0700);
    assert_null(handle);

    /* cleanup */
    for (int i = 0; i < SFTP_HANDLES; i++) {
        snprintf(name, sizeof(name), "%s/fn%d", tsftp->testdir, i);
        rc = sftp_close(handles[i]);
        assert_int_equal(rc, SSH_OK);
    }
}


int torture_run_tests(void) {
    int rc;
    struct CMUnitTest tests[] = {
        cmocka_unit_test_setup_teardown(torture_server_establish_sftp,
                                        session_setup,
                                        session_teardown),
        cmocka_unit_test_setup_teardown(torture_server_test_sftp_function,
                                        session_setup,
                                        session_teardown),
        cmocka_unit_test_setup_teardown(torture_server_sftp_open_read_write,
                                        session_setup_sftp,
                                        session_teardown),
        cmocka_unit_test_setup_teardown(torture_server_sftp_mkdir,
                                        session_setup_sftp,
                                        session_teardown),
        cmocka_unit_test_setup_teardown(torture_server_sftp_realpath,
                                        session_setup_sftp,
                                        session_teardown),
        cmocka_unit_test_setup_teardown(torture_server_sftp_symlink,
                                        session_setup_sftp,
                                        session_teardown),
        cmocka_unit_test_setup_teardown(torture_server_sftp_extended,
                                        session_setup_sftp,
                                        session_teardown),
        cmocka_unit_test_setup_teardown(torture_server_sftp_setstat,
                                        session_setup_sftp,
                                        session_teardown),
        cmocka_unit_test_setup_teardown(torture_server_sftp_handles_exhaustion,
                                        session_setup_sftp,
                                        session_teardown),
    };

    ssh_init();

    torture_filter_tests(tests);
    rc = cmocka_run_group_tests(tests,
            setup_default_server,
            teardown_default_server);

    ssh_finalize();

    return rc;
}
