/* $Id: scmsubversion.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT Testcase / Tool - Source Code Massager, Subversion Access.
 */

/*
 * Copyright (C) 2010-2024 Oracle and/or its affiliates.
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

#define SCM_WITH_DYNAMIC_LIB_SVN


/*********************************************************************************************************************************
*   Header Files                                                                                                                 *
*********************************************************************************************************************************/
#include <iprt/assert.h>
#include <iprt/ctype.h>
#include <iprt/dir.h>
#include <iprt/env.h>
#include <iprt/err.h>
#include <iprt/file.h>
#include <iprt/getopt.h>
#include <iprt/handle.h>
#include <iprt/initterm.h>
#include <iprt/ldr.h>
#include <iprt/mem.h>
#include <iprt/message.h>
#include <iprt/param.h>
#include <iprt/path.h>
#include <iprt/pipe.h>
#include <iprt/poll.h>
#include <iprt/process.h>
#include <iprt/stream.h>
#include <iprt/string.h>

#include "scm.h"

#if defined(SCM_WITH_DYNAMIC_LIB_SVN) && defined(SCM_WITH_SVN_HEADERS)
# include <svn_client.h>
#endif


/*********************************************************************************************************************************
*   Defined Constants And Macros                                                                                                 *
*********************************************************************************************************************************/
#ifdef SCM_WITH_DYNAMIC_LIB_SVN
# if defined(RT_OS_WINDOWS) && defined(RT_ARCH_X86)
#  define APR_CALL                       __stdcall
#  define SVN_CALL                       /* __stdcall ?? */
# else
#  define APR_CALL
#  define SVN_CALL
# endif
#endif
#if defined(SCM_WITH_DYNAMIC_LIB_SVN) && !defined(SCM_WITH_SVN_HEADERS)
# define SVN_ERR_MISC_CATEGORY_START    200000
# define SVN_ERR_UNVERSIONED_RESOURCE   (SVN_ERR_MISC_CATEGORY_START + 5)
#endif


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/
#if defined(SCM_WITH_DYNAMIC_LIB_SVN) && !defined(SCM_WITH_SVN_HEADERS)
typedef int                         apr_status_t;
typedef int64_t                     apr_time_t;
typedef struct apr_pool_t           apr_pool_t;
typedef struct apr_hash_t           apr_hash_t;
typedef struct apr_hash_index_t     apr_hash_index_t;
typedef struct apr_array_header_t   apr_array_header_t;


typedef struct svn_error_t
{
    apr_status_t                    apr_err;
    const char                     *_dbgr_message;
    struct svn_error_t             *_dbgr_child;
    apr_pool_t                     *_dbgr_pool;
    const char                     *_dbgr_file;
    long                            _dbgr_line;
} svn_error_t;
typedef int                         svn_boolean_t;
typedef long int                    svn_revnum_t;
typedef struct svn_client_ctx_t     svn_client_ctx_t;
typedef enum svn_opt_revision_kind
{
    svn_opt_revision_unspecified = 0,
    svn_opt_revision_number,
    svn_opt_revision_date,
    svn_opt_revision_committed,
    svn_opt_revision_previous,
    svn_opt_revision_base,
    svn_opt_revision_working,
    svn_opt_revision_head
} svn_opt_revision_kind;
typedef union svn_opt_revision_value_t
{
    svn_revnum_t                    number;
    apr_time_t                      date;
} svn_opt_revision_value_t;
typedef struct svn_opt_revision_t
{
  svn_opt_revision_kind             kind;
  svn_opt_revision_value_t          value;
} svn_opt_revision_t;
typedef enum svn_depth_t
{
    svn_depth_unknown = -2,
    svn_depth_exclude,
    svn_depth_empty,
    svn_depth_files,
    svn_depth_immediates,
    svn_depth_infinity
} svn_depth_t;

#endif /* SCM_WITH_DYNAMIC_LIB_SVN && !SCM_WITH_SVN_HEADERS */


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
static char g_szSvnPath[RTPATH_MAX];
static enum
{
    kScmSvnVersion_Ancient = 1,
    kScmSvnVersion_1_6,
    kScmSvnVersion_1_7,
    kScmSvnVersion_1_8,
    kScmSvnVersion_End
}           g_enmSvnVersion = kScmSvnVersion_Ancient;


#ifdef SCM_WITH_DYNAMIC_LIB_SVN
/** Set if all the function pointers are valid. */
static bool                             g_fSvnFunctionPointersValid;
/** @name SVN and APR imports.
 * @{ */
static apr_status_t          (APR_CALL *g_pfnAprInitialize)(void);
static apr_hash_index_t *    (APR_CALL *g_pfnAprHashFirst)(apr_pool_t *pPool, apr_hash_t *pHashTab);
static apr_hash_index_t *    (APR_CALL *g_pfnAprHashNext)(apr_hash_index_t *pCurIdx);
static void *                (APR_CALL *g_pfnAprHashThisVal)(apr_hash_index_t *pHashIdx);
static apr_pool_t *          (SVN_CALL *g_pfnSvnPoolCreateEx)(apr_pool_t *pParent, void *pvAllocator);
static void                  (APR_CALL *g_pfnAprPoolClear)(apr_pool_t *pPool);
static void                  (APR_CALL *g_pfnAprPoolDestroy)(apr_pool_t *pPool);

static svn_error_t *         (SVN_CALL *g_pfnSvnClientCreateContext)(svn_client_ctx_t **ppCtx, apr_pool_t *pPool);
static svn_error_t *         (SVN_CALL *g_pfnSvnClientPropGet4)(apr_hash_t **ppHashProps, const char *pszPropName,
                                                                const char *pszTarget, const svn_opt_revision_t *pPeggedRev,
                                                                const svn_opt_revision_t *pRevision, svn_revnum_t *pActualRev,
                                                                svn_depth_t enmDepth, const apr_array_header_t *pChangeList,
                                                                svn_client_ctx_t *pCtx, apr_pool_t *pResultPool,
                                                                apr_pool_t *pScratchPool);
/**@} */

/** Cached APR pool. */
static apr_pool_t          *g_pSvnPool = NULL;
/** Cached SVN client context. */
static svn_client_ctx_t    *g_pSvnClientCtx = NULL;
/** Number of times the current context has been used. */
static uint32_t             g_cSvnClientCtxUsed = 0;

#endif


/*********************************************************************************************************************************
*   Internal Functions                                                                                                           *
*********************************************************************************************************************************/
#ifdef SCM_WITH_DYNAMIC_LIB_SVN
static void scmSvnFlushClientContextAndPool(void);
#endif



/**
 * Callback that is call for each path to search.
 */
static DECLCALLBACK(int) scmSvnFindSvnBinaryCallback(char const *pchPath, size_t cchPath, void *pvUser1, void *pvUser2)
{
    char   *pszDst = (char *)pvUser1;
    size_t  cchDst = (size_t)pvUser2;
    if (cchDst > cchPath)
    {
        memcpy(pszDst, pchPath, cchPath);
        pszDst[cchPath] = '\0';
#if defined(RT_OS_WINDOWS) || defined(RT_OS_OS2)
        int rc = RTPathAppend(pszDst, cchDst, "svn.exe");
#else
        int rc = RTPathAppend(pszDst, cchDst, "svn");
#endif
        if (   RT_SUCCESS(rc)
            && RTFileExists(pszDst))
            return VINF_SUCCESS;
    }
    return VERR_TRY_AGAIN;
}


/**
 * Reads from a pipe.
 *
 * @returns @a rc or other status code.
 * @param   rc              The current status of the operation.  Error status
 *                          are preserved and returned.
 * @param   phPipeR         Pointer to the pipe handle.
 * @param   pcbAllocated    Pointer to the buffer size variable.
 * @param   poffCur         Pointer to the buffer offset variable.
 * @param   ppszBuffer      Pointer to the buffer pointer variable.
 */
static int rtProcProcessOutput(int rc, PRTPIPE phPipeR, size_t *pcbAllocated, size_t *poffCur, char **ppszBuffer,
                               RTPOLLSET hPollSet, uint32_t idPollSet)
{
    size_t  cbRead;
    char    szTmp[_4K - 1];
    for (;;)
    {
        int rc2 = RTPipeRead(*phPipeR, szTmp, sizeof(szTmp), &cbRead);
        if (RT_SUCCESS(rc2) && cbRead)
        {
            /* Resize the buffer. */
            if (*poffCur + cbRead >= *pcbAllocated)
            {
                if (*pcbAllocated >= _1G)
                {
                    RTPollSetRemove(hPollSet, idPollSet);
                    rc2 = RTPipeClose(*phPipeR); AssertRC(rc2);
                    *phPipeR = NIL_RTPIPE;
                    return RT_SUCCESS(rc) ? VERR_TOO_MUCH_DATA : rc;
                }

                size_t cbNew = *pcbAllocated ? *pcbAllocated * 2 : sizeof(szTmp) + 1;
                Assert(*poffCur + cbRead < cbNew);
                rc2 = RTStrRealloc(ppszBuffer, cbNew);
                if (RT_FAILURE(rc2))
                {
                    RTPollSetRemove(hPollSet, idPollSet);
                    rc2 = RTPipeClose(*phPipeR); AssertRC(rc2);
                    *phPipeR = NIL_RTPIPE;
                    return RT_SUCCESS(rc) ? rc2 : rc;
                }
                *pcbAllocated = cbNew;
            }

            /* Append the new data, terminating it. */
            memcpy(*ppszBuffer + *poffCur, szTmp, cbRead);
            *poffCur += cbRead;
            (*ppszBuffer)[*poffCur] = '\0';

            /* Check for null terminators in the string. */
            if (RT_SUCCESS(rc) && memchr(szTmp, '\0', cbRead))
                rc = VERR_NO_TRANSLATION;

            /* If we read a full buffer, try read some more. */
            if (RT_SUCCESS(rc) && cbRead == sizeof(szTmp))
                continue;
        }
        else if (rc2 != VINF_TRY_AGAIN)
        {
            if (RT_FAILURE(rc) && rc2 != VERR_BROKEN_PIPE)
                rc = rc2;
            RTPollSetRemove(hPollSet, idPollSet);
            rc2 = RTPipeClose(*phPipeR); AssertRC(rc2);
            *phPipeR = NIL_RTPIPE;
        }
        return rc;
    }
}

/** @name RTPROCEXEC_FLAGS_XXX - flags for RTProcExec and RTProcExecToString.
 * @{ */
/** Redirect /dev/null to standard input. */
#define RTPROCEXEC_FLAGS_STDIN_NULL             RT_BIT_32(0)
/** Redirect standard output to /dev/null. */
#define RTPROCEXEC_FLAGS_STDOUT_NULL            RT_BIT_32(1)
/** Redirect standard error to /dev/null. */
#define RTPROCEXEC_FLAGS_STDERR_NULL            RT_BIT_32(2)
/** Redirect all standard output to /dev/null as well as directing /dev/null
 * to standard input. */
#define RTPROCEXEC_FLAGS_STD_NULL               (  RTPROCEXEC_FLAGS_STDIN_NULL \
                                                 | RTPROCEXEC_FLAGS_STDOUT_NULL \
                                                 | RTPROCEXEC_FLAGS_STDERR_NULL)
/** Mask containing the valid flags. */
#define RTPROCEXEC_FLAGS_VALID_MASK             UINT32_C(0x00000007)
/** @} */

/**
 * Runs a process, collecting the standard output and/or standard error.
 *
 *
 * @returns IPRT status code
 * @retval  VERR_NO_TRANSLATION if the output of the program isn't valid UTF-8
 *          or contains a nul character.
 * @retval  VERR_TOO_MUCH_DATA if the process produced too much data.
 *
 * @param   pszExec     Executable image to use to create the child process.
 * @param   papszArgs   Pointer to an array of arguments to the child.  The
 *                      array terminated by an entry containing NULL.
 * @param   hEnv        Handle to the environment block for the child.
 * @param   fFlags      A combination of RTPROCEXEC_FLAGS_XXX.  The @a
 *                      ppszStdOut and @a ppszStdErr parameters takes precedence
 *                      over redirection flags.
 * @param   pStatus     Where to return the status on success.
 * @param   ppszStdOut  Where to return the text written to standard output. If
 *                      NULL then standard output will not be collected and go
 *                      to the standard output handle of the process.
 *                      Free with RTStrFree, regardless of return status.
 * @param   ppszStdErr  Where to return the text written to standard error. If
 *                      NULL then standard output will not be collected and go
 *                      to the standard error handle of the process.
 *                      Free with RTStrFree, regardless of return status.
 */
static int RTProcExecToString(const char *pszExec, const char * const *papszArgs, RTENV hEnv, uint32_t fFlags,
                              PRTPROCSTATUS pStatus, char **ppszStdOut, char **ppszStdErr)
{
    int rc2;

    /*
     * Clear output arguments (no returning failure here, simply crash!).
     */
    AssertPtr(pStatus);
    pStatus->enmReason = RTPROCEXITREASON_ABEND;
    pStatus->iStatus   = RTEXITCODE_FAILURE;
    AssertPtrNull(ppszStdOut);
    if (ppszStdOut)
        *ppszStdOut = NULL;
    AssertPtrNull(ppszStdOut);
    if (ppszStdErr)
        *ppszStdErr = NULL;

    /*
     * Check input arguments.
     */
    AssertReturn(!(fFlags & ~RTPROCEXEC_FLAGS_VALID_MASK), VERR_INVALID_PARAMETER);

    /*
     * Do we need a standard input bitbucket?
     */
    int         rc = VINF_SUCCESS;
    PRTHANDLE   phChildStdIn = NULL;
    RTHANDLE    hChildStdIn;
    hChildStdIn.enmType = RTHANDLETYPE_FILE;
    hChildStdIn.u.hFile = NIL_RTFILE;
    if ((fFlags & RTPROCEXEC_FLAGS_STDIN_NULL) && RT_SUCCESS(rc))
    {
        phChildStdIn = &hChildStdIn;
        rc = RTFileOpenBitBucket(&hChildStdIn.u.hFile, RTFILE_O_READ);
    }

    /*
     * Create the output pipes / bitbuckets.
     */
    RTPIPE      hPipeStdOutR  = NIL_RTPIPE;
    PRTHANDLE   phChildStdOut = NULL;
    RTHANDLE    hChildStdOut;
    hChildStdOut.enmType = RTHANDLETYPE_PIPE;
    hChildStdOut.u.hPipe = NIL_RTPIPE;
    if (ppszStdOut && RT_SUCCESS(rc))
    {
        phChildStdOut = &hChildStdOut;
        rc = RTPipeCreate(&hPipeStdOutR, &hChildStdOut.u.hPipe, 0 /*fFlags*/);
    }
    else if ((fFlags & RTPROCEXEC_FLAGS_STDOUT_NULL) && RT_SUCCESS(rc))
    {
        phChildStdOut = &hChildStdOut;
        hChildStdOut.enmType = RTHANDLETYPE_FILE;
        hChildStdOut.u.hFile = NIL_RTFILE;
        rc = RTFileOpenBitBucket(&hChildStdOut.u.hFile, RTFILE_O_WRITE);
    }

    RTPIPE      hPipeStdErrR  = NIL_RTPIPE;
    PRTHANDLE   phChildStdErr = NULL;
    RTHANDLE    hChildStdErr;
    hChildStdErr.enmType = RTHANDLETYPE_PIPE;
    hChildStdErr.u.hPipe = NIL_RTPIPE;
    if (ppszStdErr && RT_SUCCESS(rc))
    {
        phChildStdErr = &hChildStdErr;
        rc = RTPipeCreate(&hPipeStdErrR, &hChildStdErr.u.hPipe, 0 /*fFlags*/);
    }
    else if ((fFlags & RTPROCEXEC_FLAGS_STDERR_NULL) && RT_SUCCESS(rc))
    {
        phChildStdErr = &hChildStdErr;
        hChildStdErr.enmType = RTHANDLETYPE_FILE;
        hChildStdErr.u.hFile = NIL_RTFILE;
        rc = RTFileOpenBitBucket(&hChildStdErr.u.hFile, RTFILE_O_WRITE);
    }

    if (RT_SUCCESS(rc))
    {
        RTPOLLSET hPollSet;
        rc = RTPollSetCreate(&hPollSet);
        if (RT_SUCCESS(rc))
        {
            if (hPipeStdOutR != NIL_RTPIPE && RT_SUCCESS(rc))
                rc = RTPollSetAddPipe(hPollSet, hPipeStdOutR, RTPOLL_EVT_READ | RTPOLL_EVT_ERROR, 1);
            if (hPipeStdErrR != NIL_RTPIPE)
                rc = RTPollSetAddPipe(hPollSet, hPipeStdErrR, RTPOLL_EVT_READ | RTPOLL_EVT_ERROR, 2);
        }
        if (RT_SUCCESS(rc))
        {
            /*
             * Create the process.
             */
            RTPROCESS hProc;
            rc = RTProcCreateEx(pszExec,
                                papszArgs,
                                hEnv,
                                0 /*fFlags*/,
                                NULL /*phStdIn*/,
                                phChildStdOut,
                                phChildStdErr,
                                NULL /*pszAsUser*/,
                                NULL /*pszPassword*/,
                                NULL /*pvExtraData*/,
                                &hProc);
            rc2 = RTHandleClose(&hChildStdErr); AssertRC(rc2);
            rc2 = RTHandleClose(&hChildStdOut); AssertRC(rc2);

            if (RT_SUCCESS(rc))
            {
                /*
                 * Process output and wait for the process to finish.
                 */
                size_t cbStdOut  = 0;
                size_t offStdOut = 0;
                size_t cbStdErr  = 0;
                size_t offStdErr = 0;
                for (;;)
                {
                    if (hPipeStdOutR != NIL_RTPIPE)
                        rc = rtProcProcessOutput(rc, &hPipeStdOutR, &cbStdOut, &offStdOut, ppszStdOut, hPollSet, 1);
                    if (hPipeStdErrR != NIL_RTPIPE)
                        rc = rtProcProcessOutput(rc, &hPipeStdErrR, &cbStdErr, &offStdErr, ppszStdErr, hPollSet, 2);
                    if (hPipeStdOutR == NIL_RTPIPE && hPipeStdErrR == NIL_RTPIPE)
                        break;

                    if (hProc != NIL_RTPROCESS)
                    {
                        rc2 = RTProcWait(hProc, RTPROCWAIT_FLAGS_NOBLOCK, pStatus);
                        if (rc2 != VERR_PROCESS_RUNNING)
                        {
                            if (RT_FAILURE(rc2))
                                rc = rc2;
                            hProc = NIL_RTPROCESS;
                        }
                    }

                    rc2 = RTPoll(hPollSet, 10000, NULL, NULL);
                    Assert(RT_SUCCESS(rc2) || rc2 == VERR_TIMEOUT);
                }

                if (RT_SUCCESS(rc))
                {
                    if (   (ppszStdOut && *ppszStdOut && !RTStrIsValidEncoding(*ppszStdOut))
                        || (ppszStdErr && *ppszStdErr && !RTStrIsValidEncoding(*ppszStdErr)) )
                        rc = VERR_NO_TRANSLATION;
                }

                /*
                 * No more output, just wait for it to finish.
                 */
                if (hProc != NIL_RTPROCESS)
                {
                    rc2 = RTProcWait(hProc, RTPROCWAIT_FLAGS_BLOCK, pStatus);
                    if (RT_FAILURE(rc2))
                        rc = rc2;
                }
            }
            RTPollSetDestroy(hPollSet);
        }
    }

    rc2 = RTHandleClose(&hChildStdErr); AssertRC(rc2);
    rc2 = RTHandleClose(&hChildStdOut); AssertRC(rc2);
    rc2 = RTHandleClose(&hChildStdIn);  AssertRC(rc2);
    rc2 = RTPipeClose(hPipeStdErrR);    AssertRC(rc2);
    rc2 = RTPipeClose(hPipeStdOutR);    AssertRC(rc2);
    return rc;
}


/**
 * Runs a process, waiting for it to complete.
 *
 * @returns IPRT status code
 *
 * @param   pszExec     Executable image to use to create the child process.
 * @param   papszArgs   Pointer to an array of arguments to the child.  The
 *                      array terminated by an entry containing NULL.
 * @param   hEnv        Handle to the environment block for the child.
 * @param   fFlags      A combination of RTPROCEXEC_FLAGS_XXX.
 * @param   pStatus     Where to return the status on success.
 */
static int RTProcExec(const char *pszExec, const char * const *papszArgs, RTENV hEnv, uint32_t fFlags,
                      PRTPROCSTATUS pStatus)
{
    int rc;

    /*
     * Clear output argument (no returning failure here, simply crash!).
     */
    AssertPtr(pStatus);
    pStatus->enmReason = RTPROCEXITREASON_ABEND;
    pStatus->iStatus   = RTEXITCODE_FAILURE;

    /*
     * Check input arguments.
     */
    AssertReturn(!(fFlags & ~RTPROCEXEC_FLAGS_VALID_MASK), VERR_INVALID_PARAMETER);

    /*
     * Set up /dev/null redirections.
     */
    PRTHANDLE   aph[3] = { NULL, NULL, NULL };
    RTHANDLE    ah[3];
    for (uint32_t i = 0; i < 3; i++)
    {
        ah[i].enmType = RTHANDLETYPE_FILE;
        ah[i].u.hFile = NIL_RTFILE;
    }
    rc = VINF_SUCCESS;
    if ((fFlags & RTPROCEXEC_FLAGS_STDIN_NULL) && RT_SUCCESS(rc))
    {
        aph[0] = &ah[0];
        rc = RTFileOpenBitBucket(&ah[0].u.hFile, RTFILE_O_READ);
    }
    if ((fFlags & RTPROCEXEC_FLAGS_STDOUT_NULL) && RT_SUCCESS(rc))
    {
        aph[1] = &ah[1];
        rc = RTFileOpenBitBucket(&ah[1].u.hFile, RTFILE_O_WRITE);
    }
    if ((fFlags & RTPROCEXEC_FLAGS_STDERR_NULL) && RT_SUCCESS(rc))
    {
        aph[2] = &ah[2];
        rc = RTFileOpenBitBucket(&ah[2].u.hFile, RTFILE_O_WRITE);
    }

    /*
     * Create the process.
     */
    RTPROCESS hProc = NIL_RTPROCESS;
    if (RT_SUCCESS(rc))
        rc = RTProcCreateEx(pszExec,
                            papszArgs,
                            hEnv,
                            0 /*fFlags*/,
                            aph[0],
                            aph[1],
                            aph[2],
                            NULL /*pszAsUser*/,
                            NULL /*pszPassword*/,
                            NULL /*pvExtraData*/,
                            &hProc);

    for (uint32_t i = 0; i < 3; i++)
        RTFileClose(ah[i].u.hFile);

    if (RT_SUCCESS(rc))
        rc = RTProcWait(hProc, RTPROCWAIT_FLAGS_BLOCK, pStatus);
    return rc;
}



/**
 * Executes SVN and gets the output.
 *
 * Standard error is suppressed.
 *
 * @returns VINF_SUCCESS if the command executed successfully.
 * @param   pState              The rewrite state to work on.  Can be NULL.
 * @param   papszArgs           The SVN argument.
 * @param   fNormalFailureOk    Whether normal failure is ok.
 * @param   ppszStdOut          Where to return the output on success.
 */
static int scmSvnRunAndGetOutput(PSCMRWSTATE pState, const char **papszArgs, bool fNormalFailureOk, char **ppszStdOut)
{
    *ppszStdOut = NULL;

#ifdef SCM_WITH_DYNAMIC_LIB_SVN
    scmSvnFlushClientContextAndPool();
#endif

    char *pszCmdLine = NULL;
    int rc = RTGetOptArgvToString(&pszCmdLine, papszArgs, RTGETOPTARGV_CNV_QUOTE_BOURNE_SH);
    if (RT_FAILURE(rc))
        return rc;
    ScmVerbose(pState, 2, "executing: %s\n", pszCmdLine);

    RTPROCSTATUS Status;
    rc = RTProcExecToString(g_szSvnPath, papszArgs, RTENV_DEFAULT,
                            RTPROCEXEC_FLAGS_STD_NULL, &Status, ppszStdOut, NULL);

    if (    RT_SUCCESS(rc)
        &&  (   Status.enmReason != RTPROCEXITREASON_NORMAL
             || Status.iStatus != 0) )
    {
        if (fNormalFailureOk || Status.enmReason != RTPROCEXITREASON_NORMAL)
            RTMsgError("%s: %s -> %s %u\n",
                       pState ? pState->pszFilename : "<NONE>", pszCmdLine,
                       Status.enmReason == RTPROCEXITREASON_NORMAL   ? "exit code"
                       : Status.enmReason == RTPROCEXITREASON_SIGNAL ? "signal"
                       : Status.enmReason == RTPROCEXITREASON_ABEND  ? "abnormal end"
                       : "abducted by alien",
                       Status.iStatus);
        rc = VERR_GENERAL_FAILURE;
    }
    else if (RT_FAILURE(rc))
    {
        if (pState)
            RTMsgError("%s: executing: %s => %Rrc\n", pState->pszFilename, pszCmdLine, rc);
        else
            RTMsgError("executing: %s => %Rrc\n", pszCmdLine, rc);
    }

    if (RT_FAILURE(rc))
    {
        RTStrFree(*ppszStdOut);
        *ppszStdOut = NULL;
    }
    RTStrFree(pszCmdLine);
    return rc;
}


/**
 * Executes SVN.
 *
 * Standard error and standard output is suppressed.
 *
 * @returns VINF_SUCCESS if the command executed successfully.
 * @param   pState              The rewrite state to work on.
 * @param   papszArgs           The SVN argument.
 * @param   fNormalFailureOk    Whether normal failure is ok.
 */
static int scmSvnRun(PSCMRWSTATE pState, const char **papszArgs, bool fNormalFailureOk)
{
#ifdef SCM_WITH_DYNAMIC_LIB_SVN
    scmSvnFlushClientContextAndPool();
#endif

    char *pszCmdLine = NULL;
    int rc = RTGetOptArgvToString(&pszCmdLine, papszArgs, RTGETOPTARGV_CNV_QUOTE_BOURNE_SH);
    if (RT_FAILURE(rc))
        return rc;
    ScmVerbose(pState, 2, "executing: %s\n", pszCmdLine);

    /* Lazy bird uses RTProcExec. */
    RTPROCSTATUS Status;
    rc = RTProcExec(g_szSvnPath, papszArgs, RTENV_DEFAULT, RTPROCEXEC_FLAGS_STD_NULL, &Status);

    if (    RT_SUCCESS(rc)
        &&  (   Status.enmReason != RTPROCEXITREASON_NORMAL
             || Status.iStatus != 0) )
    {
        if (fNormalFailureOk || Status.enmReason != RTPROCEXITREASON_NORMAL)
            RTMsgError("%s: %s -> %s %u\n",
                       pState->pszFilename,
                       pszCmdLine,
                       Status.enmReason == RTPROCEXITREASON_NORMAL   ? "exit code"
                       : Status.enmReason == RTPROCEXITREASON_SIGNAL ? "signal"
                       : Status.enmReason == RTPROCEXITREASON_ABEND  ? "abnormal end"
                       : "abducted by alien",
                       Status.iStatus);
        rc = VERR_GENERAL_FAILURE;
    }
    else if (RT_FAILURE(rc))
        RTMsgError("%s: %s -> %Rrc\n", pState->pszFilename, pszCmdLine, rc);

    RTStrFree(pszCmdLine);
    return rc;
}


#ifdef SCM_WITH_DYNAMIC_LIB_SVN
/**
 * Attempts to resolve the necessary subversion and apache portable runtime APIs
 * we require dynamically.
 *
 * Will set all global function pointers and g_fSvnFunctionPointersValid to true
 * on success.
 */
static void scmSvnTryResolveFunctions(void)
{
    char szPath[RTPATH_MAX];
    int rc = RTStrCopy(szPath, sizeof(szPath), g_szSvnPath);
    if (RT_SUCCESS(rc))
    {
        RTPathStripFilename(szPath);
        char *pszEndPath = strchr(szPath, '\0');
# ifdef RT_OS_WINDOWS
        RTPathChangeToDosSlashes(szPath, false);
# endif

        /*
         * Try various prefixes/suffxies/locations.
         */
        static struct
        {
            const char *pszPrefix;
            const char *pszSuffix;
        } const s_aVariations[] =
        {
# ifdef RT_OS_WINDOWS
            { "SlikSvn-lib", "-1.dll" },    /* SlikSVN */
            { "lib", "-1.dll" },            /* Win32Svn,CollabNet,++ */
# elif defined(RT_OS_DARWIN)
            { "../lib/lib", "-1.dylib" },
# else
            { "../lib/lib", ".so" },
            { "../lib/lib", "-1.so" },
#  if ARCH_BITS == 32
            { "../lib32/lib", ".so" },
            { "../lib32/lib", "-1.so" },
#  else
            { "../lib64/lib", ".so" },
            { "../lib64/lib", "-1.so" },
#   ifdef RT_OS_SOLARIS
            { "../lib/svn/amd64/lib", ".so" },
            { "../lib/svn/amd64/lib", "-1.so" },
            { "../apr/1.6/lib/amd64/lib", ".so" },
            { "../apr/1.6/lib/amd64/lib", "-1.so" },
#   endif
#  endif
#  ifdef RT_ARCH_X86
            { "../lib/i386-linux-gnu/lib", ".so" },
            { "../lib/i386-linux-gnu/lib", "-1.so" },
#  elif defined(RT_ARCH_AMD64)
            { "../lib/x86_64-linux-gnu/lib", ".so" },
            { "../lib/x86_64-linux-gnu/lib", "-1.so" },
#  endif
# endif
        };
        for (unsigned iVar = 0; iVar < RT_ELEMENTS(s_aVariations); iVar++)
        {
            /*
             * Try load the svn_client library ...
             */
            static const char * const s_apszLibraries[]   = { "svn_client", "svn_subr",   "apr" };
            RTLDRMOD ahMods[RT_ELEMENTS(s_apszLibraries)] = { NIL_RTLDRMOD, NIL_RTLDRMOD, NIL_RTLDRMOD };

            rc = VINF_SUCCESS;
            unsigned iLib;
            for (iLib = 0; iLib < RT_ELEMENTS(s_apszLibraries) && RT_SUCCESS(rc); iLib++)
            {
                static const char * const s_apszSuffixes[] = { "", ".0", ".1" };
                for (unsigned iSuff = 0; iSuff < RT_ELEMENTS(s_apszSuffixes); iSuff++)
                {
                    *pszEndPath = '\0';
                    rc = RTPathAppend(szPath, sizeof(szPath), s_aVariations[iVar].pszPrefix);
                    if (RT_SUCCESS(rc))
                        rc = RTStrCat(szPath, sizeof(szPath), s_apszLibraries[iLib]);
                    if (RT_SUCCESS(rc))
                        rc = RTStrCat(szPath, sizeof(szPath), s_aVariations[iVar].pszSuffix);
                    if (RT_SUCCESS(rc))
                        rc = RTStrCat(szPath, sizeof(szPath), s_apszSuffixes[iSuff]);
                    if (RT_SUCCESS(rc))
                    {
# ifdef RT_OS_WINDOWS
                        RTPathChangeToDosSlashes(pszEndPath, false);
# endif
                        rc = RTLdrLoadEx(szPath, &ahMods[iLib], RTLDRLOAD_FLAGS_NT_SEARCH_DLL_LOAD_DIR , NULL);
                        if (RT_SUCCESS(rc))
                        {
                            RTMEM_WILL_LEAK(ahMods[iLib]);
                            break;
                        }
                    }
                }
# ifdef RT_OS_SOLARIS
                /*
                 * HACK: Solaris may keep libapr.so separately from svn, so do a separate search for it.
                 */
                /** @todo It would make a lot more sense to use the dlfcn.h machinery to figure
                 *        out which libapr*.so* file was loaded into the process together with
                 *        the two svn libraries and get a dlopen handle for it.  We risk ending
                 *        up with the completely wrong libapr here! */
                if (iLib == RT_ELEMENTS(s_apszLibraries) - 1 && RT_FAILURE(rc))
                {
                    ahMods[iLib] = NIL_RTLDRMOD;
                    for (unsigned iVar2 = 0; iVar2 < RT_ELEMENTS(s_aVariations) && ahMods[iLib] == NIL_RTLDRMOD; iVar2++)
                        for (unsigned iSuff2 = 0; iSuff2 < RT_ELEMENTS(s_apszSuffixes) && ahMods[iLib] == NIL_RTLDRMOD; iSuff2++)
                        {
                            *pszEndPath = '\0';
                            rc = RTPathAppend(szPath, sizeof(szPath), s_aVariations[iVar2].pszPrefix);
                            if (RT_SUCCESS(rc))
                                rc = RTStrCat(szPath, sizeof(szPath), s_apszLibraries[iLib]);
                            if (RT_SUCCESS(rc))
                                rc = RTStrCat(szPath, sizeof(szPath), s_aVariations[iVar2].pszSuffix);
                            if (RT_SUCCESS(rc))
                                rc = RTStrCat(szPath, sizeof(szPath), s_apszSuffixes[iSuff2]);
                            if (RT_SUCCESS(rc))
                                rc = RTLdrLoadEx(szPath, &ahMods[iLib], RTLDRLOAD_FLAGS_NT_SEARCH_DLL_LOAD_DIR, NULL);
                            if (RT_SUCCESS(rc))
                                RTMEM_WILL_LEAK(ahMods[iLib]);
                            else
                                ahMods[iLib] = NIL_RTLDRMOD;
                        }
                }
# endif /* RT_OS_SOLARIS */
            }
            if (iLib == RT_ELEMENTS(s_apszLibraries) && RT_SUCCESS(rc))
            {
                static const struct
                {
                    unsigned    iLib;
                    const char *pszSymbol;
                    uintptr_t  *ppfn;   /**< The nothrow attrib of PFNRT goes down the wrong way with Clang 11, thus uintptr_t. */
                } s_aSymbols[] =
                {
                    { 2, "apr_initialize",              (uintptr_t *)&g_pfnAprInitialize },
                    { 2, "apr_hash_first",              (uintptr_t *)&g_pfnAprHashFirst },
                    { 2, "apr_hash_next",               (uintptr_t *)&g_pfnAprHashNext },
                    { 2, "apr_hash_this_val",           (uintptr_t *)&g_pfnAprHashThisVal },
                    { 1, "svn_pool_create_ex",          (uintptr_t *)&g_pfnSvnPoolCreateEx },
                    { 2, "apr_pool_clear",              (uintptr_t *)&g_pfnAprPoolClear },
                    { 2, "apr_pool_destroy",            (uintptr_t *)&g_pfnAprPoolDestroy },
                    { 0, "svn_client_create_context",   (uintptr_t *)&g_pfnSvnClientCreateContext },
                    { 0, "svn_client_propget4",         (uintptr_t *)&g_pfnSvnClientPropGet4 },
                };
                for (unsigned i = 0; i < RT_ELEMENTS(s_aSymbols); i++)
                {
                    rc = RTLdrGetSymbol(ahMods[s_aSymbols[i].iLib], s_aSymbols[i].pszSymbol,
                                        (void **)(uintptr_t)s_aSymbols[i].ppfn);
                    if (RT_FAILURE(rc))
                    {
                        ScmVerbose(NULL, 0, "Failed to resolve '%s' in '%s'",
                                   s_aSymbols[i].pszSymbol, s_apszLibraries[s_aSymbols[i].iLib]);
                        break;
                    }
                }
                if (RT_SUCCESS(rc))
                {
                    apr_status_t rcApr = g_pfnAprInitialize();
                    if (rcApr == 0)
                    {
                        ScmVerbose(NULL, 1, "Found subversion APIs.\n");
                        g_fSvnFunctionPointersValid = true;
                    }
                    else
                    {
                        ScmVerbose(NULL, 0, "apr_initialize failed: %#x (%d)\n", rcApr, rcApr);
                        AssertMsgFailed(("%#x (%d)\n", rc, rc));
                    }
                    return;
                }
            }

            while (iLib-- > 0)
                RTLdrClose(ahMods[iLib]);
        }
    }
}
#endif /* SCM_WITH_DYNAMIC_LIB_SVN */


/**
 * Finds the svn binary, updating g_szSvnPath and g_enmSvnVersion.
 */
static void scmSvnFindSvnBinary(PSCMRWSTATE pState)
{
    /* Already been called? */
    if (g_szSvnPath[0] != '\0')
        return;

    /*
     * Locate it.
     */
    /** @todo code page fun... */
#ifdef RT_OS_WINDOWS
    const char *pszEnvVar = RTEnvGet("Path");
#else
    const char *pszEnvVar = RTEnvGet("PATH");
#endif
    if (pszEnvVar)
    {
#if defined(RT_OS_WINDOWS) || defined(RT_OS_OS2)
        int rc = RTPathTraverseList(pszEnvVar, ';', scmSvnFindSvnBinaryCallback, g_szSvnPath, (void *)sizeof(g_szSvnPath));
#else
        int rc = RTPathTraverseList(pszEnvVar, ':', scmSvnFindSvnBinaryCallback, g_szSvnPath, (void *)sizeof(g_szSvnPath));
#endif
        if (RT_FAILURE(rc))
            strcpy(g_szSvnPath, "svn");
    }
    else
        strcpy(g_szSvnPath, "svn");

    /*
     * Check the version.
     */
    const char *apszArgs[] = { g_szSvnPath, "--version", "--quiet", NULL };
    char *pszVersion;
    int rc = scmSvnRunAndGetOutput(pState, apszArgs, false, &pszVersion);
    if (RT_SUCCESS(rc))
    {
        char *pszStripped = RTStrStrip(pszVersion);
        if (RTStrVersionCompare(pszStripped, "1.8") >= 0)
            g_enmSvnVersion = kScmSvnVersion_1_8;
        else if (RTStrVersionCompare(pszStripped, "1.7") >= 0)
            g_enmSvnVersion = kScmSvnVersion_1_7;
        else if (RTStrVersionCompare(pszStripped, "1.6") >= 0)
            g_enmSvnVersion = kScmSvnVersion_1_6;
        else
            g_enmSvnVersion = kScmSvnVersion_Ancient;
        RTStrFree(pszVersion);
    }
    else
        g_enmSvnVersion = kScmSvnVersion_Ancient;

#ifdef SCM_WITH_DYNAMIC_LIB_SVN
    /*
     * If we got version 1.8 or later, try see if we can locate a few of the
     * simpler SVN APIs.
     */
    g_fSvnFunctionPointersValid = false;
    if (g_enmSvnVersion >= kScmSvnVersion_1_8)
        scmSvnTryResolveFunctions();
#endif
}


/**
 * Construct a dot svn filename for the file being rewritten.
 *
 * @returns IPRT status code.
 * @param   pState              The rewrite state (for the name).
 * @param   pszDir              The directory, including ".svn/".
 * @param   pszSuff             The filename suffix.
 * @param   pszDst              The output buffer.  RTPATH_MAX in size.
 */
static int scmSvnConstructName(PSCMRWSTATE pState, const char *pszDir, const char *pszSuff, char *pszDst)
{
    strcpy(pszDst, pState->pszFilename); /* ASSUMES sizeof(szBuf) <= sizeof(szPath) */
    RTPathStripFilename(pszDst);

    int rc = RTPathAppend(pszDst, RTPATH_MAX, pszDir);
    if (RT_SUCCESS(rc))
    {
        rc = RTPathAppend(pszDst, RTPATH_MAX, RTPathFilename(pState->pszFilename));
        if (RT_SUCCESS(rc))
        {
            size_t cchDst  = strlen(pszDst);
            size_t cchSuff = strlen(pszSuff);
            if (cchDst + cchSuff < RTPATH_MAX)
            {
                memcpy(&pszDst[cchDst], pszSuff, cchSuff + 1);
                return VINF_SUCCESS;
            }
            else
                rc = VERR_BUFFER_OVERFLOW;
        }
    }
    return rc;
}

/**
 * Interprets the specified string as decimal numbers.
 *
 * @returns true if parsed successfully, false if not.
 * @param   pch                 The string (not terminated).
 * @param   cch                 The string length.
 * @param   pu                  Where to return the value.
 */
static bool scmSvnReadNumber(const char *pch, size_t cch, size_t *pu)
{
    size_t u = 0;
    while (cch-- > 0)
    {
        char ch = *pch++;
        if (ch < '0' || ch > '9')
            return false;
        u *= 10;
        u += ch - '0';
    }
    *pu = u;
    return true;
}


#ifdef SCM_WITH_DYNAMIC_LIB_SVN

/**
 * Wrapper around RTPathAbs.
 * @returns Same as RTPathAbs.
 * @param   pszPath             The relative path.
 * @param   pszAbsPath          Where to return the absolute path.
 * @param   cbAbsPath           Size of the @a pszAbsPath buffer.
 */
static int scmSvnAbsPath(const char *pszPath, char *pszAbsPath, size_t cbAbsPath)
{
    int rc = RTPathAbs(pszPath, pszAbsPath, cbAbsPath);
# if RTPATH_STYLE == RTPATH_STR_F_STYLE_DOS
    if (RT_SUCCESS(rc))
    {
        RTPathChangeToUnixSlashes(pszAbsPath, true /*fForce*/);
        /* To avoid: svn: E235000: In file '..\..\..\subversion\libsvn_client\prop_commands.c' line 796: assertion failed (svn_dirent_is_absolute(target)) */
        if (pszAbsPath[1] == ':')
            pszAbsPath[0] = RT_C_TO_UPPER(pszAbsPath[0]);
    }
# endif
    return rc;
}


/**
 * Gets a client context and pool.
 *
 * This implements caching.
 *
 * @returns IPRT status code.
 * @param   ppCtx               Where to return the context
 * @param   ppPool              Where to return the pool.
 */
static int scmSvnGetClientContextAndPool(svn_client_ctx_t **ppCtx, apr_pool_t **ppPool)
{
    /*
     * Use cached if present.
     */
    if (g_pSvnClientCtx && g_pSvnPool)
    {
        g_cSvnClientCtxUsed++;
        *ppCtx  = g_pSvnClientCtx;
        *ppPool = g_pSvnPool;
        return VINF_SUCCESS;
    }
    Assert(!g_pSvnClientCtx);
    Assert(!g_pSvnPool);

    /*
     * Create new pool and context.
     */
    apr_pool_t *pPool = g_pfnSvnPoolCreateEx(NULL, NULL);
    if (pPool)
    {
        svn_client_ctx_t *pCtx = NULL;
        svn_error_t *pErr = g_pfnSvnClientCreateContext(&pCtx, pPool);
        if (!pErr)
        {
            g_cSvnClientCtxUsed = 1;
            g_pSvnClientCtx     = *ppCtx  = pCtx;
            g_pSvnPool          = *ppPool = pPool;
            return VINF_SUCCESS;
        }
        g_pfnAprPoolDestroy(pPool);
    }

    *ppCtx  = NULL;
    *ppPool = NULL;
    return VERR_GENERAL_FAILURE;
}


/**
 * Puts back a client context and pool after use.
 *
 * @param   pCtx                The context.
 * @param   pPool               The pool.
 * @param   fFlush              Whether to flush it.
 */
static void scmSvnPutClientContextAndPool(svn_client_ctx_t *pCtx, apr_pool_t *pPool, bool fFlush)
{
    if (fFlush || g_cSvnClientCtxUsed > 4096) /* Disable this to force new context every time. */
    {
        g_pfnAprPoolDestroy(pPool);
        g_pSvnPool = NULL;
        g_pSvnClientCtx = NULL;
    }
    RT_NOREF(pCtx, fFlush);
}


/**
 * Flushes the cached client context and pool
 */
static void scmSvnFlushClientContextAndPool(void)
{
    if (g_pSvnPool)
        scmSvnPutClientContextAndPool(g_pSvnClientCtx, g_pSvnPool, true /*fFlush*/);
    Assert(!g_pSvnPool);
}


/**
 * Checks if @a pszPath exists in the current WC.
 *
 * @returns true, false or -1. In the latter case, please use the fallback.
 * @param   pszPath         Path to the object that should be investigated.
 */
static int scmSvnIsObjectInWorkingCopy(const char *pszPath)
{
    /* svn_client_propget4 and later requires absolute target path. */
    char szAbsPath[RTPATH_MAX];
    int  rc = scmSvnAbsPath(pszPath, szAbsPath, sizeof(szAbsPath));
    if (RT_SUCCESS(rc))
    {
        apr_pool_t *pPool;
        svn_client_ctx_t *pCtx = NULL;
        rc = scmSvnGetClientContextAndPool(&pCtx, &pPool);
        if (RT_SUCCESS(rc))
        {
            /* Make the call. */
            apr_hash_t         *pHash = NULL;
            svn_opt_revision_t  Rev;
            RT_ZERO(Rev);
            Rev.kind          = svn_opt_revision_working;
            Rev.value.number  = -1L;
            svn_error_t *pErr = g_pfnSvnClientPropGet4(&pHash, "svn:no-such-property", szAbsPath, &Rev, &Rev,
                                                       NULL /*pActualRev*/, svn_depth_empty, NULL /*pChangeList*/,
                                                       pCtx, pPool, pPool);
            if (!pErr)
                rc = true;
            else if (pErr->apr_err == SVN_ERR_UNVERSIONED_RESOURCE)
                rc = false;

            scmSvnPutClientContextAndPool(pCtx, pPool, false);
        }
    }
    return rc;
}

#endif /* SCM_WITH_DYNAMIC_LIB_SVN */


/**
 * Checks if the file we're operating on is part of a SVN working copy.
 *
 * @returns true if it is, false if it isn't or we cannot tell.
 * @param   pState      The rewrite state to work on.  Will use the
 *                      fIsInSvnWorkingCopy member for caching the result.
 */
bool ScmSvnIsInWorkingCopy(PSCMRWSTATE pState)
{
    /*
     * We don't ask SVN twice as that's expensive.
     */
    if (pState->fIsInSvnWorkingCopy != 0)
        return pState->fIsInSvnWorkingCopy > 0;

#ifdef SCM_WITH_DYNAMIC_LIB_SVN
    if (g_fSvnFunctionPointersValid)
    {
        int rc = scmSvnIsObjectInWorkingCopy(pState->pszFilename);
        if (rc == (int)true || rc == (int)false)
        {
            pState->fIsInSvnWorkingCopy = rc == (int)true ? 1 : -1;
            return rc == (int)true;
        }
    }

    /* Fallback: */
#endif
    if (g_enmSvnVersion < kScmSvnVersion_1_7)
    {
        /*
         * Hack: check if the .svn/text-base/<file>.svn-base file exists.
         */
        char szPath[RTPATH_MAX];
        int rc = scmSvnConstructName(pState, ".svn/text-base/", ".svn-base", szPath);
        if (RT_SUCCESS(rc))
        {
            if (RTFileExists(szPath))
            {
                pState->fIsInSvnWorkingCopy = 1;
                return true;
            }
        }
    }
    else
    {
        const char *apszArgs[] = { g_szSvnPath, "proplist", pState->pszFilename, NULL };
        char       *pszValue;
        int rc = scmSvnRunAndGetOutput(pState, apszArgs, true, &pszValue);
        if (RT_SUCCESS(rc))
        {
            RTStrFree(pszValue);
            pState->fIsInSvnWorkingCopy = 1;
            return true;
        }
    }
    pState->fIsInSvnWorkingCopy = -1;
    return false;
}


/**
 * Checks if the specified directory is part of a SVN working copy.
 *
 * @returns true if it is, false if it isn't or we cannot tell.
 * @param   pszDir              The directory in question.
 */
bool ScmSvnIsDirInWorkingCopy(const char *pszDir)
{
#ifdef SCM_WITH_DYNAMIC_LIB_SVN
    if (g_fSvnFunctionPointersValid)
    {
        int rc = scmSvnIsObjectInWorkingCopy(pszDir);
        if (rc == (int)true || rc == (int)false)
            return rc == (int)true;
    }

    /* Fallback: */
#endif
    if (g_enmSvnVersion < kScmSvnVersion_1_7)
    {
        /*
         * Hack: check if the .svn/ dir exists.
         */
        char szPath[RTPATH_MAX];
        int rc = RTPathJoin(szPath, sizeof(szPath), pszDir, ".svn");
        if (RT_SUCCESS(rc))
            return RTDirExists(szPath);
    }
    else
    {
        const char *apszArgs[] = { g_szSvnPath, "propget", "svn:no-such-property", pszDir, NULL };
        char       *pszValue;
        int rc = scmSvnRunAndGetOutput(NULL, apszArgs, true, &pszValue);
        if (RT_SUCCESS(rc))
        {
            RTStrFree(pszValue);
            return true;
        }
    }
    return false;
}


#ifdef SCM_WITH_DYNAMIC_LIB_SVN
/**
 * Checks if @a pszPath exists in the current WC.
 *
 * @returns IPRT status code - VERR_NOT_SUPPORT if fallback should be attempted.
 * @param   pszPath         Path to the object that should be investigated.
 * @param   pszProperty     The property name.
 * @param   ppszValue       Where to return the property value. Optional.
 */
static int scmSvnQueryPropertyUsingApi(const char *pszPath, const char *pszProperty, char **ppszValue)
{
    /* svn_client_propget4 and later requires absolute target path. */
    char szAbsPath[RTPATH_MAX];
    int  rc = scmSvnAbsPath(pszPath, szAbsPath, sizeof(szAbsPath));
    if (RT_SUCCESS(rc))
    {
        apr_pool_t *pPool;
        svn_client_ctx_t *pCtx = NULL;
        rc = scmSvnGetClientContextAndPool(&pCtx, &pPool);
        if (RT_SUCCESS(rc))
        {
            /* Make the call. */
            apr_hash_t         *pHash = NULL;
            svn_opt_revision_t  Rev;
            RT_ZERO(Rev);
            Rev.kind          = svn_opt_revision_working;
            Rev.value.number  = -1L;
            svn_error_t *pErr = g_pfnSvnClientPropGet4(&pHash, pszProperty, szAbsPath, &Rev, &Rev,
                                                       NULL /*pActualRev*/, svn_depth_empty, NULL /*pChangeList*/,
                                                       pCtx, pPool, pPool);
            if (!pErr)
            {
                /* Get the first value, if any. */
                rc = VERR_NOT_FOUND;
                apr_hash_index_t *pHashIdx = g_pfnAprHashFirst(pPool, pHash);
                if (pHashIdx)
                {
                    const char **ppszFirst = (const char **)g_pfnAprHashThisVal(pHashIdx);
                    if (ppszFirst && *ppszFirst)
                    {
                        if (ppszValue)
                            rc = RTStrDupEx(ppszValue, *ppszFirst);
                        else
                            rc = VINF_SUCCESS;
                    }
                }
            }
            else if (pErr->apr_err == SVN_ERR_UNVERSIONED_RESOURCE)
                rc = VERR_INVALID_STATE;
            else
                rc = VERR_GENERAL_FAILURE;

            scmSvnPutClientContextAndPool(pCtx, pPool, false);
        }
    }
    return rc;
}
#endif /* SCM_WITH_DYNAMIC_LIB_SVN */


/**
 * Queries the value of an SVN property.
 *
 * This will automatically adjust for scheduled changes.
 *
 * @returns IPRT status code.
 * @retval  VERR_INVALID_STATE if not a SVN WC file.
 * @retval  VERR_NOT_FOUND if the property wasn't found.
 * @param   pState              The rewrite state to work on.
 * @param   pszName             The property name.
 * @param   ppszValue           Where to return the property value.  Free this
 *                              using RTStrFree.  Optional.
 */
int ScmSvnQueryProperty(PSCMRWSTATE pState, const char *pszName, char **ppszValue)
{
    int rc;

    /*
     * Look it up in the scheduled changes.
     */
    size_t i = pState->cSvnPropChanges;
    while (i-- > 0)
        if (!strcmp(pState->paSvnPropChanges[i].pszName, pszName))
        {
            const char *pszValue = pState->paSvnPropChanges[i].pszValue;
            if (!pszValue)
                return VERR_NOT_FOUND;
            if (ppszValue)
                return RTStrDupEx(ppszValue, pszValue);
            return VINF_SUCCESS;
        }

#ifdef SCM_WITH_DYNAMIC_LIB_SVN
    if (g_fSvnFunctionPointersValid)
    {
        rc = scmSvnQueryPropertyUsingApi(pState->pszFilename, pszName, ppszValue);
        if (rc != VERR_NOT_SUPPORTED)
            return rc;
        /* Fallback: */
    }
#endif

    if (g_enmSvnVersion < kScmSvnVersion_1_7)
    {
        /*
         * Hack: Read the .svn/props/<file>.svn-work file exists.
         */
        char szPath[RTPATH_MAX];
        rc = scmSvnConstructName(pState, ".svn/props/", ".svn-work", szPath);
        if (RT_SUCCESS(rc) && !RTFileExists(szPath))
            rc = scmSvnConstructName(pState, ".svn/prop-base/", ".svn-base", szPath);
        if (RT_SUCCESS(rc))
        {
            SCMSTREAM Stream;
            rc = ScmStreamInitForReading(&Stream, szPath);
            if (RT_SUCCESS(rc))
            {
                /*
                 * The current format is K len\n<name>\nV len\n<value>\n" ... END.
                 */
                rc = VERR_NOT_FOUND;
                size_t const    cchName = strlen(pszName);
                SCMEOL          enmEol;
                size_t          cchLine;
                const char     *pchLine;
                while ((pchLine = ScmStreamGetLine(&Stream, &cchLine, &enmEol)) != NULL)
                {
                    /*
                     * Parse the 'K num' / 'END' line.
                     */
                    if (   cchLine == 3
                        && !memcmp(pchLine, "END", 3))
                        break;
                    size_t cchKey;
                    if (   cchLine < 3
                        || pchLine[0] != 'K'
                        || pchLine[1] != ' '
                        || !scmSvnReadNumber(&pchLine[2], cchLine - 2, &cchKey)
                        || cchKey == 0
                        || cchKey > 4096)
                    {
                        RTMsgError("%s:%u: Unexpected data '%.*s'\n", szPath, ScmStreamTellLine(&Stream), cchLine, pchLine);
                        rc = VERR_PARSE_ERROR;
                        break;
                    }

                    /*
                     * Match the key and skip to the value line.  Don't bother with
                     * names containing EOL markers.
                     */
                    size_t const offKey = ScmStreamTell(&Stream);
                    bool fMatch = cchName == cchKey;
                    if (fMatch)
                    {
                        pchLine = ScmStreamGetLine(&Stream, &cchLine, &enmEol);
                        if (!pchLine)
                            break;
                        fMatch = cchLine == cchName
                              && !memcmp(pchLine, pszName, cchName);
                    }

                    if (RT_FAILURE(ScmStreamSeekAbsolute(&Stream, offKey + cchKey)))
                        break;
                    if (RT_FAILURE(ScmStreamSeekByLine(&Stream, ScmStreamTellLine(&Stream) + 1)))
                        break;

                    /*
                     * Read and Parse the 'V num' line.
                     */
                    pchLine = ScmStreamGetLine(&Stream, &cchLine, &enmEol);
                    if (!pchLine)
                        break;
                    size_t cchValue;
                    if (   cchLine < 3
                        || pchLine[0] != 'V'
                        || pchLine[1] != ' '
                        || !scmSvnReadNumber(&pchLine[2], cchLine - 2, &cchValue)
                        || cchValue > _1M)
                    {
                        RTMsgError("%s:%u: Unexpected data '%.*s'\n", szPath, ScmStreamTellLine(&Stream), cchLine, pchLine);
                        rc = VERR_PARSE_ERROR;
                        break;
                    }

                    /*
                     * If we have a match, allocate a return buffer and read the
                     * value into it.  Otherwise skip this value and continue
                     * searching.
                     */
                    if (fMatch)
                    {
                        if (!ppszValue)
                            rc = VINF_SUCCESS;
                        else
                        {
                            char *pszValue;
                            rc = RTStrAllocEx(&pszValue, cchValue + 1);
                            if (RT_SUCCESS(rc))
                            {
                                rc = ScmStreamRead(&Stream, pszValue, cchValue);
                                if (RT_SUCCESS(rc))
                                    *ppszValue = pszValue;
                                else
                                    RTStrFree(pszValue);
                            }
                        }
                        break;
                    }

                    if (RT_FAILURE(ScmStreamSeekRelative(&Stream, cchValue)))
                        break;
                    if (RT_FAILURE(ScmStreamSeekByLine(&Stream, ScmStreamTellLine(&Stream) + 1)))
                        break;
                }

                if (RT_FAILURE(ScmStreamGetStatus(&Stream)))
                {
                    rc = ScmStreamGetStatus(&Stream);
                    RTMsgError("%s: stream error %Rrc\n", szPath, rc);
                }
                ScmStreamDelete(&Stream);
            }
        }

        if (rc == VERR_FILE_NOT_FOUND)
            rc = VERR_NOT_FOUND;
    }
    else
    {
        const char *apszArgs[] = { g_szSvnPath, "propget", "--strict", pszName, pState->pszFilename, NULL };
        char       *pszValue;
        rc = scmSvnRunAndGetOutput(pState, apszArgs, false, &pszValue);
        if (RT_SUCCESS(rc))
        {
            if (pszValue && *pszValue)
            {
                if (ppszValue)
                {
                    *ppszValue = pszValue;
                    pszValue = NULL;
                }
            }
            else
                rc = VERR_NOT_FOUND;
            RTStrFree(pszValue);
        }
    }
    return rc;
}


/**
 * Queries the value of an SVN property on the parent dir/whatever.
 *
 * This will not adjust for scheduled changes to the parent!
 *
 * @returns IPRT status code.
 * @retval  VERR_INVALID_STATE if not a SVN WC file.
 * @retval  VERR_NOT_FOUND if the property wasn't found.
 * @param   pState              The rewrite state to work on.
 * @param   pszName             The property name.
 * @param   ppszValue           Where to return the property value.  Free this
 *                              using RTStrFree.  Optional.
 */
int ScmSvnQueryParentProperty(PSCMRWSTATE pState, const char *pszName, char **ppszValue)
{
    /*
     * Strip the filename and use ScmSvnQueryProperty.
     */
    char szPath[RTPATH_MAX];
    int rc = RTStrCopy(szPath, sizeof(szPath), pState->pszFilename);
    if (RT_SUCCESS(rc))
    {
        RTPathStripFilename(szPath);
        SCMRWSTATE ParentState;
        ParentState.pszFilename         = szPath;
        ParentState.fFirst              = false;
        ParentState.fNeedsManualRepair  = false;
        ParentState.fIsInSvnWorkingCopy = true;
        ParentState.cSvnPropChanges     = 0;
        ParentState.paSvnPropChanges    = NULL;
        ParentState.rc                  = VINF_SUCCESS;
        rc = ScmSvnQueryProperty(&ParentState, pszName, ppszValue);
        if (RT_SUCCESS(rc))
            rc = ParentState.rc;
    }
    return rc;
}


/**
 * Schedules the setting of a property.
 *
 * @returns IPRT status code.
 * @retval  VERR_INVALID_STATE if not a SVN WC file.
 * @param   pState              The rewrite state to work on.
 * @param   pszName             The name of the property to set.
 * @param   pszValue            The value.  NULL means deleting it.
 */
int ScmSvnSetProperty(PSCMRWSTATE pState, const char *pszName, const char *pszValue)
{
    /*
     * Update any existing entry first.
     */
    size_t i = pState->cSvnPropChanges;
    while (i-- > 0)
        if (!strcmp(pState->paSvnPropChanges[i].pszName,  pszName))
        {
            if (!pszValue)
            {
                RTStrFree(pState->paSvnPropChanges[i].pszValue);
                pState->paSvnPropChanges[i].pszValue = NULL;
            }
            else
            {
                char *pszCopy;
                int rc = RTStrDupEx(&pszCopy, pszValue);
                if (RT_FAILURE(rc))
                    return rc;
                pState->paSvnPropChanges[i].pszValue = pszCopy;
            }
            return VINF_SUCCESS;
        }

    /*
     * Insert a new entry.
     */
    i = pState->cSvnPropChanges;
    if ((i % 32) == 0)
    {
        void *pvNew = RTMemRealloc(pState->paSvnPropChanges, (i + 32) * sizeof(SCMSVNPROP));
        if (!pvNew)
            return VERR_NO_MEMORY;
        pState->paSvnPropChanges = (PSCMSVNPROP)pvNew;
    }

    pState->paSvnPropChanges[i].pszName  = RTStrDup(pszName);
    pState->paSvnPropChanges[i].pszValue = pszValue ? RTStrDup(pszValue) : NULL;
    if (   pState->paSvnPropChanges[i].pszName
        && (pState->paSvnPropChanges[i].pszValue || !pszValue) )
        pState->cSvnPropChanges = i + 1;
    else
    {
        RTStrFree(pState->paSvnPropChanges[i].pszName);
        pState->paSvnPropChanges[i].pszName = NULL;
        RTStrFree(pState->paSvnPropChanges[i].pszValue);
        pState->paSvnPropChanges[i].pszValue = NULL;
        return VERR_NO_MEMORY;
    }
    return VINF_SUCCESS;
}


/**
 * Schedules a property deletion.
 *
 * @returns IPRT status code.
 * @param   pState              The rewrite state to work on.
 * @param   pszName             The name of the property to delete.
 */
int ScmSvnDelProperty(PSCMRWSTATE pState, const char *pszName)
{
    return ScmSvnSetProperty(pState, pszName, NULL);
}


/**
 * Applies any SVN property changes to the work copy of the file.
 *
 * @returns IPRT status code.
 * @param   pState              The rewrite state which SVN property changes
 *                              should be applied.
 */
int ScmSvnDisplayChanges(PSCMRWSTATE pState)
{
    size_t i = pState->cSvnPropChanges;
    while (i-- > 0)
    {
        const char *pszName  = pState->paSvnPropChanges[i].pszName;
        const char *pszValue = pState->paSvnPropChanges[i].pszValue;
        if (pszValue)
            ScmVerbose(pState, 0, "svn propset '%s' '%s' %s\n", pszName, pszValue, pState->pszFilename);
        else
            ScmVerbose(pState, 0, "svn propdel '%s' %s\n", pszName, pState->pszFilename);
    }

    return VINF_SUCCESS;
}

/**
 * Applies any SVN property changes to the work copy of the file.
 *
 * @returns IPRT status code.
 * @param   pState              The rewrite state which SVN property changes
 *                              should be applied.
 */
int ScmSvnApplyChanges(PSCMRWSTATE pState)
{
#ifdef SCM_WITH_LATER
    if (0)
    {
        return ...;
    }

    /* Fallback: */
#endif

    /*
     * Iterate thru the changes and apply them by starting the svn client.
     */
    for (size_t i = 0; i < pState->cSvnPropChanges; i++)
    {
        const char *apszArgv[6];
        apszArgv[0] = g_szSvnPath;
        apszArgv[1] = pState->paSvnPropChanges[i].pszValue ? "propset" : "propdel";
        apszArgv[2] = pState->paSvnPropChanges[i].pszName;
        int iArg = 3;
        if (pState->paSvnPropChanges[i].pszValue)
            apszArgv[iArg++] = pState->paSvnPropChanges[i].pszValue;
        apszArgv[iArg++] = pState->pszFilename;
        apszArgv[iArg++] = NULL;

        int rc = scmSvnRun(pState, apszArgv, false);
        if (RT_FAILURE(rc))
            return rc;
    }

    return VINF_SUCCESS;
}


/**
 * Initializes the subversion interface.
 */
void ScmSvnInit(void)
{
    scmSvnFindSvnBinary(NULL);
}


void ScmSvnTerm(void)
{
#ifdef SCM_WITH_DYNAMIC_LIB_SVN
    scmSvnFlushClientContextAndPool();
#endif
}
