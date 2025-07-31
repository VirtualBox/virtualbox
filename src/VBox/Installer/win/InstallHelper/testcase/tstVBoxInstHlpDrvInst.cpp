/* $Id: tstVBoxInstHlpDrvInst.cpp 110274 2025-07-16 18:22:54Z klaus.espenlaub@oracle.com $ */
/** @file
 * Testcase for VBoxInstallHelper.dll -- Performs driver (un)installation. See syntax help below.
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
#include <iprt/errcore.h>
#include <iprt/getopt.h>
#include <iprt/path.h>
#include <iprt/types.h>
#include <iprt/string.h>
#include <iprt/test.h>
#include <iprt/utf16.h>
#include <iprt/win/windows.h>

#include <VBox/GuestHost/VBoxWinDrvStore.h>

#include <msi.h>
#include <msiquery.h>

#include "../VBoxCommon.h"


UINT __stdcall DriverInstall(MSIHANDLE hModule);
UINT __stdcall DriverUninstall(MSIHANDLE hModule);

static char g_szInfFile[RTPATH_MAX] = { 0 };
static char g_szInfSection[64] = { 0 };
static char g_szModel[64] = { 0 };
static char g_szPnpId[64] = { 0 };
static DWORD g_dwFlagSilent = 1; /* Always try a silent installation here. */
static DWORD g_dwFlagForce = 1;  /* Always force installation here. */

/**
 * Structure to keeping a single MSI property name -> value translation.
 *
 * Needed for mocking the MSI properties.
 */
typedef struct VAL2PROPNAME
{
    char *pszPropName;
    void *pvVal;
} VAL2PROPNAME;

/* Overridden function to mock MSI properties. */
UINT VBoxMsiSetProp(MSIHANDLE, const WCHAR *, const WCHAR *)
{
    /* Not used here. */
    return 0;
}

/* Overridden function to mock MSI properties. */
int VBoxMsiQueryProp(MSIHANDLE hMsi, const WCHAR *pwszName, WCHAR *pwszVal, DWORD cwVal)
{
    RT_NOREF(hMsi);

    int rc = VERR_NOT_FOUND;

    /* Note: Property names *must* match the ones in the installer + VBoxInstallHelper.cpp code. */
    VAL2PROPNAME aProps[] =
    {
        /* Install */
        { "VBoxDrvInstInfFile",    { g_szInfFile } },
        { "VBoxDrvInstInfSection", { g_szInfSection } },
        { "VBoxDrvInstModel",      { g_szModel } },
        { "VBoxDrvInstPnpId",      { g_szPnpId } },
        /* Uninstall */
        { "VBoxDrvUninstInfFile",    { g_szInfFile } },
        { "VBoxDrvUninstInfSection", { g_szInfSection } },
        { "VBoxDrvUninstModel",      { g_szModel } },
        { "VBoxDrvUninstPnpId",      { g_szPnpId } }
    };

    RT_BZERO(pwszVal, cwVal * sizeof(WCHAR));

    for (size_t i = 0; i < RT_ELEMENTS(aProps); i++)
    {
        if (!RTUtf16NCmpUtf8(pwszName, aProps[i].pszPropName, strlen(aProps[i].pszPropName) * sizeof(RTUTF16),
                             strlen(aProps[i].pszPropName)))
        {
            PRTUTF16 pwszValue = NULL;
            if (strlen((const char *)aProps[i].pvVal)) /* Emty strings indicate "not found". */
                rc = RTStrToUtf16((const char *)aProps[i].pvVal, &pwszValue);
            if (RT_SUCCESS(rc))
            {
                rc = RTUtf16Copy(pwszVal, cwVal, pwszValue);
                RTUtf16Free(pwszValue);
                break;
            }
            break;
        }
    }

    return rc;
}

/* Overridden function to mock MSI properties. */
int VBoxMsiQueryPropInt32(MSIHANDLE hMsi, const char *pszName, DWORD *pdwValue)
{
    RT_NOREF(hMsi);

    int rc = VERR_NOT_FOUND;

    VAL2PROPNAME aProps[] =
    {
        /* Install */
        { "VBoxDrvInstFlagForce", &g_dwFlagForce },
        { "VBoxDrvInstFlagSilent", &g_dwFlagSilent },
        /* Uninstall */
        { "VBoxDrvUninstFlagForce", &g_dwFlagForce },
        { "VBoxDrvUninstFlagSilent", &g_dwFlagSilent }
    };

    *pdwValue = 0;

    for (size_t i = 0; i < RT_ELEMENTS(aProps); i++)
    {
        if (!RTStrCmp(pszName, aProps[i].pszPropName))
        {
            *pdwValue = *(DWORD *)aProps[i].pvVal;
            rc = VINF_SUCCESS;
            break;
        }
    }

    return rc;
}

static const RTGETOPTDEF g_aCmdCommonOptions[] =
{
    { "--inf-file",    'i', RTGETOPT_REQ_STRING  },
    { "--inf-section", 's', RTGETOPT_REQ_STRING  },
    { "--help",        'h', RTGETOPT_REQ_NOTHING },
    { "--model",       'm', RTGETOPT_REQ_STRING  },
    { "--pnp",         'p', RTGETOPT_REQ_STRING  },
    { "--pnpid" ,      'p', RTGETOPT_REQ_STRING  },
    { "--pnp-id",      'p', RTGETOPT_REQ_STRING  }
};

static void printParms(RTTEST hTest)
{
    RTTestPrintf(hTest, RTTESTLVL_ALWAYS, "INF File   : %s\n", g_szInfFile);
    RTTestPrintf(hTest, RTTESTLVL_ALWAYS, "INF Section: %s\n", g_szInfSection);
    RTTestPrintf(hTest, RTTESTLVL_ALWAYS, "Model      : %s\n", g_szModel);
    RTTestPrintf(hTest, RTTESTLVL_ALWAYS, "PnP ID     : %s\n", g_szPnpId);
}

static RTEXITCODE printHelp(RTTEST hTest, int argc, char **argv)
{
    RT_NOREF(argc);

    RTTestPrintf(hTest, RTTESTLVL_ALWAYS, "Syntax:\n\n");
    RTTestPrintf(hTest, RTTESTLVL_ALWAYS, "%s install     --inf-file <INF-File>\n", argv[0]);
    RTTestPrintf(hTest, RTTESTLVL_ALWAYS, "%s install     --inf-file <INF File> [--inf-section <Section>]\n", argv[0]);
    RTTestPrintf(hTest, RTTESTLVL_ALWAYS, "%s uninstall   --inf-file <INF File> [--pnp-id <PnP ID>]\n", argv[0]);
    RTTestPrintf(hTest, RTTESTLVL_ALWAYS, "%s list        [exact name|pattern]\n\n", argv[0]);
    RTTestPrintf(hTest, RTTESTLVL_ALWAYS, "Examples:\n");
    RTTestPrintf(hTest, RTTESTLVL_ALWAYS, "\t%s install   --inf-file C:\\Path\\To\\VBoxUSB.inf\n", argv[0]);
    RTTestPrintf(hTest, RTTESTLVL_ALWAYS, "\t%s uninstall --inf -file C:\\Path\\To\\VBoxUSB.inf --pnp-id \"USB\\VID_80EE&PID_CAFE\"\n", argv[0]);
    RTTestPrintf(hTest, RTTESTLVL_ALWAYS, "\t%s uninstall --model \"VBoxUSB.AMD64\"\n", argv[0]);
    RTTestPrintf(hTest, RTTESTLVL_ALWAYS, "\t%s uninstall --model \"VBoxUSB*\"\n", argv[0]);
    RTTestPrintf(hTest, RTTESTLVL_ALWAYS, "\t%s list      \"VBox*\"", argv[0]);

    return RTEXITCODE_SUCCESS; /* Don't report any error here to not upset the testboxes. */
}

static RTEXITCODE printDrivers(RTTEST hTest, int argc, char **argv)
{
    PVBOXWINDRVSTORE pStore;
    RTTEST_CHECK_RC_OK_RET(hTest, VBoxWinDrvStoreCreate(&pStore), RTEXITCODE_FAILURE);

    const char *pszPattern = NULL;
    if (argc >= 3)
        pszPattern = argv[2]; /** @todo Use pattern to filter entries, e.g. "pnp:<PNP-ID>" or "model:VBoxSup*". */

    int rc;
    PVBOXWINDRVSTORELIST pList = NULL;
    if (pszPattern)
        rc = VBoxWinDrvStoreQueryAny(pStore, pszPattern, &pList);
    else
        rc = VBoxWinDrvStoreQueryAll(pStore, &pList);
    RTTEST_CHECK_RC_OK(hTest, rc);
    if (RT_SUCCESS(rc))
    {
        RTTestPrintf(hTest, RTTESTLVL_ALWAYS, "Location: %s\n\n", VBoxWinDrvStoreBackendGetLocation(pStore));

        RTTestPrintf(hTest, RTTESTLVL_ALWAYS, "%-12s | %-28s | %-24s | %-16s\n", "OEM INF File", "Model (First)", "PnP ID (First)", "Version");
        RTTestPrintf(hTest, RTTESTLVL_ALWAYS, "--------------------------------------------------------------------------------\n");

        size_t cEntries = 0;
        PVBOXWINDRVSTOREENTRY pCur;
        RTListForEach(&pList->List, pCur, VBOXWINDRVSTOREENTRY, Node)
        {
            RTTestPrintf(hTest, RTTESTLVL_ALWAYS, "%-12ls | %-28ls | %-24ls | %-16ls\n",
                         pCur->wszInfFile, pCur->wszModel, pCur->wszPnpId, pCur->Ver.wszDriverVer);
            cEntries++;
        }

        if (pszPattern)
            RTTestPrintf(hTest, RTTESTLVL_ALWAYS, "\nFound %zu entries (filtered).\n", cEntries);
        else
            RTTestPrintf(hTest, RTTESTLVL_ALWAYS, "\nFound %zu entries.\n", cEntries);
    }

    VBoxWinDrvStoreListFree(pList);

    VBoxWinDrvStoreDestroy(pStore);
    pStore = NULL;

    RTTestPrintf(hTest, RTTESTLVL_ALWAYS, "\nUse DOS-style wildcards to adjust results.\n");
    RTTestPrintf(hTest, RTTESTLVL_ALWAYS, "Use \"--help\" to print syntax help.\n");

    return RTTestErrorCount(hTest) == 0 ? RTEXITCODE_SUCCESS : RTEXITCODE_FAILURE;
}

int main(int argc, char **argv)
{
    RTTEST hTest;
    RTEXITCODE rcExit = RTTestInitAndCreate("tstVBoxInstHlpDrvInst", &hTest);
    if (rcExit != RTEXITCODE_SUCCESS)
        return rcExit;
    RTTestBanner(hTest);

    RTGETOPTSTATE GetState;
    RT_ZERO(GetState);
    int rc = RTGetOptInit(&GetState, argc, argv, g_aCmdCommonOptions, RT_ELEMENTS(g_aCmdCommonOptions),
                          1 /*idxFirst*/, RTGETOPTINIT_FLAGS_OPTS_FIRST);
    AssertRCReturn(rc, RTEXITCODE_INIT);

    int           ch;
    RTGETOPTUNION ValueUnion;
    while ((ch = RTGetOpt(&GetState, &ValueUnion)) != 0)
    {
        switch (ch)
        {
            case 'i':
                RTStrCopy(g_szInfFile, sizeof(g_szInfFile), ValueUnion.psz);
                break;

            case 'm':
                RTStrCopy(g_szModel, sizeof(g_szModel), ValueUnion.psz);
                break;

            case 'p':
                RTStrCopy(g_szPnpId, sizeof(g_szPnpId), ValueUnion.psz);
                break;

            case 's':
                RTStrCopy(g_szInfSection, sizeof(g_szInfSection), ValueUnion.psz);
                break;

            case 'h':
                return printHelp(hTest, argc, argv);

            case VERR_GETOPT_UNKNOWN_OPTION:
                return printHelp(hTest, argc, argv);

            case VINF_GETOPT_NOT_OPTION:
            {
                if (!RTStrICmp(ValueUnion.psz, "install"))
                {
                    printParms(hTest);
                    /* rc ignored */ DriverInstall(NULL /* hModule */);
                    return RTEXITCODE_SUCCESS;
                }
                else if (!RTStrICmp(ValueUnion.psz, "uninstall"))
                {
                    printParms(hTest);
                    /* rc ignored */ DriverUninstall(NULL /* hModule */);
                    return RTEXITCODE_SUCCESS;
                }
                else if (!RTStrICmp(ValueUnion.psz, "list"))
                {
                    return printDrivers(hTest, argc, argv);
                }
                break;
            }

            default:
                break;
        }
    }

    /* List all Windows driver store entries if no command is given. */
    printDrivers(hTest, argc, argv);

    /*
     * Summary.
     */
    return RTTestSummaryAndDestroy(hTest);
}

