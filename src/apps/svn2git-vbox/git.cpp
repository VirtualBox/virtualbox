/* $Id: git.cpp 168986 2025-05-26 08:33:00Z alexander.eichner@oracle.com $ */
/** @file
 * svn2git - Convert a svn repository to git.
 */

/*
 * Copyright (C) 2025 Oracle and/or its affiliates.
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
#include <iprt/dir.h>
#include <iprt/env.h>
#include <iprt/errcore.h>
#include <iprt/mem.h>
#include <iprt/path.h>
#include <iprt/pipe.h>
#include <iprt/process.h>
#include <iprt/string.h>

#include "svn2git-internal.h"


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/

#define GIT_BINARY "git"


typedef struct S2GFASTIMPORTBUF
{
    /** Pointer to the buffer. */
    char            *pchBuf;
    /** Size of the buffer. */
    size_t          cbBuf;
    /** Offset where to append data next. */
    size_t          offBuf;
} S2GFASTIMPORTBUF;
typedef S2GFASTIMPORTBUF *PS2GFASTIMPORTBUF;


/**
 * Git repository state.
 */
typedef struct S2GREPOSITORYGITINT
{
    /** Process handle to the fast-import process. */
    RTPROCESS       hProcFastImport;
    /** The pipe we write the command stream to. */
    RTPIPE          hPipeWrite;
    /** stderr of git fast-import. */
    RTPIPE          hPipeStderr;
    /** stdout of git fast-import. */
    RTPIPE          hPipeStdout;

    /** The next file mark. */
    uint64_t        idFileMark;
    /** The next commit mark. */
    uint64_t        idCommitMark;

    /** Buffer holding all deleted files for the current transaction. */
    S2GFASTIMPORTBUF BufDeletedFiles;
    /** Buffer for files being added/modified. */
    S2GFASTIMPORTBUF BufModifiedFiles;
    /** Scratch buffer. */
    S2GFASTIMPORTBUF BufScratch;
} S2GREPOSITORYGITINT;
typedef S2GREPOSITORYGITINT *PS2GREPOSITORYGITINT;
typedef const S2GREPOSITORYGITINT *PCS2GREPOSITORYGITINT;


static void s2gGitFiBufInit(PS2GFASTIMPORTBUF pBuf)
{
    pBuf->pchBuf = NULL;
    pBuf->cbBuf  = 0;
    pBuf->offBuf = 0;
}


static void s2gGitFiBufFree(PS2GFASTIMPORTBUF pBuf)
{
    if (pBuf->pchBuf)
        RTMemFree(pBuf->pchBuf);
}


static void s2gGitFiBufReset(PS2GFASTIMPORTBUF pBuf)
{
    pBuf->offBuf = 0;
}


static int s2gGitFiBufPrintf(PS2GFASTIMPORTBUF pBuf, const char *pszFmt, ...) RT_IPRT_FORMAT_ATTR(1, 2)
{
    va_list va;
    va_start(va, pszFmt);
    int rc = VINF_SUCCESS;
    ssize_t cchReq = RTStrPrintf2V(pBuf->pchBuf + pBuf->offBuf, pBuf->cbBuf - pBuf->offBuf,
                                   pszFmt, va);
    if (cchReq < 0)
    {
        size_t cbBufNew = RT_ALIGN_Z((-cchReq) + pBuf->cbBuf, _4K);
        char *pchBufNew = (char *)RTMemRealloc(pBuf->pchBuf, cbBufNew);
        if (pchBufNew)
        {
            pBuf->pchBuf = pchBufNew;
            pBuf->cbBuf  = cbBufNew;
            cchReq = RTStrPrintf2V(pBuf->pchBuf + pBuf->offBuf, pBuf->cbBuf - pBuf->offBuf,
                                   pszFmt, va);
            Assert(cchReq > 0);
        }
        else
            rc = VERR_NO_MEMORY;
    }
    if (RT_SUCCESS(rc))
        pBuf->offBuf += cchReq;

    va_end(va);
    return rc;
}


static int s2gGitExecWrapper(const char *pszExec, const char *pszCwd, const char * const *papszArgs)
{
    RTPROCESS hProc;
    int rc = RTProcCreateEx(pszExec, papszArgs, RTENV_DEFAULT, RTPROC_FLAGS_SEARCH_PATH | RTPROC_FLAGS_CWD,
                            NULL, NULL, NULL, NULL, NULL, (void *)pszCwd, &hProc);
    if (RT_SUCCESS(rc))
    {
        RTPROCSTATUS Sts;
        rc = RTProcWait(hProc, RTPROCWAIT_FLAGS_BLOCK, &Sts);
        if (RT_SUCCESS(rc))
        {
            if (   Sts.enmReason != RTPROCEXITREASON_NORMAL
                || Sts.iStatus != 0)
                rc = VERR_INVALID_HANDLE; /** @todo */
        }
    }

    return rc;
}


DECLHIDDEN(int) s2gGitRepositoryCreate(PS2GREPOSITORYGIT phGitRepo, const char *pszGitRepoPath, const char *pszDefaultBranch)
{
    RT_NOREF(phGitRepo, pszGitRepoPath, pszDefaultBranch);
    int rc = VINF_SUCCESS;
    if (!RTPathExists(pszGitRepoPath))
    {
        rc = RTDirCreate(pszGitRepoPath, 0700, RTDIRCREATE_FLAGS_NO_SYMLINKS);
        if (RT_SUCCESS(rc))
        {
            const char *apszArgs[] = { GIT_BINARY, "--bare", "init", NULL };
            rc = s2gGitExecWrapper(GIT_BINARY, pszGitRepoPath, &apszArgs[0]);
            if (RT_SUCCESS(rc))
            {
                const char *apszArgsCfg[] = { GIT_BINARY, "config", "core.ignorecase", "false", NULL };
                rc = s2gGitExecWrapper(GIT_BINARY, pszGitRepoPath, &apszArgsCfg[0]);
            }
        }
    }

    if (RT_SUCCESS(rc))
    {
        PS2GREPOSITORYGITINT pThis = (PS2GREPOSITORYGITINT)RTMemAllocZ(sizeof(*pThis));
        if (pThis)
        {
            s2gGitFiBufInit(&pThis->BufDeletedFiles);
            s2gGitFiBufInit(&pThis->BufModifiedFiles);
            s2gGitFiBufInit(&pThis->BufScratch);
            pThis->idCommitMark = 1;

            RTPIPE hPipeFiR = NIL_RTPIPE;
            rc = RTPipeCreate(&hPipeFiR, &pThis->hPipeWrite, RTPIPE_C_INHERIT_READ);
            if (RT_SUCCESS(rc))
            {
                RTHANDLE HndIn;
                HndIn.enmType = RTHANDLETYPE_PIPE;
                HndIn.u.hPipe = hPipeFiR;

                const char *apszArgs[] = { GIT_BINARY, "fast-import", NULL };
                rc = RTProcCreateEx(GIT_BINARY, &apszArgs[0], RTENV_DEFAULT, RTPROC_FLAGS_SEARCH_PATH | RTPROC_FLAGS_CWD,
                                    &HndIn, NULL, NULL,
                                    NULL, NULL, (void *)pszGitRepoPath,
                                    &pThis->hProcFastImport);
                RTPipeClose(hPipeFiR);
                if (RT_SUCCESS(rc))
                {
                    *phGitRepo = pThis;
                    return VINF_SUCCESS;
                }
                else
                    RTPipeClose(pThis->hPipeWrite);
            }

            RTMemFree(pThis);
        }
        else
            rc = VERR_NO_MEMORY;
    }

    return rc;
}


DECLHIDDEN(int) s2gGitRepositoryClose(S2GREPOSITORYGIT hGitRepo)
{
    PS2GREPOSITORYGITINT pThis = hGitRepo;
    RTPipeWriteBlocking(pThis->hPipeWrite, "checkpoint\n", sizeof("checkpoint\n") - 1, NULL /*pcbWritten*/);
    RTPipeClose(pThis->hPipeWrite);

    RTPROCSTATUS Sts;
    int rc = RTProcWait(pThis->hProcFastImport, RTPROCWAIT_FLAGS_BLOCK, &Sts);
    if (RT_SUCCESS(rc))
    {
        if (   Sts.enmReason != RTPROCEXITREASON_NORMAL
            || Sts.iStatus != 0)
            rc = VERR_INVALID_HANDLE; /** @todo */
    }

    s2gGitFiBufFree(&pThis->BufDeletedFiles);
    s2gGitFiBufFree(&pThis->BufModifiedFiles);
    s2gGitFiBufFree(&pThis->BufScratch);
    RTMemFree(pThis);
    return rc;
}


DECLHIDDEN(int) s2gGitTransactionStart(S2GREPOSITORYGIT hGitRepo)
{
    PS2GREPOSITORYGITINT pThis = hGitRepo;

    if ((pThis->idCommitMark % 10000) == 0)
    {
        int rc = RTPipeWriteBlocking(pThis->hPipeWrite, "checkpoint\n", sizeof("checkpoint\n") - 1, NULL /*pcbWritten*/);
        if (RT_FAILURE(rc))
            return rc;

        pThis->idCommitMark = 1;
    }

    pThis->idFileMark = UINT64_MAX - 1;
    s2gGitFiBufReset(&pThis->BufDeletedFiles);
    s2gGitFiBufReset(&pThis->BufModifiedFiles);
    return VINF_SUCCESS;
}


DECLHIDDEN(int) s2gGitTransactionCommit(S2GREPOSITORYGIT hGitRepo, const char *pszAuthor, const char *pszAuthorEmail,
                                       const char *pszLog, int64_t cEpochSecs)
{
    PS2GREPOSITORYGITINT pThis = hGitRepo;

    s2gGitFiBufReset(&pThis->BufScratch);
    size_t cchLog = strlen(pszLog);
    int rc = s2gGitFiBufPrintf(&pThis->BufScratch,
                               "commit refs/head/%s\n"
                               "mark :%RU64\n",
                               "committer %s <%s> %RI64 +0000\n"
                               "data %zu\n"
                               "%s\n",
                               "main" /** @todo Make branch configurable*/,
                               pThis->idCommitMark++,
                               pszAuthor, pszAuthorEmail, cEpochSecs,
                               cchLog, pszLog);
    if (RT_SUCCESS(rc))
    {
        rc = RTPipeWriteBlocking(pThis->hPipeWrite, pThis->BufScratch.pchBuf, pThis->BufScratch.offBuf, NULL /*pcbWritten*/);
        if (RT_SUCCESS(rc))
            rc = RTPipeWriteBlocking(pThis->hPipeWrite, pThis->BufDeletedFiles.pchBuf, pThis->BufDeletedFiles.offBuf,
                                     NULL /*pcbWritten*/);
        if (RT_SUCCESS(rc))
            rc = RTPipeWriteBlocking(pThis->hPipeWrite, pThis->BufModifiedFiles.pchBuf, pThis->BufModifiedFiles.offBuf,
                                     NULL /*pcbWritten*/);
    }

    return rc;
}


DECLHIDDEN(int) s2gGitTransactionFileAdd(S2GREPOSITORYGIT hGitRepo, const char *pszPath, bool fIsExec, uint64_t cbFile)
{
    PS2GREPOSITORYGITINT pThis = hGitRepo;
    s2gGitFiBufReset(&pThis->BufScratch);
    uint64_t idFileMark = pThis->idFileMark--;
    int rc = s2gGitFiBufPrintf(&pThis->BufModifiedFiles, "M %s :%RU64 %s\n",
                               fIsExec ? "100755" : "100644",
                               idFileMark, pszPath);
    if (RT_SUCCESS(rc))
    {
        rc = s2gGitFiBufPrintf(&pThis->BufScratch,
                               "blob\n"
                               "mark :%RU64\n"
                               "data %RU64\n",
                               idFileMark, cbFile);
        if (RT_SUCCESS(rc))
            RTPipeWriteBlocking(pThis->hPipeWrite, pThis->BufScratch.pchBuf, pThis->BufScratch.offBuf, NULL /*pcbWritten*/);
    }

    return rc;
}


DECLHIDDEN(int) s2gGitTransactionFileWriteData(S2GREPOSITORYGIT hGitRepo, const void *pvBuf, size_t cb)
{
    PS2GREPOSITORYGITINT pThis = hGitRepo;

    return RTPipeWriteBlocking(pThis->hPipeWrite, pvBuf, cb, NULL /*pcbWritten*/);
}


DECLHIDDEN(int) s2gGitTransactionFileRemove(S2GREPOSITORYGIT hGitRepo, const char *pszPath)
{
    PS2GREPOSITORYGITINT pThis = hGitRepo;
    return s2gGitFiBufPrintf(&pThis->BufDeletedFiles, "D %s\n", pszPath);
}


DECLHIDDEN(int) s2gGitTransactionSubmoduleAdd(S2GREPOSITORYGIT hGitRepo, const char *pszPath, const char *pszSha1CommitId)
{
    PS2GREPOSITORYGITINT pThis = hGitRepo;
    return s2gGitFiBufPrintf(&pThis->BufModifiedFiles, "M 160000 %s %s\n", pszSha1CommitId, pszPath);
}

