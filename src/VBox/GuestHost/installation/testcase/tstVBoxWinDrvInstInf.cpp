/* $Id: tstVBoxWinDrvInstInf.cpp 107177 2024-11-26 12:34:25Z andreas.loeffler@oracle.com $ */
/** @file
 * VirtualBox Windows driver installation tests.
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

#include <iprt/assert.h>
#include <iprt/dir.h>
#include <iprt/err.h>
#include <iprt/path.h>
#include <iprt/process.h>
#include <iprt/test.h>

#include <VBox/err.h>
#include <VBox/log.h>

#include <VBox/GuestHost/VBoxWinDrvInst.h>
#include <VBox/GuestHost/VBoxWinDrvStore.h>

#include "VBoxWinDrvInstInternal.h"


/**
 * Test context to use.
 */
typedef struct VBOXWINDRVINSTTESTCTX
{
    /** Test handle. */
    RTTEST         hTest;
    /** Installer handle. */
    VBOXWINDRVINST hInst;
    /** Index of current test running.
     *  Set to UINT32_MAX if no test running (yet). */
    size_t         idxTest;
} VBOXWINDRVINSTTESTCTX;
typedef VBOXWINDRVINSTTESTCTX *PVBOXWINDRVINSTTESTCTX;

/**
 * Expected results for a single test.
 */
typedef struct VBOXWINDRVINSTTESTRES
{
    /** Number of errors occurred for this test. */
    unsigned cErrors;
} VBOXWINDRVINSTTESTRES;

/**
 * Parameters for a single test.
 */
typedef struct VBOXWINDRVINSTTESTPARMS
{
    /** Expected section to (un)install.
     *  NULL if not being used. */
    const char           *pszSection;
    /** Expected model name.
     *  NULL if not being used. */
    const char           *pszModel;
    /** Expected PnP ID.
     *  NULL if not being used. */
    const char           *pszPnpId;
} VBOXWINDRVINSTTESTPARMS;

/**
 * A single test.
 */
typedef struct VBOXWINDRVINSTTEST
{
    /** INF file to use for testing. */
    const char             *pszFile;
    /** (Un)installation flags. */
    uint32_t                fFlags;
    /** Expected overall result of the test. */
    int                     rc;
    /** Test parameters. */
    VBOXWINDRVINSTTESTPARMS Parms;
    /** Test result(s). Must come last due to array initialization. */
    VBOXWINDRVINSTTESTRES   Result;
} VBOXWINDRVINSTTEST;
typedef VBOXWINDRVINSTTEST *PVBOXWINDRVINSTTEST;

/**
 * Test definitions.
 */
VBOXWINDRVINSTTEST g_aTests[] =
{
    /**
     * Failing tests.
     */
    /** File does not exist. */
    { "testInstallFileDoesNotExist.inf", VBOX_WIN_DRIVERINSTALL_F_NONE, VERR_FILE_NOT_FOUND },
    /** File exists but empty. */
    { "testInstallEmpty.inf", VBOX_WIN_DRIVERINSTALL_F_NONE, VERR_INSTALLATION_FAILED /* ERROR_GENERAL_SYNTAX */ },
    /** Does not have an Version section and/or signature. */
    { "testInstallWrongInfStyle.inf", VBOX_WIN_DRIVERINSTALL_F_NONE, VERR_INSTALLATION_FAILED /* ERROR_WRONG_INF_STYLE */ },
    /** Does neither have a DefaultInstall nor a Manufacturer section. */
    { "testInstallNoManufacturerOrDefaultInstall.inf", VBOX_WIN_DRIVERINSTALL_F_NONE, VERR_INVALID_PARAMETER /* ERROR_WRONG_INF_STYLE */ },
    /** Both, a Manufacturer section *and* a DefaultInstall section are present, which is invalid. */
    { "testInstallManufacturerAndDefaultInstall.inf", VBOX_WIN_DRIVERINSTALL_F_NONE, VERR_INVALID_PARAMETER /* ERROR_WRONG_INF_STYLE */ },
    /** Manufacturer section is present, but no model section. */
    { "testInstallManufacturerButNoModelSection.inf", VBOX_WIN_DRIVERINSTALL_F_NONE, VERR_NOT_FOUND },
    /** Manufacturer section + model present, but bogus / invalid architecture. */
    { "testInstallManufacturerInvalidArch.inf", VBOX_WIN_DRIVERINSTALL_F_NONE, VERR_PLATFORM_ARCH_NOT_SUPPORTED },
    /**
     * Succeeding tests.
     */
    /** Default section is present, but no model section.
     *  This actually is valid and has to succeed. */
    { "testInstallDefaultInstallButNoModelSection.inf", VBOX_WIN_DRIVERINSTALL_F_NONE, VINF_SUCCESS },
    /** Manufacturer, model and section given. */
    { "testInstallManufacturerWithModelSection.inf", VBOX_WIN_DRIVERINSTALL_F_NONE,
      VINF_SUCCESS, { "VBoxTest" /* Section */, "VBoxTest.NTAMD64" /* Model */, "PCI\\VEN_80ee&DEV_cafe" /* PnP ID */, } }
};
typedef VBOXWINDRVINSTTEST *PVBOXWINDRVINSTTEST;

/**
 * Logging callback for the Windows driver (un)installation code.
 */
static DECLCALLBACK(void) tstVBoxDrvInstLogCallback(VBOXWINDRIVERLOGTYPE enmType, const char *pszMsg, void *pvUser)
{
    PVBOXWINDRVINSTTESTCTX pCtx = (PVBOXWINDRVINSTTESTCTX)pvUser;

    RTTestPrintf(pCtx->hTest, RTTESTLVL_ALWAYS, "%s\n", pszMsg);

    switch (enmType)
    {
        case VBOXWINDRIVERLOGTYPE_ERROR:
        {
            if (pCtx->idxTest == UINT32_MAX) /* No test defined? Must be an error in the actual framework. */
                RTTestFailed(pCtx->hTest, "%s", pszMsg);
            else /* Just count the error here. If that actually is a not expected error is up to the test later then. */
                g_aTests[pCtx->idxTest].Result.cErrors++;
            break;
        }

        default:
            break;
    }
}

static int tstVBoxDrvInstTestOne(PVBOXWINDRVINSTTESTCTX pCtx, const char *pszInfFile, PVBOXWINDRVINSTTEST pTest)
{
    pTest->fFlags |= /* Only run in dry-mode. */
                     VBOX_WIN_DRIVERINSTALL_F_DRYRUN
                     /* Don't destroy the (un)installation parameters for later inspection (see below). */
                   | VBOX_WIN_DRIVERINSTALL_F_NO_DESTROY;

    int rc = VBoxWinDrvInstInstall(pCtx->hInst, pszInfFile, pTest->fFlags);
    RTTEST_CHECK_MSG_RET(pCtx->hTest, rc == pTest->rc, (pCtx->hTest, "Error: Got %Rrc, expected %Rrc\n", rc, pTest->rc), rc);
    if (RT_FAILURE(rc)) /* Nothing to do here anymore. */
        return VINF_SUCCESS;

    PVBOXWINDRVINSTPARMS const pParms = VBoxWinDrvInstTestGetParms(pCtx->hInst);

    /* Check section. */
    char *psz;
    if (pTest->Parms.pszSection)
    {
        RTUtf16ToUtf8(pParms->u.UnInstall.pwszSection, &psz);
        if (RTStrCmp(pTest->Parms.pszSection, psz))
            RTTestFailed(pCtx->hTest, "Error: Got section %s, expected %s\n", psz, pTest->Parms.pszSection);
        RTStrFree(psz);
    }

    /* Check model. */
    if (pTest->Parms.pszModel)
    {
        RTUtf16ToUtf8(pParms->u.UnInstall.pwszModel, &psz);
        if (RTStrCmp(pTest->Parms.pszModel, psz))
            RTTestFailed(pCtx->hTest, "Error: Got model %s, expected %s\n", psz, pTest->Parms.pszModel);
        RTStrFree(psz);
    }

    /* Check PnP ID. */
    if (pTest->Parms.pszPnpId)
    {
        RTUtf16ToUtf8(pParms->u.UnInstall.pwszPnpId, &psz);
        if (RTStrCmp(pTest->Parms.pszPnpId, psz))
            RTTestFailed(pCtx->hTest, "Error: Got PnP ID %s, expected %s\n", psz, pTest->Parms.pszPnpId);
        RTStrFree(psz);
    }

    VBoxWinDrvInstTestParmsDestroy(pParms); /* For VBOX_WIN_DRIVERINSTALL_F_NO_DESTROY. */

    return VINF_SUCCESS;
}

static int tstVBoxDrvInstTestPath(PVBOXWINDRVINSTTESTCTX pCtx, const char *pszPath)
{
    for (size_t i = 0; i < RT_ELEMENTS(g_aTests); i++)
    {
        pCtx->idxTest = i; /* Set current test index for callback. */

        char szInfPath[RTPATH_MAX];
        RTTEST_CHECK(pCtx->hTest, RTStrPrintf(szInfPath, sizeof(szInfPath), "%s", pszPath) > 0);
        RTTEST_CHECK_RC_OK(pCtx->hTest, RTPathAppend(szInfPath, sizeof(szInfPath), g_aTests[i].pszFile));
        RTTEST_CHECK_RC_OK(pCtx->hTest, tstVBoxDrvInstTestOne(pCtx, szInfPath, &g_aTests[i]));
    }

    return VINF_SUCCESS;
}

int main(int argc, char **argv)
{
    /*
     * Init the runtime, test and say hello.
     */
    RTTEST hTest;
    int rc = RTTestInitAndCreate("tstVBoxWinDrvInstInf", &hTest);
    if (rc)
        return rc;
    RTTestBanner(hTest);

    /* Simply create + destroy. */
    VBOXWINDRVINST hWinDrvInst;
    RTTEST_CHECK_RC_OK_BREAK(hTest, VBoxWinDrvInstCreateEx(&hWinDrvInst, 4 /* Verbosity */,
                                                           NULL, NULL /* pvUser */));
    VBoxWinDrvInstDestroy(hWinDrvInst);

    /* Run all tests. */
    do
    {
        char szTestPath[RTPATH_MAX];

        if (argc < 2)
        {
            RTTEST_CHECK_BREAK(hTest, RTProcGetExecutablePath(szTestPath, sizeof(szTestPath)) != NULL);
            RTPathStripFilename(szTestPath);
            RTTEST_CHECK_RC_OK_BREAK(hTest, RTPathAppend(szTestPath, sizeof(szTestPath), "inf"));
        }
        else
        {
            RTTestPrintf(hTest, RTTESTLVL_ALWAYS, "Warning: Custom directory specified\n");
            RTTEST_CHECK_BREAK(hTest, RTStrPrintf(szTestPath, sizeof(szTestPath), "%s", argv[1]) > 0);
        }

        RTTestPrintf(hTest, RTTESTLVL_ALWAYS, "Test directory: %s\n", szTestPath);

        VBOXWINDRVINSTTESTCTX Ctx = { 0 };
        Ctx.idxTest = UINT32_MAX;

        RTTEST_CHECK_RC_OK_BREAK(hTest, VBoxWinDrvInstCreateEx(&hWinDrvInst, 4 /* Verbosity */,
                                                               &tstVBoxDrvInstLogCallback, &Ctx /* pvUser */));
        Ctx.hInst   = hWinDrvInst;

        rc = tstVBoxDrvInstTestPath(&Ctx, szTestPath);

        VBoxWinDrvInstDestroy(hWinDrvInst);

    } while (0);

    /*
     * Run the test.
     */
    return RTTestSummaryAndDestroy(hTest);
}

