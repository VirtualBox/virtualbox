/* $Id: clipboard-wayland-popup.cpp 114738 2026-07-21 13:40:26Z knut.osmundsen@oracle.com $ */
/** @file
 * Guest Additions - Wayland Shared Clipboard, Basic Protocol Popups.
 */

/*
 * Copyright (C) 2006-2026 Oracle and/or its affiliates.
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
#include <iprt/env.h>
#include <iprt/pipe.h>
#include <iprt/process.h>
#include <iprt/semaphore.h>
#include <iprt/string.h>

#include "VBoxClient.h"
#include "clipboard.h"


/*********************************************************************************************************************************
*   Defined Constants And Macros                                                                                                 *
*********************************************************************************************************************************/
#define SZ_OPT_VERBOSITY    "--verbosity"


/**
 * Runs --clipboard-get to check the guest's clipboard when the host VM process
 * loses focus.
 */
int VBClClipboardWaylandPopupGetAll(PSHCLCONTEXT pCtx)
{
    LogRel3(("%s:\n", __func__));

    /*
     * Take down the revision number before we start, so we won't can detect
     * host clipboard updates racing us.
     */
    RTCritSectEnter(&pCtx->Wl.CritSect);
    uint64_t const uRevision = pCtx->Wl.uRevision;
    RTCritSectLeave(&pCtx->Wl.CritSect);

    /*
     * Read the clipboard content.
     */
    /* Create pipe for reading content. */
    RTHANDLE hPipeRead  = { RTHANDLETYPE_PIPE };
    RTHANDLE hStdOut    = { RTHANDLETYPE_PIPE };
    int rc = RTPipeCreate(&hPipeRead.u.hPipe, &hStdOut.u.hPipe, RTPIPE_C_INHERIT_WRITE);
    if (RT_SUCCESS(rc))
    {
        /* Pass on the log verbosity level. */
        char szSetVerbosity[sizeof(SZ_OPT_VERBOSITY) + 64];
        RTStrPrintf(szSetVerbosity, sizeof(szSetVerbosity), SZ_OPT_VERBOSITY "=%u", g_cVerbosity);
        const char *apszArgs[] =
        {
            RTProcExecutablePath(),
            "--clipboard-get",
            szSetVerbosity,
            NULL
        };

        /* Create the process. */
        RTPROCESS hProcess = NIL_RTPROCESS;
        rc = RTProcCreateEx(apszArgs[0], apszArgs, RTENV_DEFAULT, 0 /*fFlags*/,
                            NULL, &hStdOut, NULL, NULL, NULL, NULL, &hProcess);
        RTPipeClose(hStdOut.u.hPipe);
        if (RT_SUCCESS(rc))
        {
            /*
             * Read and process the output.
             */
            SHCLFORMATS fFormats = UINT32_MAX;
            SHCLCACHE   Cache;
            ShClCacheInit(&Cache);
            rc = VBClClipboardDeserializeCache(&hPipeRead, &Cache, &fFormats, RT_MS_30SEC);
            bool const fDataOkay = RT_SUCCESS(rc);

            rc = RTPipeClose(hPipeRead.u.hPipe);
            AssertLogRelRC(rc);

            /*
             * Make sure it terminates.
             */
            RTPROCSTATUS ProcStatus;
            rc = VbghWaylandPopupTerminateAndWaitForChild(hProcess, "--clipboard-get", RT_MS_1SEC, RT_MS_5SEC, 3, &ProcStatus);
            if (   RT_SUCCESS(rc)
                && ProcStatus.enmReason == RTPROCEXITREASON_NORMAL)
            {
                /*
                 * RTEXITCODE_SUCCESS: We've got data. Report it to the host.
                 */
                if (ProcStatus.iStatus == RTEXITCODE_SUCCESS && fDataOkay)
                {
                    RTCritSectEnter(&pCtx->Wl.CritSect);
                    if (pCtx->Wl.uRevision == uRevision)
                    {
                        if (   SHCLWLCTX_REV_IS_OTHER(pCtx->Wl.uRevision)
                            || pCtx->Wl.fOurFormats != fFormats
                            || !ShClCacheEquals(&pCtx->Wl.OurCache, &Cache))
                        {
                            LogRel2(("%s: --clipboard-get exit code: 0 - sending formats %#x and data to the host\n",
                                     __func__, fFormats));

                            VbghWaylandClipboardResetOurState(&pCtx->Wl, __func__, NULL);

                            pCtx->Wl.fOurFormats = fFormats;
                            rc = ShClCacheTransferAll(&pCtx->Wl.OurCache, &Cache);
                            AssertRC(rc);

                            rc = RTSemEventMultiSignal(pCtx->Wl.hOurCacheFilledEvent);
                            AssertRC(rc);

                            /* Do the reporting to the host. */
                            rc = VbglR3ClipboardReportFormats(pCtx->CmdCtx.idClient, fFormats);
                            AssertLogRelRC(rc);
                        }
                        else
                            LogRel2(("%s: --clipboard-get: skipping. data is unchanged\n", __func__));
                    }
                    else
                        LogRel2(("%s: --clipboard-get: Revision changed while getting guest clipboard data (%#RX64 -> %#RX64)\n",
                                 __func__, uRevision, pCtx->Wl.uRevision));
                    RTCritSectLeave(&pCtx->Wl.CritSect);
                }
                /*
                 * RTEXITCODE_SKIPPED: Clipboard contains host data or no data at all.
                 */
                else if (ProcStatus.iStatus == RTEXITCODE_SKIPPED && fDataOkay && fFormats == 0)
                    LogRel2(("%s: --clipboard-get exit code: skipped\n", __func__));
                /*
                 * Anything else is bad.
                 */
                else
                    VBClLogError("%s: --clipboard-get exit code: %d, fDataOkay=%d fFormats=%#x\n",
                                 __func__, ProcStatus.iStatus, fDataOkay, fFormats);
            }
            else if (RT_SUCCESS(rc) && ProcStatus.enmReason == RTPROCEXITREASON_SIGNAL)
                VBClLogError("%s: --clipboard-get terminated by signal: %d\n", __func__, ProcStatus.iStatus);
            else if (RT_SUCCESS(rc))
                VBClLogError("%s: --clipboard-get abend'ed (%d)\n", __func__, ProcStatus.iStatus);
            else
                VBClLogError("%s: RTProcWait failed on --clipboard-get: %Rrc\n", __func__, rc);
            ShClCacheTerm(&Cache);
        }
        else
        {
            VBClLogError("RTProcCreateEx failed: %Rrc\n", rc);
            RTPipeClose(hPipeRead.u.hPipe);
        }
    }
    else
        VBClLogError("RTPipeCreate failed: %Rrc\n", rc);
    return VINF_SUCCESS;
}


/**
 * Spawns --clipboard-set to offer the guest the content of the host clipboard.
 */
int VBClClipboardWaylandPopupSetAll(PSHCLCONTEXT pCtx, SHCLFORMATS fFormats)
{
    LogRel3(("%s:\n", __func__));

    /*
     * Nudge any existing --clipboard-set process into quitting while we
     * retrieve the host data.
     */
    RTPROCESS const hProcPrev = pCtx->Wl.hProcClipboardSet;
    if (hProcPrev != NIL_RTPROCESS)
    {
        pCtx->Wl.hProcClipboardSet = NIL_RTPROCESS;
        RTPipeClose(pCtx->Wl.hPipeClipboardSet);
        pCtx->Wl.hPipeClipboardSet = NIL_RTPIPE;
    }

    /*
     * Read all the host formats into the cache.
     */
    RTCritSectEnter(&pCtx->Wl.CritSect);
    uint64_t const uRevision = pCtx->Wl.uRevision;
    RTCritSectLeave(&pCtx->Wl.CritSect);
    if (!SHCLWLCTX_REV_IS_OTHER(uRevision))
    {
        LogRel2(("%s: Owner switched (rev %#RX64)\n", __func__, uRevision));
        return VINF_SUCCESS;
    }

    SHCLFORMATS fFormatsLeft = fFormats;
    while (fFormatsLeft)
    {
        /* Get the first format left in the mask. */
        SHCLFORMAT const uCurFormat = fFormatsLeft & ~(fFormatsLeft - 1);
        Assert(uCurFormat && (fFormatsLeft & uCurFormat));
        fFormatsLeft &= ~uCurFormat;

        /* Retrieve the data. */
        void    *pvData = NULL;
        uint32_t cbData = 0;
        int rc = VbglR3ClipboardReadDataEx(&pCtx->CmdCtx, uCurFormat, &pvData, &cbData);

        RTCritSectEnter(&pCtx->Wl.CritSect);

        /* Recheck the revision number before continuing. */
        if (uRevision != pCtx->Wl.uRevision)
        {
            LogRel2(("%s: Revision changed while getting host data: %#RX64 -> %#RX64\n", __func__, uRevision, pCtx->Wl.uRevision));
            RTCritSectLeave(&pCtx->Wl.CritSect);
            if (RT_SUCCESS(rc))
                RTMemFree(pvData);
            return VINF_SUCCESS;
        }

        /* Enter the data into the cache on success.  A failure here is probably
           an indicator that the host clipboard has changed again, but we'll just
           remove the format from the mask for now as we don't know that for sure. */
        if (RT_SUCCESS(rc))
        {
            rc = ShClCacheSet(&pCtx->Wl.OtherCache, uCurFormat, pvData, cbData);
            RTMemFree(pvData);
        }
        else
            LogRel(("%s: Error reading %#x from the host: %Rrc\n", __func__, uCurFormat, rc));
        if (RT_FAILURE(rc))
        {
            pCtx->Wl.fOtherFormats = fFormats &= ~uCurFormat;
            LogRel(("%s: dropping format %#x, new mask %#x.\n", __func__, uCurFormat, fFormats));
        }

        RTCritSectLeave(&pCtx->Wl.CritSect);
    }

    /*
     * Kick of the --clipboard-set child.
     */
    /* Create pipe for reading content. */
    RTHANDLE hStdIn     = { RTHANDLETYPE_PIPE };
    RTHANDLE hPipeWrite = { RTHANDLETYPE_PIPE };
    int rc = RTPipeCreate(&hStdIn.u.hPipe, &hPipeWrite.u.hPipe, RTPIPE_C_INHERIT_READ);
    if (RT_SUCCESS(rc))
    {
        /* Create dummy pipe for signalling termination to poll(). */
        RTPIPE hPipeTermRead  = NIL_RTPIPE;
        RTPIPE hPipeWriteTerm = NIL_RTPIPE;
        rc = RTPipeCreate(&hPipeTermRead, &hPipeWriteTerm, RTPIPE_C_INHERIT_READ);
        if (RT_SUCCESS(rc))
        {
            /* Pass on the log verbosity level. */
            char szSetVerbosity[sizeof(SZ_OPT_VERBOSITY) + 64];
            RTStrPrintf(szSetVerbosity, sizeof(szSetVerbosity), SZ_OPT_VERBOSITY "=%u", g_cVerbosity);
            char szNotifyPipe[sizeof("--termination-pipe=10930940323467909656")];
            RTStrPrintf(szNotifyPipe, sizeof(szNotifyPipe), "--termination-pipe=%u", (uint32_t)RTPipeToNative(hPipeTermRead));
            const char *apszArgs[] =
            {
                RTProcExecutablePath(),
                "--clipboard-set",
                szSetVerbosity,
                szNotifyPipe,
                NULL
            };

            /* Create the process. */
            RTPROCESS hProcess = NIL_RTPROCESS;
            rc = RTProcCreateEx(apszArgs[0], apszArgs, RTENV_DEFAULT, 0 /*fFlags*/,
                                &hStdIn, NULL, NULL, NULL, NULL, NULL, &hProcess);
            if (RT_SUCCESS(rc))
            {
                /* close the child end of the pipes. */
                RTPipeClose(hPipeTermRead);
                hPipeTermRead = NIL_RTPIPE;
                RTPipeClose(hStdIn.u.hPipe);
                hStdIn.u.hPipe = NIL_RTPIPE;
                LogRel2(("Launched --clipboard-set process %u (%#x) ...\n", hProcess, hProcess));

                /* Feed the clipboard data to the child. */
                rc = VBClClipboardSerializeCache(&pCtx->Wl.OtherCache, fFormats, &hPipeWrite, RT_MS_30SEC);
                RTPipeClose(hPipeWrite.u.hPipe);
                hPipeWrite.u.hPipe = NIL_RTPIPE;
                if (RT_SUCCESS(rc))
                {
                    LogRel2(("Successfully transferred host clipboard data to --clipboard-set process %u (%#x).\n",
                             hProcess, hProcess));
                    pCtx->Wl.hProcClipboardSet = hProcess;
                    pCtx->Wl.hPipeClipboardSet = hPipeWriteTerm;
                }
                else
                {
                    /*
                     * Dang. Something went wrong transferring the clipboard data to the
                     * child process, so we have to terminate it and do zombie collecting.
                     */
                    VBClLogError("Terminating --clipboard-set child because VBClClipboardSerializeCache failed (%Rrc) ...\n", rc);
                    RTPipeClose(hPipeWriteTerm);
                    VbghWaylandPopupTerminateAndWaitForChild(hProcess, "--clipboard-set", RT_MS_1SEC, RT_MS_5SEC, 3, NULL);
                }
                hProcess       = NIL_RTPROCESS;
                hPipeWriteTerm = NIL_RTPIPE;
            }
            else
                VBClLogError("RTProcCreateEx/--clipboard-set failed: %Rrc\n", rc);
            RTPipeClose(hPipeTermRead);
            RTPipeClose(hPipeWriteTerm);
        }
        else
            VBClLogError("RTPipeCreate failed: %Rrc (--clipboard-set #2)\n", rc);
        RTPipeClose(hStdIn.u.hPipe);
        RTPipeClose(hPipeWrite.u.hPipe);
    }
    else
        VBClLogError("RTPipeCreate failed: %Rrc (--clipboard-set #1)\n", rc);

    /*
     * If there was a previous process, do zombie processing for it.
     */
    if (hProcPrev != NIL_RTPROCESS)
        VbghWaylandPopupTerminateAndWaitForChild(hProcPrev, "--clipboard-set", RT_MS_1SEC, RT_MS_5SEC, 3, NULL);

    return rc;
}

