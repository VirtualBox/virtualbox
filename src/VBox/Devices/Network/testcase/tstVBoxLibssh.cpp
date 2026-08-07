/* $Id: tstVBoxLibssh.cpp 114891 2026-08-07 11:59:56Z aleksey.ilyushin@oracle.com $ */
/** @file
 * tstVBoxLibssh - Testcase for the libssh. Requires sshd with keys configured on some host.
 */

/*
 * Copyright (C) 2026 Oracle and/or its affiliates.
 *
 * This file is part of VirtualBox base platform packages, as
 * available from https://www.virtualbox.org.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation, in version 3 of the
 * License.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <https://www.gnu.org/licenses>.
 *
 * SPDX-License-Identifier: GPL-3.0-only
 */


/*********************************************************************************************************************************
*   Header Files                                                                                                                 *
*********************************************************************************************************************************/
#include <VBox/err.h>

#include <iprt/assert.h>
#include <iprt/env.h>
#include <iprt/getopt.h>
#include <iprt/initterm.h>
#include <iprt/mem.h>
#include <iprt/path.h>
#include <iprt/string.h>
#include <iprt/test.h>

#ifdef RT_OS_WINDOWS
# include <iprt/win/windows.h>
typedef int socklen_t;
#else
# include <errno.h>
 typedef int SOCKET;
# define closesocket close
# define INVALID_SOCKET -1
# define SOCKET_ERROR   -1
#endif

#define _WINSOCK2API_
#include <libssh/libssh.h>

static char g_szHost[64] { 0 };
static char g_szUser[64] { 0 };
static char g_szKeyFile[RTPATH_MAX] { 0 };
static char g_szCommand[64] { 0 };
static char g_szExpected[64] { 0 };

static int runSessionAndExec(void)
{
    RTTestISub("Session and exec");

    int rc = VINF_SUCCESS;
    ssh_session sess = NULL;
    ssh_key key = NULL;
    ssh_channel channel = NULL;

    RTTestIDisableAssertions();

    do
    {
        RTTestIPrintf(RTTESTLVL_ALWAYS, "Testing with libssh-" SSH_STRINGIFY(LIBSSH_VERSION) "\n");
        RTTestIPrintf(RTTESTLVL_ALWAYS,
                      "Connecting to host=%s user=%s key=%s\n",
                      g_szHost, g_szUser, g_szKeyFile);

        rc = ssh_init();
        if (rc != SSH_OK)
        {
            RTTestIFailed("ssh_init failed: %d", rc);
            rc = VERR_INTERNAL_ERROR;
            break;
        }

        sess = ssh_new();
        if (!sess)
        {
            RTTestIFailed("ssh_new failed");
            rc = VERR_NO_MEMORY;
            break;
        }

        int cTimeout = 30;
        if (ssh_options_set(sess, SSH_OPTIONS_HOST, g_szHost) < 0)
        {
            RTTestIFailed("ssh_options_set(SSH_OPTIONS_HOST) failed");
            rc = VERR_INTERNAL_ERROR;
            break;
        }
        if (ssh_options_set(sess, SSH_OPTIONS_USER, g_szUser) < 0)
        {
            RTTestIFailed("ssh_options_set(SSH_OPTIONS_USER) failed");
            rc = VERR_INTERNAL_ERROR;
            break;
        }
        if (ssh_options_set(sess, SSH_OPTIONS_TIMEOUT, &cTimeout) < 0)
        {
            RTTestIFailed("ssh_options_set(SSH_OPTIONS_TIMEOUT) failed");
            rc = VERR_INTERNAL_ERROR;
            break;
        }

        int rcLibssh = ssh_pki_import_privkey_file(g_szKeyFile, NULL, NULL, NULL, &key);
        if (rcLibssh != SSH_OK)
        {
            RTTestIFailed("ssh_pki_import_privkey_file failed: %d", rcLibssh);
            rc = VERR_NOT_FOUND;
            break;
        }

        rcLibssh = ssh_connect(sess);
        if (rcLibssh != SSH_OK)
        {
            RTTestIFailed("ssh_connect failed: %s", ssh_get_error(sess));
            rc = VERR_OPEN_FAILED;
            break;
        }

        rcLibssh = ssh_userauth_publickey(sess, NULL, key);
        if (rcLibssh != SSH_AUTH_SUCCESS)
        {
            RTTestIFailed("ssh_userauth_publickey failed: %s", ssh_get_error(sess));
            rc = VERR_AUTHENTICATION_FAILURE;
            break;
        }

        channel = ssh_channel_new(sess);
        if (!channel)
        {
            RTTestIFailed("ssh_channel_new failed");
            rc = VERR_NO_MEMORY;
            break;
        }

        rcLibssh = ssh_channel_open_session(channel);
        if (rcLibssh != SSH_OK)
        {
            RTTestIFailed("ssh_channel_open_session failed: %s", ssh_get_error(sess));
            rc = VERR_OPEN_FAILED;
            break;
        }

        rcLibssh = ssh_channel_request_exec(channel, g_szCommand);
        if (rcLibssh != SSH_OK)
        {
            RTTestIFailed("ssh_channel_request_exec failed: %s", ssh_get_error(sess));
            rc = VERR_INTERNAL_ERROR;
            break;
        }

        char szStdout[8192];
        char szStderr[8192];
        RT_ZERO(szStdout);
        RT_ZERO(szStderr);

        int cbStdout = 0;
        for (;;)
        {
            int cbRead = ssh_channel_read_timeout(channel,
                                                 szStdout + cbStdout,
                                                 sizeof(szStdout) - 1 - cbStdout,
                                                 0 /* stdout */,
                                                 5000 /* ms */);
            if (cbRead < 0)
            {
                RTTestIFailed("ssh_channel_read_timeout(stdout) failed");
                rc = VERR_INTERNAL_ERROR;
                break;
            }
            if (cbRead == 0)
                break;
            cbStdout += cbRead;
            if (cbStdout >= (int)sizeof(szStdout) - 1)
                break;
        }
        if (RT_FAILURE(rc))
            break;
        szStdout[cbStdout] = '\0';

        int cbStderr = 0;
        for (;;)
        {
            int cbRead = ssh_channel_read_timeout(channel,
                                                 szStderr + cbStderr,
                                                 sizeof(szStderr) - 1 - cbStderr,
                                                 1 /* stderr */,
                                                 1000 /* ms */);
            if (cbRead < 0)
            {
                RTTestIFailed("ssh_channel_read_timeout(stderr) failed");
                rc = VERR_INTERNAL_ERROR;
                break;
            }
            if (cbRead == 0)
                break;
            cbStderr += cbRead;
            if (cbStderr >= (int)sizeof(szStderr) - 1)
                break;
        }
        if (RT_FAILURE(rc))
            break;
        szStderr[cbStderr] = '\0';

        if (!strstr(szStdout, g_szExpected))
        {
            RTTestIFailed("stdout did not contain expected substring");
            RTTestIPrintf(RTTESTLVL_ALWAYS, "stdout was:\n%s\n", szStdout);
            rc = VERR_INTERNAL_ERROR;
            break;
        }

        if (cbStderr != 0)
        {
            RTTestIFailed("stderr was not expected to contain output");
            RTTestIPrintf(RTTESTLVL_ALWAYS, "stderr was:\n%s\n", szStderr);
            rc = VERR_INTERNAL_ERROR;
            break;
        }

        rcLibssh = ssh_channel_send_eof(channel);
        if (rcLibssh != SSH_OK)
        {
            RTTestIFailed("ssh_channel_send_eof failed: %s", ssh_get_error(sess));
            rc = VERR_INTERNAL_ERROR;
            break;
        }

        rcLibssh = ssh_channel_close(channel);
        if (rcLibssh != SSH_OK)
        {
            RTTestIFailed("ssh_channel_close failed: %s", ssh_get_error(sess));
            rc = VERR_INTERNAL_ERROR;
            break;
        }

        RTTestIPrintf(RTTESTLVL_ALWAYS, "stdout:\n%s\n", szStdout);
        RTTestIPrintf(RTTESTLVL_ALWAYS, "stderr:\n%s\n", szStderr);

    } while (0);

    if (channel)
        ssh_channel_free(channel);

    if (sess)
    {
        ssh_disconnect(sess);
        ssh_free(sess);
    }

    if (key)
        ssh_key_free(key);

    ssh_finalize();

    RTTestIRestoreAssertions();
    RTTestISubDone();
    return rc;
}

static RTEXITCODE printHelp(int argc, char **argv)
{
    RT_NOREF(argc);

    RTTestIPrintf(RTTESTLVL_ALWAYS,
                 "Usage: %s --host <host> --user <user> --key <private-key-file>"
                 " <command> <expected-substring>\n", argv[0]);

    return RTEXITCODE_SUCCESS; /* Don't report any error here to not upset the testboxes. */
}

int main(int argc, char **argv)
{
    RTTEST hTest;
    RTEXITCODE rcExit = RTTestInitAndCreate("tstVBoxLibssh", &hTest);
    if (rcExit != RTEXITCODE_SUCCESS)
        return rcExit;

    RTTestBanner(hTest);

    RTGETOPTSTATE               GetState;
    RTGETOPTUNION               ValueUnion;
    static const RTGETOPTDEF    s_aOptions[] =
    {
        { "--host", 's', RTGETOPT_REQ_STRING }, // ssh host to connect to
        { "--user",   'u', RTGETOPT_REQ_STRING }, // user at the ssh host
        { "--key" ,   'k', RTGETOPT_REQ_STRING }  // private key to use for authentication
    };
    int rc = RTGetOptInit(&GetState, argc, argv, s_aOptions, RT_ELEMENTS(s_aOptions), 1,  RTGETOPTINIT_FLAGS_OPTS_FIRST);
    AssertRCReturn(rc, rc);

    int ch;
    while (  (ch = RTGetOpt(&GetState, &ValueUnion)) != 0
           && ch != VINF_GETOPT_NOT_OPTION)
    {
        switch (ch)
        {
            case 's':
                RTStrCopy(g_szHost, sizeof(g_szHost), ValueUnion.psz);
                break;

            case 'u':
                RTStrCopy(g_szUser, sizeof(g_szUser), ValueUnion.psz);
                break;

            case 'k':
                RTStrCopy(g_szKeyFile, sizeof(g_szKeyFile), ValueUnion.psz);
                break;

            case 'h':
                return printHelp(argc, argv);

            default:
                RTGetOptPrintError(ch, &ValueUnion);
                return RTTestSkipAndDestroy(hTest, "Invalid arguments.\n");
        }
    }

    if (ch != VINF_GETOPT_NOT_OPTION)
        return RTTestSummaryAndDestroy(hTest);

    int iArg = 0;
    char **papszArgs = RTGetOptNonOptionArrayPtr(&GetState);
    if (papszArgs[iArg])
        RTStrCopy(g_szCommand, sizeof(g_szCommand), papszArgs[iArg++]);
    if (papszArgs[iArg])
        RTStrCopy(g_szExpected, sizeof(g_szExpected), papszArgs[iArg++]);
    if (iArg != 2)
        return RTTestSummaryAndDestroy(hTest);

    rc = runSessionAndExec();
    if (RT_FAILURE(rc))
        RTTestIFailed("runSessionAndExec failed with %Rrc", rc);

    return RTTestSummaryAndDestroy(hTest);
}
