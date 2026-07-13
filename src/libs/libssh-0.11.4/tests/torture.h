/*
 * torture.c - torture library for testing libssh
 *
 * This file is part of the SSH Library
 *
 * Copyright (c) 2008-2009 by Andreas Schneider <asn@cryptomilk.org>
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

#ifndef _TORTURE_H
#define _TORTURE_H

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>

#include "libssh/priv.h"
#include "libssh/server.h"
#include "libssh/sftp.h"

#include <cmocka.h>

#include "torture_cmocka.h"
#include "tests_config.h"

#ifndef assert_return_code
/* hack for older versions of cmocka */
#define assert_return_code(code, errno) \
    assert_true(code >= 0)
#endif /* assert_return_code */

#define TORTURE_SSH_SERVER "127.0.0.10"
#define TORTURE_SSH_SERVER_IP6 "fd00::5357:5f0a"
#define TORTURE_SSH_USER_BOB "bob"
#define TORTURE_SSH_USER_BOB_PASSWORD "secret"

#define TORTURE_SSH_USER_ALICE "alice"
#define TORTURE_SSH_USER_CHARLIE "charlie"

/* Used by main to communicate with parse_opt. */
struct argument_s {
  const char *pattern;
  int verbose;
};

struct torture_sftp {
    ssh_session ssh;
    sftp_session sftp;
    char *testdir;
};

struct torture_state {
    char *socket_dir;
    char *gss_dir;
    char *pcap_file;
    char *log_file;
    char *srv_pidfile;
    char *srv_config;
    bool srv_pam;
    char *srv_additional_config;
    struct {
        ssh_session session;
        struct torture_sftp *tsftp;
    } ssh;
#ifdef WITH_PCAP
    ssh_pcap_file plain_pcap;
#endif
    void *private_data;
};

#ifndef ZERO_STRUCT
#define ZERO_STRUCT(x) memset((char *)&(x), 0, sizeof(x))
#endif

void torture_cmdline_parse(int argc, char **argv, struct argument_s *arguments);

int torture_rmdirs(const char *path);
int torture_isdir(const char *path);

int torture_terminate_process(const char *pidfile);

/*
 * Returns the verbosity level asked by user
 */
int torture_libssh_verbosity(void);

ssh_session torture_ssh_session(struct torture_state *s,
                                const char *host,
                                const unsigned int *port,
                                const char *user,
                                const char *password);

ssh_bind torture_ssh_bind(const char *addr,
                          const unsigned int port,
                          enum ssh_keytypes_e key_type,
                          const char *private_key_file);

struct torture_sftp *torture_sftp_session(ssh_session session);
struct torture_sftp *torture_sftp_session_channel(ssh_session session, ssh_channel channel);
void torture_sftp_close(struct torture_sftp *t);

void torture_write_file(const char *filename, const char *data);

#define torture_filter_tests(tests) _torture_filter_tests(tests, sizeof(tests) / sizeof(tests)[0])
void _torture_filter_tests(struct CMUnitTest *tests, size_t ntests);

const char *torture_server_address(int domain);
int torture_server_port(void);

int torture_wait_for_daemon(unsigned int seconds);

#ifdef SSHD_EXECUTABLE
void torture_setup_socket_dir(void **state);
void torture_setup_sshd_server(void **state, bool pam);

void torture_teardown_socket_dir(void **state);
void torture_teardown_sshd_server(void **state);

int torture_update_sshd_config(void **state, const char *config);
#endif /* SSHD_EXECUTABLE */

#ifdef WITH_PKCS11_URI
void torture_setup_tokens(const char *temp_dir,
                          const char *filename,
                          const char object_name[],
                          const char *load_public);
void torture_cleanup_tokens(const char *temp_dir);
#endif /* WITH_PKCS11_URI */

void torture_reset_config(ssh_session session);

void torture_setup_create_libssh_config(void **state);

void torture_setup_libssh_server(void **state, const char *server_path);

#ifdef WITH_GSSAPI
void torture_setup_kdc_server(void **state,
                              const char *kadmin_script,
                              const char *kinit_script);
void torture_teardown_kdc_server(void **state);
void torture_set_kdc_env_str(const char *gss_dir, char *env, size_t size);
void torture_set_env_from_str(const char *env);
#endif /* WITH_GSSAPI */

#if defined(HAVE_WEAK_ATTRIBUTE) && defined(TORTURE_SHARED)
__attribute__((weak)) int torture_run_tests(void);
#else
/*
 * This function must be defined in every unit test file.
 */
int torture_run_tests(void);
#endif

void torture_free_state(struct torture_state *s);

char *torture_make_temp_dir(const char *template);
char *torture_create_temp_file(const char *template);

char *torture_get_current_working_dir(void);
int torture_change_dir(char *path);

void torture_setenv(char const* variable, char const* value);
void torture_unsetenv(char const* variable);

void torture_finalize(void);

#endif /* _TORTURE_H */
