/* $Id: tstUsbMouse.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * tstUsbMouse.cpp - testcase USB mouse and tablet devices.
 */

/*
 * Copyright (C) 2013-2024 Oracle and/or its affiliates.
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
#include "VBoxDD.h"
#include <VBox/vmm/pdmdrv.h>
#include <iprt/mem.h>
#include <iprt/rand.h>
#include <iprt/stream.h>
#include <iprt/test.h>
#include <iprt/uuid.h>


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/
/** Test mouse driver structure. */
typedef struct DRVTSTMOUSE
{
    /** The USBHID structure. */
    struct USBHID              *pUsbHid;
    /** The base interface for the mouse driver. */
    PDMIBASE                    IBase;
    /** Our mouse connector interface. */
    PDMIMOUSECONNECTOR          IConnector;
    /** The base interface of the attached mouse port. */
    PPDMIBASE                   pDrvBase;
    /** The mouse port interface of the attached mouse port. */
    PPDMIMOUSEPORT              pDrv;
    /** Is relative mode currently supported? */
    bool                        fRel;
    /** Is absolute mode currently supported? */
    bool                        fAbs;
    /** Is absolute multi-touch mode currently supported? */
    bool                        fMTAbs;
    /** Is relative multi-touch mode currently supported? */
    bool                        fMTRel;
} DRVTSTMOUSE;
typedef DRVTSTMOUSE *PDRVTSTMOUSE;


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
static PDMUSBHLP   g_tstUsbHlp;
/** Global mouse driver variable.
 * @todo To be improved some time. */
static DRVTSTMOUSE g_drvTstMouse;


/** @interface_method_impl{PDMUSBHLPR3,pfnVMSetErrorV} */
static DECLCALLBACK(int) tstVMSetErrorV(PPDMUSBINS pUsbIns, int rc,
                                        RT_SRC_POS_DECL, const char *pszFormat,
                                        va_list va)
{
    RT_NOREF(pUsbIns);
    RTPrintf("Error: %s:%u:%s:", RT_SRC_POS_ARGS);
    RTPrintfV(pszFormat, va);
    return rc;
}

/** @interface_method_impl{PDMUSBHLPR3,pfnDriverAttach} */
/** @todo We currently just take the driver interface from the global
 * variable.  This is sufficient for a unit test but still a bit sad. */
static DECLCALLBACK(int) tstDriverAttach(PPDMUSBINS pUsbIns, RTUINT iLun, PPDMIBASE pBaseInterface,
                                         PPDMIBASE *ppBaseInterface, const char *pszDesc)
{
    RT_NOREF3(pUsbIns, iLun, pszDesc);
    g_drvTstMouse.pDrvBase = pBaseInterface;
    g_drvTstMouse.pDrv = PDMIBASE_QUERY_INTERFACE(pBaseInterface, PDMIMOUSEPORT);
    *ppBaseInterface = &g_drvTstMouse.IBase;
    return VINF_SUCCESS;
}


/** @interface_method_impl{PDMUSBHLP,pfnTimerCreate} */
static DECLCALLBACK(int) tstTimerCreate(PPDMUSBINS pUsbIns, TMCLOCK enmClock, PFNTMTIMERUSB pfnCallback, void *pvUser,
                                        uint32_t fFlags, const char *pszDesc, PTMTIMERHANDLE phTimer)
{
    RT_NOREF7(pUsbIns, enmClock, pfnCallback, pvUser, fFlags, pszDesc, phTimer);
    return VINF_SUCCESS;
}


/** @interface_method_impl{PDMUSBHLP,pfnTimerCreate} */
static DECLCALLBACK(int) tstTimerDestroy(PPDMUSBINS pUsbIns, TMTIMERHANDLE hTimer)
{
    RT_NOREF2(pUsbIns, hTimer);
    return VINF_SUCCESS;
}


/**
 * @interface_method_impl{PDMIBASE,pfnQueryInterface}
 */
static DECLCALLBACK(void *) tstMouseQueryInterface(PPDMIBASE pInterface,
                                                   const char *pszIID)
{
    PDRVTSTMOUSE pUsbIns = RT_FROM_MEMBER(pInterface, DRVTSTMOUSE, IBase);
    PDMIBASE_RETURN_INTERFACE(pszIID, PDMIBASE, &pUsbIns->IBase);
    PDMIBASE_RETURN_INTERFACE(pszIID, PDMIMOUSECONNECTOR, &pUsbIns->IConnector);
    return NULL;
}


/**
 * @interface_method_impl{PDMIMOUSECONNECTOR,pfnReportModes}
 */
static DECLCALLBACK(void) tstMouseReportModes(PPDMIMOUSECONNECTOR pInterface,
                                              bool fRel, bool fAbs,
                                              bool fMTAbs, bool fMTRel)
{
    PDRVTSTMOUSE pDrv = RT_FROM_MEMBER(pInterface, DRVTSTMOUSE, IConnector);
    pDrv->fRel = fRel;
    pDrv->fAbs = fAbs;
    pDrv->fMTAbs  = fMTAbs;
    pDrv->fMTRel  = fMTRel;
}


static int tstMouseConstruct(RTTEST hTest, int iInstance, const char *pcszMode,
                             uint8_t u8CoordShift, PPDMUSBINS *ppThis,
                             uint32_t uInstanceVersion = PDM_USBINS_VERSION)
{
    size_t cbUsbIns = RT_UOFFSETOF(PDMUSBINS, achInstanceData) + g_UsbHidMou.cbInstance;
    PPDMUSBINS pUsbIns;
    int rc = RTTestGuardedAlloc(hTest, cbUsbIns, 1, RTRandU32Ex(0, 1) != 0 /*fHead*/, (void **)&pUsbIns);
    if (RT_SUCCESS(rc))
    {
        RT_BZERO(pUsbIns, cbUsbIns);

        PCFGMNODE pCfg = CFGMR3CreateTree(NULL);
        if (pCfg)
        {
            rc = CFGMR3InsertString(pCfg, "Mode", pcszMode);
            if (RT_SUCCESS(rc))
                rc = CFGMR3InsertInteger(pCfg, "CoordShift", u8CoordShift);
            if (RT_SUCCESS(rc))
            {
                g_drvTstMouse.pDrv     = NULL;
                g_drvTstMouse.pDrvBase = NULL;
                pUsbIns->u32Version         = uInstanceVersion;
                pUsbIns->iInstance          = iInstance;
                pUsbIns->pHlpR3             = &g_tstUsbHlp;
                pUsbIns->pvInstanceDataR3   = pUsbIns->achInstanceData;
                pUsbIns->pCfg               = pCfg;
                rc = g_UsbHidMou.pfnConstruct(pUsbIns, iInstance, pCfg, NULL);
                if (RT_SUCCESS(rc))
                {
                   *ppThis = pUsbIns;
                   return rc;
                }
            }
            /* Failure */
            CFGMR3DestroyTree(pCfg);
        }
    }
    RTTestGuardedFree(hTest, pUsbIns);
    return rc;
}


static void tstMouseDestruct(RTTEST hTest, PPDMUSBINS pUsbIns)
{
    if (pUsbIns)
    {
        g_UsbHidMou.pfnDestruct(pUsbIns);
        CFGMR3DestroyTree(pUsbIns->pCfg);
        RTTestGuardedFree(hTest, pUsbIns);
    }
}


static void testConstructAndDestruct(RTTEST hTest)
{
    RTTestSub(hTest, "simple construction and destruction");

    /*
     * Normal check first.
     */
    PPDMUSBINS pUsbIns = NULL;
    RTTEST_CHECK_RC(hTest, tstMouseConstruct(hTest, 0, "relative", 1, &pUsbIns), VINF_SUCCESS);
    tstMouseDestruct(hTest, pUsbIns);

    /*
     * Modify the dev hlp version.
     */
    static struct
    {
        int         rc;
        uint32_t    uInsVersion;
        uint32_t    uHlpVersion;
    } const s_aVersionTests[] =
    {
        {  VERR_PDM_USBHLPR3_VERSION_MISMATCH, PDM_USBINS_VERSION, 0 },
        {  VERR_PDM_USBHLPR3_VERSION_MISMATCH, PDM_USBINS_VERSION, PDM_USBHLP_VERSION - PDM_VERSION_MAKE(0, 1, 0) },
        {  VERR_PDM_USBHLPR3_VERSION_MISMATCH, PDM_USBINS_VERSION, PDM_USBHLP_VERSION + PDM_VERSION_MAKE(0, 1, 0) },
        {  VERR_PDM_USBHLPR3_VERSION_MISMATCH, PDM_USBINS_VERSION, PDM_USBHLP_VERSION + PDM_VERSION_MAKE(0, 1, 1) },
        {  VERR_PDM_USBHLPR3_VERSION_MISMATCH, PDM_USBINS_VERSION, PDM_USBHLP_VERSION + PDM_VERSION_MAKE(1, 0, 0) },
        {  VERR_PDM_USBHLPR3_VERSION_MISMATCH, PDM_USBINS_VERSION, PDM_USBHLP_VERSION - PDM_VERSION_MAKE(1, 0, 0) },
        {  VINF_SUCCESS,                       PDM_USBINS_VERSION, PDM_USBHLP_VERSION + PDM_VERSION_MAKE(0, 0, 1) },
        {  VERR_PDM_USBINS_VERSION_MISMATCH,   PDM_USBINS_VERSION - PDM_VERSION_MAKE(0, 1, 0), PDM_USBHLP_VERSION },
        {  VERR_PDM_USBINS_VERSION_MISMATCH,   PDM_USBINS_VERSION + PDM_VERSION_MAKE(0, 1, 0), PDM_USBHLP_VERSION },
        {  VERR_PDM_USBINS_VERSION_MISMATCH,   PDM_USBINS_VERSION + PDM_VERSION_MAKE(0, 1, 1), PDM_USBHLP_VERSION },
        {  VERR_PDM_USBINS_VERSION_MISMATCH,   PDM_USBINS_VERSION + PDM_VERSION_MAKE(1, 0, 0), PDM_USBHLP_VERSION },
        {  VERR_PDM_USBINS_VERSION_MISMATCH,   PDM_USBINS_VERSION - PDM_VERSION_MAKE(1, 0, 0), PDM_USBHLP_VERSION },
        {  VINF_SUCCESS,                       PDM_USBINS_VERSION + PDM_VERSION_MAKE(0, 0, 1), PDM_USBHLP_VERSION },
        {  VINF_SUCCESS,
           PDM_USBINS_VERSION + PDM_VERSION_MAKE(0, 0, 1),         PDM_USBHLP_VERSION + PDM_VERSION_MAKE(0, 0, 1) },
    };
    bool const fSavedMayPanic = RTAssertSetMayPanic(false);
    bool const fSavedQuiet    = RTAssertSetQuiet(true);
    for (unsigned i = 0; i < RT_ELEMENTS(s_aVersionTests); i++)
    {
        g_tstUsbHlp.u32Version = g_tstUsbHlp.u32TheEnd = s_aVersionTests[i].uHlpVersion;
        pUsbIns = NULL;
        RTTEST_CHECK_RC(hTest, tstMouseConstruct(hTest, 0, "relative", 1, &pUsbIns, s_aVersionTests[i].uInsVersion),
                        s_aVersionTests[i].rc);
        tstMouseDestruct(hTest, pUsbIns);
    }
    RTAssertSetMayPanic(fSavedMayPanic);
    RTAssertSetQuiet(fSavedQuiet);

    g_tstUsbHlp.u32Version = g_tstUsbHlp.u32TheEnd = PDM_USBHLP_VERSION;
}


static void testSendPositionRel(RTTEST hTest)
{
    PPDMUSBINS pUsbIns = NULL;
    VUSBURB Urb;
    RTTestSub(hTest, "sending a relative position event");
    int rc = tstMouseConstruct(hTest, 0, "relative", 1, &pUsbIns);
    RT_ZERO(Urb);
    if (RT_SUCCESS(rc))
        rc = g_UsbHidMou.pfnUsbReset(pUsbIns, false);
    if (RT_SUCCESS(rc) && !g_drvTstMouse.pDrv)
        rc = VERR_PDM_MISSING_INTERFACE;
    RTTEST_CHECK_RC_OK(hTest, rc);
    if (RT_SUCCESS(rc))
    {
        g_drvTstMouse.pDrv->pfnPutEvent(g_drvTstMouse.pDrv, 123, -16, 1, -1, 3);
        Urb.EndPt = 0x01;
        Urb.enmType = VUSBXFERTYPE_INTR;
        Urb.cbData = 4;
        rc = g_UsbHidMou.pfnUrbQueue(pUsbIns, &Urb);
    }
    if (RT_SUCCESS(rc))
    {
        PVUSBURB pUrb = g_UsbHidMou.pfnUrbReap(pUsbIns, 0);
        if (pUrb)
        {
            if (pUrb == &Urb)
            {
                if (   Urb.abData[0] != 3    /* Buttons */
                    || Urb.abData[1] != 123  /* x */
                    || Urb.abData[2] != 240  /* 256 - y */
                    || Urb.abData[3] != 255  /* z */)
                    rc = VERR_GENERAL_FAILURE;
            }
            else
                rc = VERR_GENERAL_FAILURE;
        }
        else
            rc = VERR_GENERAL_FAILURE;
    }
    RTTEST_CHECK_RC_OK(hTest, rc);
    tstMouseDestruct(hTest, pUsbIns);
}


static void testSendPositionAbs(RTTEST hTest)
{
    PPDMUSBINS pUsbIns = NULL;
    VUSBURB Urb;
    RTTestSub(hTest, "sending an absolute position event");
    int rc = tstMouseConstruct(hTest, 0, "absolute", 1, &pUsbIns);
    RT_ZERO(Urb);
    if (RT_SUCCESS(rc))
        rc = g_UsbHidMou.pfnUsbReset(pUsbIns, false);
    if (RT_SUCCESS(rc))
    {
        if (g_drvTstMouse.pDrv)
            g_drvTstMouse.pDrv->pfnPutEventAbs(g_drvTstMouse.pDrv, 300, 200, 1, 3, 3);
        else
            rc = VERR_PDM_MISSING_INTERFACE;
    }
    if (RT_SUCCESS(rc))
    {
        Urb.EndPt = 0x01;
        Urb.enmType = VUSBXFERTYPE_INTR;
        Urb.cbData = 8;
        rc = g_UsbHidMou.pfnUrbQueue(pUsbIns, &Urb);
    }
    if (RT_SUCCESS(rc))
    {
        PVUSBURB pUrb = g_UsbHidMou.pfnUrbReap(pUsbIns, 0);
        if (pUrb)
        {
            if (pUrb == &Urb)
            {
                if (   Urb.abData[0] != 3                  /* Buttons */
                    || (int8_t)Urb.abData[1] != -1         /* dz */
                    || (int8_t)Urb.abData[2] != -3         /* dw */
                    || *(uint16_t *)&Urb.abData[4] != 150  /* x >> 1 */
                    || *(uint16_t *)&Urb.abData[6] != 100  /* y >> 1 */)
                    rc = VERR_GENERAL_FAILURE;
            }
            else
                rc = VERR_GENERAL_FAILURE;
        }
        else
            rc = VERR_GENERAL_FAILURE;
    }
    RTTEST_CHECK_RC_OK(hTest, rc);
    tstMouseDestruct(hTest, pUsbIns);
}

#if 0
/** @todo PDM interface was updated. This is not working anymore. */
static void testSendPositionMT(RTTEST hTest)
{
    PPDMUSBINS pUsbIns = NULL;
    VUSBURB Urb;
    RTTestSub(hTest, "sending a multi-touch position event");
    int rc = tstMouseConstruct(hTest, 0, "multitouch", 1, &pUsbIns);
    RT_ZERO(Urb);
    if (RT_SUCCESS(rc))
    {
        rc = g_UsbHidMou.pfnUsbReset(pUsbIns, false);
    }
    if (RT_SUCCESS(rc))
    {
        if (g_drvTstMouse.pDrv)
            g_drvTstMouse.pDrv->pfnPutEventMT(g_drvTstMouse.pDrv, 300, 200, 2,
                                              3);
        else
            rc = VERR_PDM_MISSING_INTERFACE;
    }
    if (RT_SUCCESS(rc))
    {
        Urb.EndPt = 0x01;
        Urb.enmType = VUSBXFERTYPE_INTR;
        Urb.cbData = 8;
        rc = g_UsbHidMou.pfnUrbQueue(pUsbIns, &Urb);
    }
    if (RT_SUCCESS(rc))
    {
        PVUSBURB pUrb = g_UsbHidMou.pfnUrbReap(pUsbIns, 0);
        if (pUrb)
        {
            if (pUrb == &Urb)
            {
                if (   Urb.abData[0] != 1                  /* Report ID */
                    || Urb.abData[1] != 3                  /* Contact flags */
                    || *(uint16_t *)&Urb.abData[2] != 150  /* x >> 1 */
                    || *(uint16_t *)&Urb.abData[4] != 100  /* y >> 1 */
                    || Urb.abData[6] != 2                  /* Contact number */)
                    rc = VERR_GENERAL_FAILURE;
            }
            else
                rc = VERR_GENERAL_FAILURE;
        }
        else
            rc = VERR_GENERAL_FAILURE;
    }
    RTTEST_CHECK_RC_OK(hTest, rc);
    tstMouseDestruct(hTest, pUsbIns);
}
#endif

int main()
{
    /*
     * Init the runtime, test and say hello.
     */
    RTTEST hTest;
    int rc = RTTestInitAndCreate("tstUsbMouse", &hTest);
    if (rc)
        return rc;
    RTTestBanner(hTest);
    /* Set up our faked PDMUSBHLP interface. */
    g_tstUsbHlp.u32Version      = PDM_USBHLP_VERSION;
    g_tstUsbHlp.pfnVMSetErrorV  = tstVMSetErrorV;
    g_tstUsbHlp.pfnDriverAttach = tstDriverAttach;
    g_tstUsbHlp.pfnTimerCreate  = tstTimerCreate;
    g_tstUsbHlp.pfnTimerDestroy = tstTimerDestroy;
    g_tstUsbHlp.pfnCFGMValidateConfig = CFGMR3ValidateConfig;
    g_tstUsbHlp.pfnCFGMQueryStringDef = CFGMR3QueryStringDef;
    g_tstUsbHlp.pfnCFGMQueryU8Def     = CFGMR3QueryU8Def;
    g_tstUsbHlp.u32TheEnd       = PDM_USBHLP_VERSION;
    /* Set up our global mouse driver */
    g_drvTstMouse.IBase.pfnQueryInterface = tstMouseQueryInterface;
    g_drvTstMouse.IConnector.pfnReportModes = tstMouseReportModes;

    /*
     * Run the tests.
     */
    testConstructAndDestruct(hTest);
    testSendPositionRel(hTest);
    testSendPositionAbs(hTest);
    /* testSendPositionMT(hTest); */
    return RTTestSummaryAndDestroy(hTest);
}
