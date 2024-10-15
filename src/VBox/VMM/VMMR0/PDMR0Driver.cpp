/* $Id: PDMR0Driver.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * PDM - Pluggable Device and Driver Manager, R0 Driver parts.
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


/*********************************************************************************************************************************
*   Header Files                                                                                                                 *
*********************************************************************************************************************************/
#define LOG_GROUP LOG_GROUP_PDM_DRIVER
#include "PDMInternal.h"
#include <VBox/vmm/pdm.h>
#include <VBox/vmm/vmcc.h>
#include <VBox/vmm/gvmm.h>

#include <VBox/log.h>
#include <iprt/errcore.h>
#include <iprt/assert.h>


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
RT_C_DECLS_BEGIN
extern DECLEXPORT(const PDMDRVHLPR0)    g_pdmR0DrvHlp;
RT_C_DECLS_END


/** @name Ring-0 Context Driver Helpers
 * @{
 */

/** @interface_method_impl{PDMDRVHLPR0,pfnAssertEMT} */
static DECLCALLBACK(bool) pdmR0DrvHlp_AssertEMT(PPDMDRVINS pDrvIns, const char *pszFile, unsigned iLine, const char *pszFunction)
{
    PDMDRV_ASSERT_DRVINS(pDrvIns);
    if (VM_IS_EMT(pDrvIns->Internal.s.pVMR0))
        return true;

    RTAssertMsg1Weak("AssertEMT", iLine, pszFile, pszFunction);
    RTAssertPanic();
    return false;
}


/** @interface_method_impl{PDMDRVHLPR0,pfnAssertOther} */
static DECLCALLBACK(bool) pdmR0DrvHlp_AssertOther(PPDMDRVINS pDrvIns, const char *pszFile, unsigned iLine, const char *pszFunction)
{
    PDMDRV_ASSERT_DRVINS(pDrvIns);
    if (!VM_IS_EMT(pDrvIns->Internal.s.pVMR0))
        return true;

    RTAssertMsg1Weak("AssertOther", iLine, pszFile, pszFunction);
    RTAssertPanic();
    return false;
}


/** @interface_method_impl{PDMDRVHLPR0,pfnCritSectEnter} */
static DECLCALLBACK(int)      pdmR0DrvHlp_CritSectEnter(PPDMDRVINS pDrvIns, PPDMCRITSECT pCritSect, int rcBusy)
{
    PDMDRV_ASSERT_DRVINS(pDrvIns);
    return PDMCritSectEnter(pDrvIns->Internal.s.pVMR0, pCritSect, rcBusy);
}


/** @interface_method_impl{PDMDRVHLPR0,pfnCritSectEnterDebug} */
static DECLCALLBACK(int)      pdmR0DrvHlp_CritSectEnterDebug(PPDMDRVINS pDrvIns, PPDMCRITSECT pCritSect, int rcBusy,
                                                             RTHCUINTPTR uId, RT_SRC_POS_DECL)
{
    PDMDRV_ASSERT_DRVINS(pDrvIns);
    return PDMCritSectEnterDebug(pDrvIns->Internal.s.pVMR0, pCritSect, rcBusy, uId, RT_SRC_POS_ARGS);
}


/** @interface_method_impl{PDMDRVHLPR0,pfnCritSectTryEnter} */
static DECLCALLBACK(int)      pdmR0DrvHlp_CritSectTryEnter(PPDMDRVINS pDrvIns, PPDMCRITSECT pCritSect)
{
    PDMDRV_ASSERT_DRVINS(pDrvIns);
    return PDMCritSectTryEnter(pDrvIns->Internal.s.pVMR0, pCritSect);
}


/** @interface_method_impl{PDMDRVHLPR0,pfnCritSectTryEnterDebug} */
static DECLCALLBACK(int)      pdmR0DrvHlp_CritSectTryEnterDebug(PPDMDRVINS pDrvIns, PPDMCRITSECT pCritSect,
                                                                RTHCUINTPTR uId, RT_SRC_POS_DECL)
{
    PDMDRV_ASSERT_DRVINS(pDrvIns);
    return PDMCritSectTryEnterDebug(pDrvIns->Internal.s.pVMR0, pCritSect, uId, RT_SRC_POS_ARGS);
}


/** @interface_method_impl{PDMDRVHLPR0,pfnCritSectLeave} */
static DECLCALLBACK(int)      pdmR0DrvHlp_CritSectLeave(PPDMDRVINS pDrvIns, PPDMCRITSECT pCritSect)
{
    PDMDRV_ASSERT_DRVINS(pDrvIns);
    return PDMCritSectLeave(pDrvIns->Internal.s.pVMR0, pCritSect);
}


/** @interface_method_impl{PDMDRVHLPR0,pfnCritSectIsOwner} */
static DECLCALLBACK(bool)     pdmR0DrvHlp_CritSectIsOwner(PPDMDRVINS pDrvIns, PCPDMCRITSECT pCritSect)
{
    PDMDRV_ASSERT_DRVINS(pDrvIns);
    return PDMCritSectIsOwner(pDrvIns->Internal.s.pVMR0, pCritSect);
}


/** @interface_method_impl{PDMDRVHLPR0,pfnCritSectIsInitialized} */
static DECLCALLBACK(bool)     pdmR0DrvHlp_CritSectIsInitialized(PPDMDRVINS pDrvIns, PCPDMCRITSECT pCritSect)
{
    PDMDRV_ASSERT_DRVINS(pDrvIns);
    NOREF(pDrvIns);
    return PDMCritSectIsInitialized(pCritSect);
}


/** @interface_method_impl{PDMDRVHLPR0,pfnCritSectHasWaiters} */
static DECLCALLBACK(bool)     pdmR0DrvHlp_CritSectHasWaiters(PPDMDRVINS pDrvIns, PCPDMCRITSECT pCritSect)
{
    PDMDRV_ASSERT_DRVINS(pDrvIns);
    return PDMCritSectHasWaiters(pDrvIns->Internal.s.pVMR0, pCritSect);
}


/** @interface_method_impl{PDMDRVHLPR0,pfnCritSectGetRecursion} */
static DECLCALLBACK(uint32_t) pdmR0DrvHlp_CritSectGetRecursion(PPDMDRVINS pDrvIns, PCPDMCRITSECT pCritSect)
{
    PDMDRV_ASSERT_DRVINS(pDrvIns);
    NOREF(pDrvIns);
    return PDMCritSectGetRecursion(pCritSect);
}


/** @interface_method_impl{PDMDRVHLPR0,pfnCritSectScheduleExitEvent} */
static DECLCALLBACK(int)      pdmR0DrvHlp_CritSectScheduleExitEvent(PPDMDRVINS pDrvIns, PPDMCRITSECT pCritSect,
                                                                    SUPSEMEVENT hEventToSignal)
{
    PDMDRV_ASSERT_DRVINS(pDrvIns);
    NOREF(pDrvIns);
    return PDMHCCritSectScheduleExitEvent(pCritSect, hEventToSignal);
}


/** @interface_method_impl{PDMDRVHLPR0,pfnNetShaperAllocateBandwidth} */
static DECLCALLBACK(bool) pdmR0DrvHlp_NetShaperAllocateBandwidth(PPDMDRVINS pDrvIns, PPDMNSFILTER pFilter, size_t cbTransfer)
{
#ifdef VBOX_WITH_NETSHAPER
    PDMDRV_ASSERT_DRVINS(pDrvIns);
    LogFlow(("pdmR0DrvHlp_NetShaperDetach: caller='%s'/%d: pFilter=%p cbTransfer=%#zx\n",
             pDrvIns->pReg->szName, pDrvIns->iInstance, pFilter, cbTransfer));

    bool const fRc = PDMNetShaperAllocateBandwidth(pDrvIns->Internal.s.pVMR0, pFilter, cbTransfer);

    LogFlow(("pdmR0DrvHlp_NetShaperDetach: caller='%s'/%d: returns %RTbool\n", pDrvIns->pReg->szName, pDrvIns->iInstance, fRc));
    return fRc;
#else
    RT_NOREF(pDrvIns, pFilter, cbTransfer);
    return true;
#endif
}


/**
 * The Ring-0 Context Driver Helper Callbacks.
 */
extern DECLEXPORT(const PDMDRVHLPR0) g_pdmR0DrvHlp =
{
    PDM_DRVHLPRC_VERSION,
    pdmR0DrvHlp_AssertEMT,
    pdmR0DrvHlp_AssertOther,
    pdmR0DrvHlp_CritSectEnter,
    pdmR0DrvHlp_CritSectEnterDebug,
    pdmR0DrvHlp_CritSectTryEnter,
    pdmR0DrvHlp_CritSectTryEnterDebug,
    pdmR0DrvHlp_CritSectLeave,
    pdmR0DrvHlp_CritSectIsOwner,
    pdmR0DrvHlp_CritSectIsInitialized,
    pdmR0DrvHlp_CritSectHasWaiters,
    pdmR0DrvHlp_CritSectGetRecursion,
    pdmR0DrvHlp_CritSectScheduleExitEvent,
    pdmR0DrvHlp_NetShaperAllocateBandwidth,
    PDM_DRVHLPRC_VERSION
};

/** @} */



/**
 * PDMDrvHlpCallR0 helper.
 *
 * @returns See PFNPDMDRVREQHANDLERR0.
 * @param   pGVM    The global (ring-0) VM structure. (For validation.)
 * @param   pReq    Pointer to the request buffer.
 */
VMMR0_INT_DECL(int) PDMR0DriverCallReqHandler(PGVM pGVM, PPDMDRIVERCALLREQHANDLERREQ pReq)
{
    /*
     * Validate input and make the call.
     */
    int rc = GVMMR0ValidateGVM(pGVM);
    if (RT_SUCCESS(rc))
    {
        AssertPtrReturn(pReq, VERR_INVALID_POINTER);
        AssertMsgReturn(pReq->Hdr.cbReq == sizeof(*pReq), ("%#x != %#x\n", pReq->Hdr.cbReq, sizeof(*pReq)), VERR_INVALID_PARAMETER);

        PPDMDRVINS pDrvIns = pReq->pDrvInsR0;
        AssertPtrReturn(pDrvIns, VERR_INVALID_POINTER);
        AssertReturn(pDrvIns->Internal.s.pVMR0 == pGVM, VERR_INVALID_PARAMETER);

        PFNPDMDRVREQHANDLERR0 pfnReqHandlerR0 = pDrvIns->Internal.s.pfnReqHandlerR0;
        AssertPtrReturn(pfnReqHandlerR0, VERR_INVALID_POINTER);

        rc = pfnReqHandlerR0(pDrvIns, pReq->uOperation, pReq->u64Arg);
    }
    return rc;
}

