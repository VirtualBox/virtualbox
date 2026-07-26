/* $Id: tstClipboardMimeConv.cpp 114778 2026-07-26 00:59:02Z knut.osmundsen@oracle.com $ */
/** @file
 * Shared Clipboard MIME converter testcase.
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

#include <VBox/GuestHost/SharedClipboard.h>
#include <VBox/GuestHost/mime-type-converter.h>

#include <iprt/errcore.h>
#include <iprt/mem.h>
#include <iprt/string.h>
#include <iprt/test.h>

#include <string.h>


/** MIME enumeration test state. */
typedef struct TSTMIMECONVENUMCTX
{
    /** Whether text/uri-list was seen. */
    bool fFoundUriList;
    /** Flags and priority reported for text/uri-list. */
    uint32_t fFlagsAndPriority;
} TSTMIMECONVENUMCTX;
/** Pointer to a MIME enumeration test state. */
typedef TSTMIMECONVENUMCTX *PTSTMIMECONVENUMCTX;


/**
 * MIME enumeration callback used by testUriListMapping().
 *
 * @param   pcszMimeType        MIME type being enumerated.
 * @param   fFlagsAndPriority   MIME converter flags and priority.
 * @param   pvUser              Pointer to TSTMIMECONVENUMCTX.
 */
static DECLCALLBACK(void) tstMimeConvEnumCallback(const char *pcszMimeType, uint32_t fFlagsAndPriority, void *pvUser)
{
    PTSTMIMECONVENUMCTX pCtx = (PTSTMIMECONVENUMCTX)pvUser;
    if (RTStrCmp(pcszMimeType, "text/uri-list") == 0)
    {
        pCtx->fFoundUriList = true;
        pCtx->fFlagsAndPriority = fFlagsAndPriority;
    }
}


/**
 * Tests the transfer URI-list MIME mapping and copy conversion.
 */
static void testUriListMapping(void)
{
    RTTestISub("Testing text/uri-list mapping");

    uint32_t fFlagsAndPriority = 0;
    const char *pcszPersistentMimeType = NULL;
    SHCLFORMAT const uFmt = VbghMimeConvGetVBoxFormatByMime("text/uri-list", &fFlagsAndPriority,
                                                            &pcszPersistentMimeType);
    RTTESTI_CHECK_MSG(uFmt == VBOX_SHCL_FMT_URI_LIST, ("uFmt=%#x\n", uFmt));
    RTTESTI_CHECK_MSG(pcszPersistentMimeType != NULL, ("pcszPersistentMimeType=NULL\n"));
    if (pcszPersistentMimeType)
        RTTESTI_CHECK_MSG(RTStrCmp(pcszPersistentMimeType, "text/uri-list") == 0,
                          ("pcszPersistentMimeType=%s\n", pcszPersistentMimeType));
    RTTESTI_CHECK_MSG((fFlagsAndPriority & VBGH_MIME_CONV_F_PRIORITY_MASK) != 0,
                      ("fFlagsAndPriority=%#x\n", fFlagsAndPriority));

    TSTMIMECONVENUMCTX EnumCtx;
    RT_ZERO(EnumCtx);
    VbghMimeConvEnumerateByVBoxFormats(VBOX_SHCL_FMT_URI_LIST, tstMimeConvEnumCallback, &EnumCtx);
    RTTESTI_CHECK_MSG(EnumCtx.fFoundUriList, ("text/uri-list was not enumerated\n"));

    char szUriList[] = "file:///tmp/vbox-shcl-a\r\nfile:///tmp/vbox-shcl-b\r\n";
    void *pvOut = NULL;
    size_t cbOut = 0;
    int rc = VbghMimeConvToVBox("text/uri-list", szUriList, (int)strlen(szUriList), &pvOut, &cbOut);
    RTTESTI_CHECK_RC_OK(rc);
    if (RT_SUCCESS(rc))
    {
        RTTESTI_CHECK_MSG(cbOut == strlen(szUriList), ("cbOut=%zu expected=%zu\n", cbOut, strlen(szUriList)));
        RTTESTI_CHECK_MSG(memcmp(pvOut, szUriList, cbOut) == 0, ("URI-list ToVBox data changed\n"));
        RTMemFree(pvOut);
    }

    pvOut = NULL;
    cbOut = 0;
    rc = VbghMimeConvFromVBox("text/uri-list", szUriList, (int)strlen(szUriList), &pvOut, &cbOut);
    RTTESTI_CHECK_RC_OK(rc);
    if (RT_SUCCESS(rc))
    {
        RTTESTI_CHECK_MSG(cbOut == strlen(szUriList), ("cbOut=%zu expected=%zu\n", cbOut, strlen(szUriList)));
        RTTESTI_CHECK_MSG(memcmp(pvOut, szUriList, cbOut) == 0, ("URI-list FromVBox data changed\n"));
        RTMemFree(pvOut);
    }

    uint8_t abInvalid[] = { 'f', 'i', 'l', 'e', ':', '/', '/', 0xff, 0x00 };
    pvOut = NULL;
    cbOut = 0;
    rc = VbghMimeConvToVBox("text/uri-list", abInvalid, sizeof(abInvalid), &pvOut, &cbOut);
    RTTESTI_CHECK_MSG(RT_FAILURE(rc), ("invalid UTF-8 URI-list accepted by ToVBox: rc=%Rrc\n", rc));
    if (pvOut)
        RTMemFree(pvOut);

    pvOut = NULL;
    cbOut = 0;
    rc = VbghMimeConvFromVBox("text/uri-list", abInvalid, sizeof(abInvalid), &pvOut, &cbOut);
    RTTESTI_CHECK_MSG(RT_FAILURE(rc), ("invalid UTF-8 URI-list accepted by FromVBox: rc=%Rrc\n", rc));
    if (pvOut)
        RTMemFree(pvOut);
}


/**
 * Testcase entry point.
 */
int main(int argc, char **argv)
{
    RT_NOREF(argc, argv);

    RTTEST hTest;
    RTEXITCODE rcExit = RTTestInitAndCreate("tstClipboardMimeConv", &hTest);
    if (rcExit != RTEXITCODE_SUCCESS)
        return rcExit;

    RTTestBanner(hTest);

    testUriListMapping();

    return RTTestSummaryAndDestroy(hTest);
}
