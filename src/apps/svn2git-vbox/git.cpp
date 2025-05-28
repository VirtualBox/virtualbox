/* $Id: git.cpp 169030 2025-05-28 11:29:24Z alexander.eichner@oracle.com $ */
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
#include <iprt/err.h>
#include <iprt/file.h>
#include <iprt/env.h>
#include <iprt/path.h>
#include <iprt/pipe.h>
#include <iprt/process.h>

#include "svn2git-internal.h"


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/

#define GIT_BINARY "git"


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

    /* The dump file handle. */
    RTFILE          hFileDump;

    /** The next file mark. */
    uint64_t        idFileMark;
    /** The next commit mark. */
    uint64_t        idCommitMark;

    /** Buffer holding all deleted files for the current transaction. */
    S2GSCRATCHBUF   BufDeletedFiles;
    /** Buffer for files being added/modified. */
    S2GSCRATCHBUF   BufModifiedFiles;
    /** Scratch buffer. */
    S2GSCRATCHBUF   BufScratch;
} S2GREPOSITORYGITINT;
typedef S2GREPOSITORYGITINT *PS2GREPOSITORYGITINT;
typedef const S2GREPOSITORYGITINT *PCS2GREPOSITORYGITINT;


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


static int s2gGitExecWrapperStdOut(const char *pszExec, const char *pszCwd, const char * const *papszArgs,
                                   PS2GSCRATCHBUF pStdOut)
{
    RTPROCESS hProc;
    RTPIPE hPipeStdOutR = NIL_RTPIPE;
    RTPIPE hPipeStdOutW = NIL_RTPIPE;
    int rc = RTPipeCreate(&hPipeStdOutR, &hPipeStdOutW, RTPIPE_C_INHERIT_WRITE);
    if (RT_SUCCESS(rc))
    {
        RTHANDLE HndOut;
        HndOut.enmType = RTHANDLETYPE_PIPE;
        HndOut.u.hPipe = hPipeStdOutW;

        rc = RTProcCreateEx(pszExec, papszArgs, RTENV_DEFAULT, RTPROC_FLAGS_SEARCH_PATH | RTPROC_FLAGS_CWD,
                            NULL, &HndOut, NULL,
                            NULL, NULL, (void *)pszCwd,
                            &hProc);
        RTPipeClose(hPipeStdOutW);
        if (RT_SUCCESS(rc))
        {
            /* Read stdout until we get a broken pipe. */
            for (;;)
            {
                void *pvStdout = s2gScratchBufEnsureSize(pStdOut, _2K);
                if (!pvStdout)
                {
                    rc = VERR_NO_MEMORY;
                    break;
                }

                size_t cbRead = 0;
                rc = RTPipeReadBlocking(hPipeStdOutR, pvStdout, _2K, &cbRead);
                if (RT_FAILURE(rc))
                    break;

                s2gScratchBufAdvance(pStdOut, cbRead);
            }

            if (rc == VERR_BROKEN_PIPE)
                rc = VINF_SUCCESS;
            else if (RT_FAILURE(rc))
                RTProcTerminate(hProc);

            RTPROCSTATUS Sts;
            int rc2 = RTProcWait(hProc, RTPROCWAIT_FLAGS_BLOCK, &Sts);
            if (RT_SUCCESS(rc2))
            {
                if (   Sts.enmReason != RTPROCEXITREASON_NORMAL
                    || Sts.iStatus != 0)
                    rc2 = VERR_INVALID_HANDLE; /** @todo */
            }

            if (RT_SUCCESS(rc))
                rc = rc2;
        }

        RTPipeClose(hPipeStdOutR);
    }

    return rc;
}


DECLINLINE(int) s2gGitWrite(PS2GREPOSITORYGITINT pThis, const void *pvBuf, size_t cbWrite)
{
    int rc = VINF_SUCCESS;

    if (pThis->hPipeWrite != NIL_RTPIPE)
        rc = RTPipeWriteBlocking(pThis->hPipeWrite, pvBuf, cbWrite, NULL /*pcbWritten*/);

    if (pThis->hFileDump != NIL_RTFILE)
    {
        int rc2 = RTFileWrite(pThis->hFileDump, pvBuf, cbWrite, NULL /*pcbWritten*/);
        if (RT_SUCCESS(rc))
            rc = rc2;
    }

    return rc;
}


DECLHIDDEN(int) s2gGitRepositoryCreate(PS2GREPOSITORYGIT phGitRepo, const char *pszGitRepoPath, const char *pszDefaultBranch,
                                       const char *pszDumpFilename, uint32_t *pidRevLast)
{
    RT_NOREF(pszDefaultBranch);

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
    else
    {
        /* Try to gather the svn revision to continue at from the commit log. */
        S2GSCRATCHBUF StdOut;
        s2gScratchBufInit(&StdOut);

        *pidRevLast = 0;

        const char *apszArgs[] = { GIT_BINARY, "log", "HEAD", "-1", NULL };
        rc = s2gGitExecWrapperStdOut(GIT_BINARY, pszGitRepoPath, &apszArgs[0], &StdOut);
        if (RT_SUCCESS(rc))
        {
            char *pb = (char *)s2gScratchBufEnsureSize(&StdOut, 1);
            if (pb)
            {
                *pb = '\0';

                const char *pszRevision = RTStrStr(StdOut.pbBuf, "svn:sync-xref-src-repo-rev: ");
                if (pszRevision)
                {
                    pszRevision += sizeof("svn:sync-xref-src-repo-rev: ") - 1;
                    if (*pszRevision == 'r')
                        *pidRevLast = RTStrToUInt32(pszRevision + 1);
                }
            }
            else
                rc = VERR_NO_MEMORY;
        }
    }

    if (RT_SUCCESS(rc))
    {
        PS2GREPOSITORYGITINT pThis = (PS2GREPOSITORYGITINT)RTMemAllocZ(sizeof(*pThis));
        if (pThis)
        {
            s2gScratchBufInit(&pThis->BufDeletedFiles);
            s2gScratchBufInit(&pThis->BufModifiedFiles);
            s2gScratchBufInit(&pThis->BufScratch);
            pThis->idCommitMark = 1;
            pThis->hFileDump    = NIL_RTFILE;

            if (pszDumpFilename)
                rc = RTFileOpen(&pThis->hFileDump, pszDumpFilename, RTFILE_O_WRITE | RTFILE_O_CREATE_REPLACE | RTFILE_O_DENY_NONE);

            if (RT_SUCCESS(rc))
            {
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
            }

            if (pThis->hFileDump != NIL_RTFILE)
            {
                RTFileClose(pThis->hFileDump);
                RTFileDelete(pszDumpFilename);
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
    s2gGitWrite(pThis, "checkpoint\n", sizeof("checkpoint\n") - 1);
    RTPipeClose(pThis->hPipeWrite);

    RTPROCSTATUS Sts;
    int rc = RTProcWait(pThis->hProcFastImport, RTPROCWAIT_FLAGS_BLOCK, &Sts);
    if (RT_SUCCESS(rc))
    {
        if (   Sts.enmReason != RTPROCEXITREASON_NORMAL
            || Sts.iStatus != 0)
            rc = VERR_INVALID_HANDLE; /** @todo */
    }

    if (pThis->hFileDump != NIL_RTFILE)
        RTFileClose(pThis->hFileDump);

    s2gScratchBufFree(&pThis->BufDeletedFiles);
    s2gScratchBufFree(&pThis->BufModifiedFiles);
    s2gScratchBufFree(&pThis->BufScratch);
    RTMemFree(pThis);
    return rc;
}


DECLHIDDEN(int) s2gGitTransactionStart(S2GREPOSITORYGIT hGitRepo)
{
    PS2GREPOSITORYGITINT pThis = hGitRepo;

    if ((pThis->idCommitMark % 10000) == 0)
    {
        int rc = s2gGitWrite(pThis, "checkpoint\n", sizeof("checkpoint\n") - 1);
        if (RT_FAILURE(rc))
            return rc;

        pThis->idCommitMark = 1;
    }

    pThis->idFileMark = UINT64_MAX - 1;
    s2gScratchBufReset(&pThis->BufDeletedFiles);
    s2gScratchBufReset(&pThis->BufModifiedFiles);
    return VINF_SUCCESS;
}


DECLHIDDEN(int) s2gGitTransactionCommit(S2GREPOSITORYGIT hGitRepo, const char *pszAuthor, const char *pszAuthorEmail,
                                       const char *pszLog, int64_t cEpochSecs)
{
    PS2GREPOSITORYGITINT pThis = hGitRepo;

    s2gScratchBufReset(&pThis->BufScratch);
    size_t cchLog = strlen(pszLog);
    int rc = s2gScratchBufPrintf(&pThis->BufScratch,
                                 "commit refs/heads/%s\n"
                                 "mark :%RU64\n"
                                 "committer %s <%s> %RI64 +0000\n"
                                 "data %zu\n"
                                 "%s\n",
                                 "main" /** @todo Make branch configurable*/,
                                 pThis->idCommitMark++,
                                 pszAuthor, pszAuthorEmail, cEpochSecs,
                                 cchLog, pszLog);
    if (RT_SUCCESS(rc))
    {
        rc = s2gGitWrite(pThis, pThis->BufScratch.pbBuf, pThis->BufScratch.offBuf);
        if (RT_SUCCESS(rc) && pThis->BufDeletedFiles.offBuf)
            rc = s2gGitWrite(pThis, pThis->BufDeletedFiles.pbBuf, pThis->BufDeletedFiles.offBuf);
        if (RT_SUCCESS(rc) && pThis->BufModifiedFiles.offBuf)
            rc = s2gGitWrite(pThis, pThis->BufModifiedFiles.pbBuf, pThis->BufModifiedFiles.offBuf);
    }

    return rc;
}


DECLHIDDEN(int) s2gGitTransactionFileAdd(S2GREPOSITORYGIT hGitRepo, const char *pszPath, bool fIsExec, uint64_t cbFile)
{
    PS2GREPOSITORYGITINT pThis = hGitRepo;
    s2gScratchBufReset(&pThis->BufScratch);
    uint64_t idFileMark = pThis->idFileMark--;
    int rc = s2gScratchBufPrintf(&pThis->BufModifiedFiles, "M %s :%RU64 %s\n",
                                 fIsExec ? "100755" : "100644",
                                 idFileMark, pszPath);
    if (RT_SUCCESS(rc))
    {
        rc = s2gScratchBufPrintf(&pThis->BufScratch,
                                 "blob\n"
                                 "mark :%RU64\n"
                                 "data %RU64\n",
                                 idFileMark, cbFile);
        if (RT_SUCCESS(rc))
            rc = s2gGitWrite(pThis, pThis->BufScratch.pbBuf, pThis->BufScratch.offBuf);
    }

    return rc;
}


DECLHIDDEN(int) s2gGitTransactionFileWriteData(S2GREPOSITORYGIT hGitRepo, const void *pvBuf, size_t cb)
{
    PS2GREPOSITORYGITINT pThis = hGitRepo;

    int rc = VINF_SUCCESS;
    if (cb)
        rc = s2gGitWrite(pThis, pvBuf, cb);

    if (RT_SUCCESS(rc)) /* Need to print an ending line after the file data. */
        rc = s2gGitWrite(pThis, "\n", sizeof("\n") - 1);

    return rc;
}


DECLHIDDEN(int) s2gGitTransactionFileRemove(S2GREPOSITORYGIT hGitRepo, const char *pszPath)
{
    PS2GREPOSITORYGITINT pThis = hGitRepo;
    return s2gScratchBufPrintf(&pThis->BufDeletedFiles, "D %s\n", pszPath);
}


DECLHIDDEN(int) s2gGitTransactionSubmoduleAdd(S2GREPOSITORYGIT hGitRepo, const char *pszPath, const char *pszSha1CommitId)
{
    PS2GREPOSITORYGITINT pThis = hGitRepo;
    return s2gScratchBufPrintf(&pThis->BufModifiedFiles, "M 160000 %s %s\n", pszSha1CommitId, pszPath);
}

