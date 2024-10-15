/* $Id: tstClipboardGH-X11Smoke.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * Shared Clipboard guest/host X11 code smoke tests.
 */

/*
 * Copyright (C) 2011-2024 Oracle and/or its affiliates.
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

/* This is a simple test case that just starts a copy of the X11 clipboard
 * backend, checks the X11 clipboard and exits.  If ever needed I will add an
 * interactive mode in which the user can read and copy to the clipboard from
 * the command line. */

#include <iprt/assert.h>
#include <iprt/env.h>
#include <iprt/test.h>

#include <VBox/err.h>
#include <VBox/GuestHost/SharedClipboard.h>
#include <VBox/GuestHost/SharedClipboard-x11.h>
#include <VBox/GuestHost/clipboard-helper.h>


static DECLCALLBACK(int) tstShClReportFormatsCallback(PSHCLCONTEXT pCtx, uint32_t fFormats, void *pvUser)
{
    RT_NOREF(pCtx, fFormats, pvUser);
    return VINF_SUCCESS;
}

static DECLCALLBACK(int) tstShClOnRequestDataFromSourceCallback(PSHCLCONTEXT pCtx, SHCLFORMAT uFmt, void **ppv, uint32_t *pcb, void *pvUser)
{
    RT_NOREF(pCtx, uFmt, ppv, pcb, pvUser);
    return VERR_SHCLPB_NO_DATA;
}

static DECLCALLBACK(int) tstShClOnSendDataToDest(PSHCLCONTEXT pCtx, void *pv, uint32_t cb, void *pvUser)
{
    RT_NOREF(pCtx, pv, cb, pvUser);
    return VINF_SUCCESS;
}

int main()
{
    /*
     * Init the runtime, test and say hello.
     */
    RTTEST hTest;
    int rc = RTTestInitAndCreate("tstClipboardGH-X11Smoke", &hTest);
    if (rc)
        return rc;
    RTTestBanner(hTest);

    /*
     * Run the test.
     */
    rc = VINF_SUCCESS;
    /* We can't test anything without an X session, so just return success
     * in that case. */
    if (!RTEnvExist("DISPLAY"))
    {
        RTTestPrintf(hTest, RTTESTLVL_INFO,
                     "X11 not available, not running test\n");
        return RTTestSummaryAndDestroy(hTest);
    }

    SHCLCALLBACKS Callbacks;
    RT_ZERO(Callbacks);
    Callbacks.pfnReportFormats           = tstShClReportFormatsCallback;
    Callbacks.pfnOnRequestDataFromSource = tstShClOnRequestDataFromSourceCallback;
    Callbacks.pfnOnSendDataToDest        = tstShClOnSendDataToDest;

    SHCLX11CTX X11Ctx;
    RTTEST_CHECK_RC_OK(hTest, ShClX11Init(&X11Ctx, &Callbacks, NULL /* pParent */, false /* fHeadless */));
    RTTEST_CHECK_RC_OK(hTest, ShClX11ThreadStart(&X11Ctx, false /* fGrab */));

    /* Give the clipboard time to synchronise. */
    RTThreadSleep(500);

    RTTEST_CHECK_RC_OK(hTest, ShClX11ThreadStop(&X11Ctx));
    RTTEST_CHECK_RC_OK(hTest, ShClX11Destroy(&X11Ctx));

    return RTTestSummaryAndDestroy(hTest);
}

