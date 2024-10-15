/* $Id: process-posix.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - Process, POSIX.
 */

/*
 * Copyright (C) 2006-2024 Oracle and/or its affiliates.
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
 * The contents of this file may alternatively be used under the terms
 * of the Common Development and Distribution License Version 1.0
 * (CDDL), a copy of it is provided in the "COPYING.CDDL" file included
 * in the VirtualBox distribution, in which case the provisions of the
 * CDDL are applicable instead of those of the GPL.
 *
 * You may elect to license modified versions of this file under the
 * terms and conditions of either the GPL or the CDDL or both.
 *
 * SPDX-License-Identifier: GPL-3.0-only OR CDDL-1.0
 */



/*********************************************************************************************************************************
*   Header Files                                                                                                                 *
*********************************************************************************************************************************/
#define LOG_GROUP RTLOGGROUP_PROCESS
#include <unistd.h>
#include <stdlib.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <signal.h>
#include <pwd.h>

#include <iprt/process.h>
#include "internal/iprt.h"

#include <iprt/assert.h>
#include <iprt/env.h>
#include <iprt/err.h>
#include <iprt/file.h>
#include <iprt/pipe.h>
#include <iprt/socket.h>
#include <iprt/string.h>
#include <iprt/mem.h>
#include "internal/process.h"


RTR3DECL(int)   RTProcWait(RTPROCESS Process, unsigned fFlags, PRTPROCSTATUS pProcStatus)
{
    int rc;
    do rc = RTProcWaitNoResume(Process, fFlags, pProcStatus);
    while (rc == VERR_INTERRUPTED);
    return rc;
}


RTR3DECL(int)   RTProcWaitNoResume(RTPROCESS Process, unsigned fFlags, PRTPROCSTATUS pProcStatus)
{
    /*
     * Validate input.
     */
    if (Process <= 0)
    {
        AssertMsgFailed(("Invalid Process=%d\n", Process));
        return VERR_INVALID_PARAMETER;
    }
    if (fFlags & ~(RTPROCWAIT_FLAGS_NOBLOCK | RTPROCWAIT_FLAGS_BLOCK))
    {
        AssertMsgFailed(("Invalid flags %#x\n", fFlags));
        return VERR_INVALID_PARAMETER;
    }

    /*
     * Perform the wait.
     */
    int iStatus = 0;
    int rc = waitpid(Process, &iStatus, fFlags & RTPROCWAIT_FLAGS_NOBLOCK ? WNOHANG : 0);
    if (rc > 0)
    {
        /*
         * Fill in the status structure.
         */
        if (pProcStatus)
        {
            if (WIFEXITED(iStatus))
            {
                pProcStatus->enmReason = RTPROCEXITREASON_NORMAL;
                pProcStatus->iStatus = WEXITSTATUS(iStatus);
            }
            else if (WIFSIGNALED(iStatus))
            {
                pProcStatus->enmReason = RTPROCEXITREASON_SIGNAL;
                pProcStatus->iStatus = WTERMSIG(iStatus);
            }
            else
            {
                Assert(!WIFSTOPPED(iStatus));
                pProcStatus->enmReason = RTPROCEXITREASON_ABEND;
                pProcStatus->iStatus = iStatus;
            }
        }
        return VINF_SUCCESS;
    }

    /*
     * Child running?
     */
    if (!rc)
    {
        Assert(fFlags & RTPROCWAIT_FLAGS_NOBLOCK);
        return VERR_PROCESS_RUNNING;
    }

    /*
     * Figure out which error to return.
     */
    int iErr = errno;
    if (iErr == ECHILD)
        return VERR_PROCESS_NOT_FOUND;
    return RTErrConvertFromErrno(iErr);
}


RTR3DECL(int) RTProcTerminate(RTPROCESS Process)
{
    if (Process == NIL_RTPROCESS)
        return VINF_SUCCESS;

    if (!kill(Process, SIGKILL))
        return VINF_SUCCESS;
    return RTErrConvertFromErrno(errno);
}


RTR3DECL(uint64_t) RTProcGetAffinityMask(void)
{
    /// @todo
    return 1;
}


RTR3DECL(int) RTProcQueryParent(RTPROCESS hProcess, PRTPROCESS phParent)
{
    if (hProcess == RTProcSelf())
    {
        *phParent = getppid();
        return VINF_SUCCESS;
    }
    return VERR_NOT_SUPPORTED;
}


RTR3DECL(int) RTProcQueryUsername(RTPROCESS hProcess, char *pszUser, size_t cbUser, size_t *pcbUser)
{
    AssertReturn(   (pszUser && cbUser > 0)
                 || (!pszUser && !cbUser), VERR_INVALID_PARAMETER);
    AssertReturn(pcbUser || pszUser, VERR_INVALID_PARAMETER);

    int rc;
    if (   hProcess == NIL_RTPROCESS
        || hProcess == RTProcSelf())
    {
        /*
         * Figure a good buffer estimate.
         */
        int32_t cbPwdMax = sysconf(_SC_GETPW_R_SIZE_MAX);
        if (cbPwdMax <= _1K)
            cbPwdMax = _1K;
        else
            AssertStmt(cbPwdMax <= 32*_1M, cbPwdMax = 32*_1M);
        char *pchBuf = (char *)RTMemTmpAllocZ(cbPwdMax);
        if (pchBuf)
        {
            /*
             * Get the password file entry.
             */
            struct passwd  Pwd;
            struct passwd *pPwd = NULL;
            rc = getpwuid_r(geteuid(), &Pwd, pchBuf, cbPwdMax, &pPwd);
            if (!rc)
            {
                /*
                 * Convert the name to UTF-8, assuming that we're getting it in the local codeset.
                 */
                /** @todo This isn't exactly optimal... the current codeset/page conversion
                 *        stuff never was.  Should optimize that for UTF-8 and ASCII one day.
                 *        And also optimize for avoiding heap. */
                char *pszTmp = NULL;
                rc = RTStrCurrentCPToUtf8(&pszTmp, pPwd->pw_name);
                if (RT_SUCCESS(rc))
                {
                    size_t cbTmp = strlen(pszTmp) + 1;
                    if (pcbUser)
                        *pcbUser = cbTmp;
                    if (cbTmp <= cbUser)
                    {
                        memcpy(pszUser, pszTmp, cbTmp);
                        rc = VINF_SUCCESS;
                    }
                    else
                        rc = VERR_BUFFER_OVERFLOW;
                    RTStrFree(pszTmp);
                }
            }
            else
                rc = RTErrConvertFromErrno(rc);
            RTMemFree(pchBuf);
        }
        else
            rc = VERR_NO_TMP_MEMORY;
    }
    else
        rc = VERR_NOT_SUPPORTED;
    return rc;
}


RTR3DECL(int) RTProcQueryUsernameA(RTPROCESS hProcess, char **ppszUser)
{
    AssertPtrReturn(ppszUser, VERR_INVALID_POINTER);

    int rc;
    if (   hProcess == NIL_RTPROCESS
        || hProcess == RTProcSelf())
    {
        /*
         * Figure a good buffer estimate.
         */
        int32_t cbPwdMax = sysconf(_SC_GETPW_R_SIZE_MAX);
        if (cbPwdMax <= _1K)
            cbPwdMax = _1K;
        else
            AssertStmt(cbPwdMax <= 32*_1M, cbPwdMax = 32*_1M);
        char *pchBuf = (char *)RTMemTmpAllocZ(cbPwdMax);
        if (pchBuf)
        {
            /*
             * Get the password file entry.
             */
            struct passwd  Pwd;
            struct passwd *pPwd = NULL;
            rc = getpwuid_r(geteuid(), &Pwd, pchBuf, cbPwdMax, &pPwd);
            if (!rc)
            {
                /*
                 * Convert the name to UTF-8, assuming that we're getting it in the local codeset.
                 */
                rc = RTStrCurrentCPToUtf8(ppszUser, pPwd->pw_name);
            }
            else
                rc = RTErrConvertFromErrno(rc);
            RTMemFree(pchBuf);
        }
        else
            rc = VERR_NO_TMP_MEMORY;
    }
    else
        rc = VERR_NOT_SUPPORTED;
    return rc;
}

