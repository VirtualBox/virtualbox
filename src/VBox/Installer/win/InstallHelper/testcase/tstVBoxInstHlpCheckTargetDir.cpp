/* $Id: tstVBoxInstHlpCheckTargetDir.cpp 110274 2025-07-16 18:22:54Z klaus.espenlaub@oracle.com $ */
/** @file
 * Testcase for VBoxInstallHelper.dll -- CheckTargetDir functionality.
 */

/*
 * Copyright (C) 2024 Oracle and/or its affiliates.
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

#include <iprt/cpp/ministring.h> /* For replacement fun. */
#include <iprt/env.h>
#include <iprt/err.h>
#include <iprt/path.h>
#include <iprt/types.h>
#include <iprt/string.h>
#include <iprt/test.h>
#include <iprt/utf16.h>
#include <iprt/win/windows.h>

#include <msi.h>
#include <msiquery.h>

#include "../VBoxCommon.h"

UINT __stdcall CheckTargetDir(MSIHANDLE hModule);

static struct
{
    /** Desired target directory to check. Absolute path. */
    const char *pszTargetDir;
    /** Expected result. */
    int         rc;
} g_aTests[] =
{
    /* Invalid stuff. */
    { "$SystemDrive$\\", VERR_INVALID_NAME },
    { "$SystemDrive$\\..\\", VERR_INVALID_NAME },
    { "$ProgramData$", VERR_INVALID_NAME },
    { "$ProgramFiles$\\DoesNotExist\\..", VERR_INVALID_NAME },
    /* Valid stuff. */
    { "$HOMEDRIVE$\\Users", VINF_SUCCESS },
    { "$ProgramFiles$\\DoesNotExist", VINF_SUCCESS },
    { "$ProgramFiles$\\Oracle\\VirtualBox", VINF_SUCCESS }, /* Might not exist yet. */
    { "$ProgramFiles(x86)$", VINF_SUCCESS },
    { "$SystemRoot$", VINF_SUCCESS },
    { "C:\\Program Files\\Oracle\\VirtualBox\\", VINF_SUCCESS }
};
static size_t g_idxTest = 0; /* Current index of test running. */

static int expandStringWithEnvVar(RTCString &strVal, const char *pszTemplate, const char *pszEnvVar)
{
    int rc = VINF_SUCCESS;
    size_t const cchOff = strVal.find(pszTemplate);
    if (cchOff != RTCString::npos)
        rc = strVal.replaceNoThrow(cchOff, strlen(pszTemplate), RTEnvGet(pszEnvVar));
    return rc;
}

/* Overridden function to mock MSI properties. */
int VBoxMsiQueryProp(MSIHANDLE hMsi, const WCHAR *pwszName, WCHAR *pwszVal, DWORD cwVal)
{
    RT_NOREF(hMsi);

    int rc = VINF_SUCCESS;

    RT_BZERO(pwszVal, cwVal * sizeof(WCHAR));

    if (!RTUtf16NCmpUtf8(pwszName, "INSTALLDIR", strlen("INSTALLDIR") * sizeof(RTUTF16), strlen("INSTALLDIR")))
    {
        RTCString strVal = g_aTests[g_idxTest].pszTargetDir;

#define EXPAND_STR(a_Str) \
        expandStringWithEnvVar(strVal, "$" ## a_Str "$", a_Str);

        /* For simplicity we only replace the first occurrence found. Alphabetically sorted.
         * ASSUMES a recent Windows version with a default environment block. */
        EXPAND_STR("HOMEDRIVE");
        EXPAND_STR("HOMEPATH");
        EXPAND_STR("ProgramData");
        EXPAND_STR("ProgramFiles");
        EXPAND_STR("ProgramFiles(x86)");
        EXPAND_STR("SystemDrive");
        EXPAND_STR("SystemRoot");
        EXPAND_STR("TEMP");

#undef EXPAND_STR

        PRTUTF16 pwszValue;
        rc = RTStrToUtf16(strVal.c_str(), &pwszValue);
        if (RT_SUCCESS(rc))
        {
            rc = RTUtf16Copy(pwszVal, cwVal, pwszValue);
            RTUtf16Free(pwszValue);
        }
    }

    return rc;
}

/* Overridden function to mock MSI properties. Unused. */
int VBoxMsiQueryPropInt32(MSIHANDLE, const char *, DWORD *)
{
    return VERR_NOT_IMPLEMENTED;
}

/* Overridden function to mock MSI properties. */
UINT VBoxMsiSetProp(MSIHANDLE hMsi, const WCHAR *pwszName, const WCHAR *pwszValue)
{
    RT_NOREF(hMsi);

    if (!RTUtf16NCmpUtf8(pwszName, "VBox_Target_Dir_Is_Valid",  RTPATH_MAX, RT_ELEMENTS("VBox_Target_Dir_Is_Valid")))
    {
        int const rc = RTUtf16NCmpUtf8(pwszValue, "1",  RTPATH_MAX, RT_ELEMENTS("1")) == 0 ? VINF_SUCCESS : VERR_INVALID_NAME;
        RTTESTI_CHECK_MSG(rc == g_aTests[g_idxTest].rc, ("Test %#d failed: Expected %Rrc, got %Rrc\n",
                                                         g_idxTest, g_aTests[g_idxTest].rc, rc));
    }

    return 0;
}

int main(void)
{
    RTTEST hTest;
    RTEXITCODE rcExit = RTTestInitAndCreate("tstVBoxInstHlpCheckTargetDir", &hTest);
    if (rcExit != RTEXITCODE_SUCCESS)
        return rcExit;
    RTTestBanner(hTest);

    for (size_t i = 0; i < RT_ELEMENTS(g_aTests); i++)
    {
        g_idxTest = i;
        /* rc ignored */ CheckTargetDir(NULL /* hModule */);
        if (RTTestIErrorCount())
            break;
    }

    /*
     * Summary.
     */
    return RTTestSummaryAndDestroy(hTest);
}

