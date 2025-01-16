/* $Id: SUPLib.cpp 107889 2025-01-16 00:29:04Z knut.osmundsen@oracle.com $ */
/** @file
 * VirtualBox Support Library - Common code.
 */

/*
 * Copyright (C) 2006-2024 Oracle and/or its affiliates.
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
 * The contents of this file may alternatively be used under the terms
 * of the Common Development and Distribution License Version 1.0
 * (CDDL), a copy of it is provided in the "COPYING.CDDL" file included
 * in the VirtualBox distribution, in which case the provisions of the
 * CDDL are applicable instead of those of the GPL.
 *
 * You may elect to license modified versions of this file under the
 * terms and conditions of either the GPL or the CDDL or both.
 *
 * SPDX-License-Identifier: GPL-3.0-only OR CDDL-1.0
 */

/** @page   pg_sup          SUP - The Support Library
 *
 * The support library is responsible for providing facilities to load
 * VMM Host Ring-0 code, to call Host VMM Ring-0 code from Ring-3 Host
 * code, to pin down physical memory, and more.
 *
 * The VMM Host Ring-0 code can be combined in the support driver if
 * permitted by kernel module license policies. If it is not combined
 * it will be externalized in a .r0 module that will be loaded using
 * the IPRT loader.
 *
 * The Ring-0 calling is done thru a generic SUP interface which will
 * transfer an argument set and call a predefined entry point in the Host
 * VMM Ring-0 code.
 *
 * See @ref grp_sup "SUP - Support APIs" for API details.
 */


/*********************************************************************************************************************************
*   Header Files                                                                                                                 *
*********************************************************************************************************************************/
#define LOG_GROUP LOG_GROUP_SUP
#include <VBox/sup.h>
#include <VBox/err.h>
#include <VBox/param.h>
#include <VBox/log.h>
#include <VBox/VBoxTpG.h>

#include <iprt/assert.h>
#include <iprt/alloc.h>
#include <iprt/alloca.h>
#include <iprt/ldr.h>
#include <iprt/asm.h>
#include <iprt/mp.h>
#include <iprt/cpuset.h>
#include <iprt/thread.h>
#include <iprt/process.h>
#include <iprt/path.h>
#include <iprt/string.h>
#include <iprt/env.h>
#include <iprt/rand.h>
#include <iprt/x86.h>

#include "SUPDrvIOC.h"
#include "SUPLibInternal.h"


/*********************************************************************************************************************************
*   Defined Constants And Macros                                                                                                 *
*********************************************************************************************************************************/
/** R0 VMM module name. */
#define VMMR0_NAME      "VMMR0"


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/
typedef DECLCALLBACKTYPE(int, FNCALLVMMR0,(PVMR0 pVMR0, unsigned uOperation, void *pvArg));
typedef FNCALLVMMR0 *PFNCALLVMMR0;


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
/** Init counter. */
static uint32_t                 g_cInits = 0;
/** Whether we've been preinitied. */
static bool                     g_fPreInited = false;
/** The SUPLib instance data.
 * Well, at least parts of it, specifically the parts that are being handed over
 * via the pre-init mechanism from the hardened executable stub.  */
DECL_HIDDEN_DATA(SUPLIBDATA)    g_supLibData =
{
    /*.hDevice              = */    SUP_HDEVICE_NIL,
    /*.fUnrestricted        = */    true,
    /*.fDriverless          = */    false
#if   defined(RT_OS_DARWIN)
    ,/* .uConnection        = */    0
#elif defined(RT_OS_LINUX)
    ,/* .fSysMadviseWorks   = */    false
#endif
};

/** Pointer to the Global Information Page.
 *
 * This pointer is valid as long as SUPLib has a open session. Anyone using
 * the page must treat this pointer as highly volatile and not trust it beyond
 * one transaction.
 *
 * @todo This will probably deserve it's own session or some other good solution...
 */
DECLEXPORT(PSUPGLOBALINFOPAGE)      g_pSUPGlobalInfoPage;
/** Address of the ring-0 mapping of the GIP. */
PSUPGLOBALINFOPAGE                  g_pSUPGlobalInfoPageR0;
/** The physical address of the GIP. */
static RTHCPHYS                     g_HCPhysSUPGlobalInfoPage = NIL_RTHCPHYS;

/** The negotiated cookie. */
DECL_HIDDEN_DATA(uint32_t)          g_u32Cookie = 0;
/** The negotiated session cookie. */
DECL_HIDDEN_DATA(uint32_t)          g_u32SessionCookie;
/** The session version. */
DECL_HIDDEN_DATA(uint32_t)          g_uSupSessionVersion = 0;
/** Session handle. */
DECL_HIDDEN_DATA(PSUPDRVSESSION)    g_pSession;
/** R0 SUP Functions used for resolving referenced to the SUPR0 module. */
DECL_HIDDEN_DATA(PSUPQUERYFUNCS)    g_pSupFunctions;

/** PAGE_ALLOC_EX sans kernel mapping support indicator. */
static bool                         g_fSupportsPageAllocNoKernel = true;
/** Fake mode indicator. (~0 at first, 0 or 1 after first test) */
DECL_HIDDEN_DATA(uint32_t)          g_uSupFakeMode = UINT32_MAX;


/*********************************************************************************************************************************
*   Internal Functions                                                                                                           *
*********************************************************************************************************************************/
static int supInitFake(PSUPDRVSESSION *ppSession);


/** Touch a range of pages. */
DECLINLINE(void) supR3TouchPages(void *pv, size_t cPages)
{
    uint32_t volatile *pu32 = (uint32_t volatile *)pv;
    while (cPages-- > 0)
    {
        ASMAtomicCmpXchgU32(pu32, 0, 0);
        pu32 += PAGE_SIZE / sizeof(uint32_t);
    }
}


SUPR3DECL(int) SUPR3Install(void)
{
    return suplibOsInstall();
}


SUPR3DECL(int) SUPR3Uninstall(void)
{
    return suplibOsUninstall();
}


DECL_NOTHROW(DECLEXPORT(int)) supR3PreInit(PSUPPREINITDATA pPreInitData, uint32_t fFlags)
{
    /*
     * The caller is kind of trustworthy, just perform some basic checks.
     *
     * Note! Do not do any fancy stuff here because IPRT has NOT been
     *       initialized at this point.
     */
    if (!RT_VALID_PTR(pPreInitData))
        return VERR_INVALID_POINTER;
    if (g_fPreInited || g_cInits > 0)
        return VERR_WRONG_ORDER;

    if (    pPreInitData->u32Magic != SUPPREINITDATA_MAGIC
        ||  pPreInitData->u32EndMagic != SUPPREINITDATA_MAGIC)
        return VERR_INVALID_MAGIC;
    if (    !(fFlags & SUPSECMAIN_FLAGS_DONT_OPEN_DEV)
        &&  pPreInitData->Data.hDevice == SUP_HDEVICE_NIL
        &&  !pPreInitData->Data.fDriverless)
        return VERR_INVALID_HANDLE;
    if (    (   (fFlags & SUPSECMAIN_FLAGS_DONT_OPEN_DEV)
             || pPreInitData->Data.fDriverless)
        &&  pPreInitData->Data.hDevice != SUP_HDEVICE_NIL)
        return VERR_INVALID_PARAMETER;

    /*
     * Hand out the data.
     */
    int rc = supR3HardenedRecvPreInitData(pPreInitData);
    if (RT_FAILURE(rc))
        return rc;

    /** @todo This may need some small restructuring later, it doesn't quite work with a root service flag... */
    if (!(fFlags & SUPSECMAIN_FLAGS_DONT_OPEN_DEV))
    {
        g_supLibData = pPreInitData->Data;
        g_fPreInited = true;
    }

    return VINF_SUCCESS;
}


SUPR3DECL(int) SUPR3InitEx(uint32_t fFlags, PSUPDRVSESSION *ppSession)
{
    /*
     * Perform some sanity checks.
     * (Got some trouble with compile time member alignment assertions.)
     */
    Assert(!(RT_UOFFSETOF(SUPGLOBALINFOPAGE, u64NanoTSLastUpdateHz) & 0x7));
    Assert(!(RT_UOFFSETOF(SUPGLOBALINFOPAGE, aCPUs) & 0x1f));
    Assert(!(RT_UOFFSETOF(SUPGLOBALINFOPAGE, aCPUs[1]) & 0x1f));
    Assert(!(RT_UOFFSETOF(SUPGLOBALINFOPAGE, aCPUs[0].u64NanoTS) & 0x7));
    Assert(!(RT_UOFFSETOF(SUPGLOBALINFOPAGE, aCPUs[0].u64TSC) & 0x7));
    Assert(!(RT_UOFFSETOF(SUPGLOBALINFOPAGE, aCPUs[0].u64CpuHz) & 0x7));

#ifdef VBOX_WITH_DRIVERLESS_FORCED
    fFlags |= SUPR3INIT_F_DRIVERLESS;
    fFlags &= ~SUPR3INIT_F_UNRESTRICTED;
#endif

    /*
     * Check if already initialized.
     */
    if (ppSession)
        *ppSession = g_pSession;
    if (g_cInits++ > 0)
    {
        if (   (fFlags & SUPR3INIT_F_UNRESTRICTED)
            && !g_supLibData.fUnrestricted
            && !g_supLibData.fDriverless)
        {
            g_cInits--;
            if (ppSession)
                *ppSession = NIL_RTR0PTR;
            return VERR_VM_DRIVER_NOT_ACCESSIBLE; /** @todo different status code? */
        }
        return VINF_SUCCESS;
    }

    /*
     * Check for fake mode.
     *
     * Fake mode is used when we're doing smoke testing and debugging.
     * It's also useful on platforms where we haven't root access or which
     * we haven't ported the support driver to.
     */
    if (g_uSupFakeMode == ~0U)
    {
        const char *psz = RTEnvGet("VBOX_SUPLIB_FAKE");
        if (psz && !strcmp(psz, "fake"))
            ASMAtomicCmpXchgU32(&g_uSupFakeMode, 1, ~0U);
        else
            ASMAtomicCmpXchgU32(&g_uSupFakeMode, 0, ~0U);
    }
    if (RT_UNLIKELY(g_uSupFakeMode))
        return supInitFake(ppSession);

    /*
     * Open the support driver.
     */
    SUPINITOP enmWhat = kSupInitOp_Driver;
    int rc = suplibOsInit(&g_supLibData, g_fPreInited, fFlags, &enmWhat, NULL);
    if (RT_SUCCESS(rc) && !g_supLibData.fDriverless)
    {
        /*
         * Negotiate the cookie.
         */
        SUPCOOKIE CookieReq;
        memset(&CookieReq, 0xff, sizeof(CookieReq));
        CookieReq.Hdr.u32Cookie = SUPCOOKIE_INITIAL_COOKIE;
        CookieReq.Hdr.u32SessionCookie = RTRandU32();
        CookieReq.Hdr.cbIn = SUP_IOCTL_COOKIE_SIZE_IN;
        CookieReq.Hdr.cbOut = SUP_IOCTL_COOKIE_SIZE_OUT;
        CookieReq.Hdr.fFlags = SUPREQHDR_FLAGS_DEFAULT;
        CookieReq.Hdr.rc = VERR_INTERNAL_ERROR;
        strcpy(CookieReq.u.In.szMagic, SUPCOOKIE_MAGIC);
        CookieReq.u.In.u32ReqVersion = SUPDRV_IOC_VERSION;
        const uint32_t uMinVersion = (SUPDRV_IOC_VERSION & 0xffff0000) == 0x00340000
                                   ? 0x00340001
                                   : SUPDRV_IOC_VERSION & 0xffff0000;
        CookieReq.u.In.u32MinVersion = uMinVersion;
        rc = suplibOsIOCtl(&g_supLibData, SUP_IOCTL_COOKIE, &CookieReq, SUP_IOCTL_COOKIE_SIZE);
        if (    RT_SUCCESS(rc)
            &&  RT_SUCCESS(CookieReq.Hdr.rc))
        {
            g_uSupSessionVersion = CookieReq.u.Out.u32SessionVersion;
            if (    (CookieReq.u.Out.u32SessionVersion & 0xffff0000) == (SUPDRV_IOC_VERSION & 0xffff0000)
                &&  CookieReq.u.Out.u32SessionVersion >= uMinVersion)
            {
                /*
                 * Query the functions.
                 */
                PSUPQUERYFUNCS pFuncsReq = NULL;
                if (g_supLibData.fUnrestricted)
                {
                    pFuncsReq = (PSUPQUERYFUNCS)RTMemAllocZ(SUP_IOCTL_QUERY_FUNCS_SIZE(CookieReq.u.Out.cFunctions));
                    if (pFuncsReq)
                    {
                        pFuncsReq->Hdr.u32Cookie            = CookieReq.u.Out.u32Cookie;
                        pFuncsReq->Hdr.u32SessionCookie     = CookieReq.u.Out.u32SessionCookie;
                        pFuncsReq->Hdr.cbIn                 = SUP_IOCTL_QUERY_FUNCS_SIZE_IN;
                        pFuncsReq->Hdr.cbOut                = SUP_IOCTL_QUERY_FUNCS_SIZE_OUT(CookieReq.u.Out.cFunctions);
                        pFuncsReq->Hdr.fFlags               = SUPREQHDR_FLAGS_DEFAULT;
                        pFuncsReq->Hdr.rc                   = VERR_INTERNAL_ERROR;
                        rc = suplibOsIOCtl(&g_supLibData, SUP_IOCTL_QUERY_FUNCS(CookieReq.u.Out.cFunctions), pFuncsReq,
                                           SUP_IOCTL_QUERY_FUNCS_SIZE(CookieReq.u.Out.cFunctions));
                        if (RT_SUCCESS(rc))
                            rc = pFuncsReq->Hdr.rc;
                        if (RT_SUCCESS(rc))
                        {
                            /*
                             * Map the GIP into userspace.
                             */
                            Assert(!g_pSUPGlobalInfoPage);
                            SUPGIPMAP GipMapReq;
                            GipMapReq.Hdr.u32Cookie         = CookieReq.u.Out.u32Cookie;
                            GipMapReq.Hdr.u32SessionCookie  = CookieReq.u.Out.u32SessionCookie;
                            GipMapReq.Hdr.cbIn              = SUP_IOCTL_GIP_MAP_SIZE_IN;
                            GipMapReq.Hdr.cbOut             = SUP_IOCTL_GIP_MAP_SIZE_OUT;
                            GipMapReq.Hdr.fFlags            = SUPREQHDR_FLAGS_DEFAULT;
                            GipMapReq.Hdr.rc                = VERR_INTERNAL_ERROR;
                            GipMapReq.u.Out.HCPhysGip       = NIL_RTHCPHYS;
                            GipMapReq.u.Out.pGipR0          = NIL_RTR0PTR;
                            GipMapReq.u.Out.pGipR3          = NULL;
                            rc = suplibOsIOCtl(&g_supLibData, SUP_IOCTL_GIP_MAP, &GipMapReq, SUP_IOCTL_GIP_MAP_SIZE);
                            if (RT_SUCCESS(rc))
                                rc = GipMapReq.Hdr.rc;
                            if (RT_SUCCESS(rc))
                            {
                                /*
                                 * Set the GIP globals.
                                 */
                                AssertRelease(GipMapReq.u.Out.pGipR3->u32Magic == SUPGLOBALINFOPAGE_MAGIC);
                                AssertRelease(GipMapReq.u.Out.pGipR3->u32Version >= SUPGLOBALINFOPAGE_VERSION);

                                ASMAtomicXchgSize(&g_HCPhysSUPGlobalInfoPage, GipMapReq.u.Out.HCPhysGip);
                                ASMAtomicCmpXchgPtr((void * volatile *)&g_pSUPGlobalInfoPage, GipMapReq.u.Out.pGipR3, NULL);
                                ASMAtomicCmpXchgPtr((void * volatile *)&g_pSUPGlobalInfoPageR0, (void *)GipMapReq.u.Out.pGipR0, NULL);
                            }
                        }
                    }
                    else
                        rc = VERR_NO_MEMORY;
                }

                if (RT_SUCCESS(rc))
                {
                    /*
                     * Set the globals and return success.
                     */
                    g_u32Cookie         = CookieReq.u.Out.u32Cookie;
                    g_u32SessionCookie  = CookieReq.u.Out.u32SessionCookie;
                    g_pSession          = CookieReq.u.Out.pSession;
                    g_pSupFunctions  = pFuncsReq;
                    if (ppSession)
                        *ppSession = CookieReq.u.Out.pSession;
                    return VINF_SUCCESS;
                }

                /* bailout */
                RTMemFree(pFuncsReq);
            }
            else
            {
                LogRel(("Support driver version mismatch: SessionVersion=%#x DriverVersion=%#x ClientVersion=%#x MinVersion=%#x\n",
                        CookieReq.u.Out.u32SessionVersion, CookieReq.u.Out.u32DriverVersion, SUPDRV_IOC_VERSION, uMinVersion));
                rc = VERR_VM_DRIVER_VERSION_MISMATCH;
            }
        }
        else
        {
            if (RT_SUCCESS(rc))
            {
                rc = CookieReq.Hdr.rc;
                LogRel(("Support driver version mismatch: DriverVersion=%#x ClientVersion=%#x rc=%Rrc\n",
                        CookieReq.u.Out.u32DriverVersion, SUPDRV_IOC_VERSION, rc));
                if (rc != VERR_VM_DRIVER_VERSION_MISMATCH)
                    rc = VERR_VM_DRIVER_VERSION_MISMATCH;
            }
            else
            {
                /* for pre 0x00060000 drivers */
                LogRel(("Support driver version mismatch: DriverVersion=too-old ClientVersion=%#x\n", SUPDRV_IOC_VERSION));
                rc = VERR_VM_DRIVER_VERSION_MISMATCH;
            }
        }

        suplibOsTerm(&g_supLibData);
    }
    else if (RT_SUCCESS(rc))
    {
        /*
         * Driverless initialization.
         */
        Assert(fFlags & SUPR3INIT_F_DRIVERLESS_MASK);
        LogRel(("SUP: In driverless mode.\n"));
        return VINF_SUCCESS;
    }

    g_cInits--;

    return rc;
}


SUPR3DECL(int) SUPR3Init(PSUPDRVSESSION *ppSession)
{
#ifndef VBOX_WITH_DRIVERLESS_FORCED
    return SUPR3InitEx(SUPR3INIT_F_UNRESTRICTED, ppSession);
#else
    return SUPR3InitEx(SUPR3INIT_F_DRIVERLESS, ppSession);
#endif
}

/**
 * Fake mode init.
 */
static int supInitFake(PSUPDRVSESSION *ppSession)
{
    Log(("SUP: Fake mode!\n"));
    static const SUPFUNC s_aFakeFunctions[] =
    {
        /* name                                     0, function */
        { "SUPR0AbsIs64bit",                        0, 0 },
        { "SUPR0Abs64bitKernelCS",                  0, 0 },
        { "SUPR0Abs64bitKernelSS",                  0, 0 },
        { "SUPR0Abs64bitKernelDS",                  0, 0 },
        { "SUPR0AbsKernelCS",                       0, 8 },
        { "SUPR0AbsKernelSS",                       0, 16 },
        { "SUPR0AbsKernelDS",                       0, 16 },
        { "SUPR0AbsKernelES",                       0, 16 },
        { "SUPR0AbsKernelFS",                       0, 24 },
        { "SUPR0AbsKernelGS",                       0, 32 },
        { "SUPR0ComponentRegisterFactory",          0, 0xefeefffd },
        { "SUPR0ComponentDeregisterFactory",        0, 0xefeefffe },
        { "SUPR0ComponentQueryFactory",             0, 0xefeeffff },
        { "SUPR0ObjRegister",                       0, 0xefef0000 },
        { "SUPR0ObjAddRef",                         0, 0xefef0001 },
        { "SUPR0ObjAddRefEx",                       0, 0xefef0001 },
        { "SUPR0ObjRelease",                        0, 0xefef0002 },
        { "SUPR0ObjVerifyAccess",                   0, 0xefef0003 },
        { "SUPR0LockMem",                           0, 0xefef0004 },
        { "SUPR0UnlockMem",                         0, 0xefef0005 },
        { "SUPR0ContAlloc",                         0, 0xefef0006 },
        { "SUPR0ContFree",                          0, 0xefef0007 },
        { "SUPR0MemAlloc",                          0, 0xefef0008 },
        { "SUPR0MemGetPhys",                        0, 0xefef0009 },
        { "SUPR0MemFree",                           0, 0xefef000a },
        { "SUPR0Printf",                            0, 0xefef000b },
        { "SUPR0GetPagingMode",                     0, 0xefef000c },
        { "SUPR0EnableVTx",                         0, 0xefef000e },
        { "RTMemAlloc",                             0, 0xefef000f },
        { "RTMemAllocZ",                            0, 0xefef0010 },
        { "RTMemFree",                              0, 0xefef0011 },
        { "RTR0MemObjAddress",                      0, 0xefef0012 },
        { "RTR0MemObjAddressR3",                    0, 0xefef0013 },
        { "RTR0MemObjAllocPage",                    0, 0xefef0014 },
        { "RTR0MemObjAllocPhysNC",                  0, 0xefef0015 },
        { "RTR0MemObjAllocLow",                     0, 0xefef0016 },
        { "RTR0MemObjEnterPhys",                    0, 0xefef0017 },
        { "RTR0MemObjFree",                         0, 0xefef0018 },
        { "RTR0MemObjGetPagePhysAddr",              0, 0xefef0019 },
        { "RTR0MemObjMapUser",                      0, 0xefef001a },
        { "RTR0MemObjMapKernel",                    0, 0xefef001b },
        { "RTR0MemObjMapKernelEx",                  0, 0xefef001c },
        { "RTMpGetArraySize",                       0, 0xefef001c },
        { "RTProcSelf",                             0, 0xefef001d },
        { "RTR0ProcHandleSelf",                     0, 0xefef001e },
        { "RTSemEventCreate",                       0, 0xefef001f },
        { "RTSemEventSignal",                       0, 0xefef0020 },
        { "RTSemEventWait",                         0, 0xefef0021 },
        { "RTSemEventWaitNoResume",                 0, 0xefef0022 },
        { "RTSemEventDestroy",                      0, 0xefef0023 },
        { "RTSemEventMultiCreate",                  0, 0xefef0024 },
        { "RTSemEventMultiSignal",                  0, 0xefef0025 },
        { "RTSemEventMultiReset",                   0, 0xefef0026 },
        { "RTSemEventMultiWait",                    0, 0xefef0027 },
        { "RTSemEventMultiWaitNoResume",            0, 0xefef0028 },
        { "RTSemEventMultiDestroy",                 0, 0xefef0029 },
        { "RTSemFastMutexCreate",                   0, 0xefef002a },
        { "RTSemFastMutexDestroy",                  0, 0xefef002b },
        { "RTSemFastMutexRequest",                  0, 0xefef002c },
        { "RTSemFastMutexRelease",                  0, 0xefef002d },
        { "RTSpinlockCreate",                       0, 0xefef002e },
        { "RTSpinlockDestroy",                      0, 0xefef002f },
        { "RTSpinlockAcquire",                      0, 0xefef0030 },
        { "RTSpinlockRelease",                      0, 0xefef0031 },
        { "RTSpinlockAcquireNoInts",                0, 0xefef0032 },
        { "RTTimeNanoTS",                           0, 0xefef0034 },
        { "RTTimeMillieTS",                         0, 0xefef0035 },
        { "RTTimeSystemNanoTS",                     0, 0xefef0036 },
        { "RTTimeSystemMillieTS",                   0, 0xefef0037 },
        { "RTThreadNativeSelf",                     0, 0xefef0038 },
        { "RTThreadSleep",                          0, 0xefef0039 },
        { "RTThreadYield",                          0, 0xefef003a },
        { "RTTimerCreate",                          0, 0xefef003a },
        { "RTTimerCreateEx",                        0, 0xefef003a },
        { "RTTimerDestroy",                         0, 0xefef003a },
        { "RTTimerStart",                           0, 0xefef003a },
        { "RTTimerStop",                            0, 0xefef003a },
        { "RTTimerChangeInterval",                  0, 0xefef003a },
        { "RTTimerGetSystemGranularity",            0, 0xefef003a },
        { "RTTimerRequestSystemGranularity",        0, 0xefef003a },
        { "RTTimerReleaseSystemGranularity",        0, 0xefef003a },
        { "RTTimerCanDoHighResolution",             0, 0xefef003a },
        { "RTLogDefaultInstance",                   0, 0xefef003b },
        { "RTLogRelGetDefaultInstance",             0, 0xefef003c },
        { "RTLogSetDefaultInstanceThread",          0, 0xefef003d },
        { "RTLogLogger",                            0, 0xefef003e },
        { "RTLogLoggerEx",                          0, 0xefef003f },
        { "RTLogLoggerExV",                         0, 0xefef0040 },
        { "RTAssertMsg1",                           0, 0xefef0041 },
        { "RTAssertMsg2",                           0, 0xefef0042 },
        { "RTAssertMsg2V",                          0, 0xefef0043 },
        { "SUPR0QueryVTCaps",                       0, 0xefef0044 },
    };

    /* fake r0 functions. */
    g_pSupFunctions = (PSUPQUERYFUNCS)RTMemAllocZ(SUP_IOCTL_QUERY_FUNCS_SIZE(RT_ELEMENTS(s_aFakeFunctions)));
    if (g_pSupFunctions)
    {
        g_pSupFunctions->u.Out.cFunctions = RT_ELEMENTS(s_aFakeFunctions);
        memcpy(&g_pSupFunctions->u.Out.aFunctions[0], &s_aFakeFunctions[0], sizeof(s_aFakeFunctions));
        g_pSession = (PSUPDRVSESSION)(void *)g_pSupFunctions;
        if (ppSession)
            *ppSession = g_pSession;

        /* fake the GIP. */
        g_pSUPGlobalInfoPage = (PSUPGLOBALINFOPAGE)RTMemPageAllocZ(PAGE_SIZE);
        if (g_pSUPGlobalInfoPage)
        {
            g_pSUPGlobalInfoPageR0 = g_pSUPGlobalInfoPage;
            g_HCPhysSUPGlobalInfoPage = NIL_RTHCPHYS & ~(RTHCPHYS)PAGE_OFFSET_MASK;
            /* the page is supposed to be invalid, so don't set the magic. */
            return VINF_SUCCESS;
        }

        RTMemFree(g_pSupFunctions);
        g_pSupFunctions = NULL;
    }
    return VERR_NO_MEMORY;
}


SUPR3DECL(int) SUPR3Term(bool fForced)
{
    /*
     * Verify state.
     */
    AssertMsg(g_cInits > 0, ("SUPR3Term() is called before SUPR3Init()!\n"));
    if (g_cInits == 0)
        return VERR_WRONG_ORDER;
    if (g_cInits == 1 || fForced)
    {
        /*
         * NULL the GIP pointer.
         */
        if (g_pSUPGlobalInfoPage)
        {
            ASMAtomicWriteNullPtr((void * volatile *)&g_pSUPGlobalInfoPage);
            ASMAtomicWriteNullPtr((void * volatile *)&g_pSUPGlobalInfoPageR0);
            ASMAtomicWriteU64(&g_HCPhysSUPGlobalInfoPage, NIL_RTHCPHYS);
            /* just a little safe guard against threads using the page. */
            RTThreadSleep(50);
        }

        /*
         * Close the support driver.
         */
        int rc = suplibOsTerm(&g_supLibData);
        if (rc)
            return rc;

        g_supLibData.hDevice       = SUP_HDEVICE_NIL;
        g_supLibData.fUnrestricted = true;
        g_supLibData.fDriverless   = false;
        g_u32Cookie                = 0;
        g_u32SessionCookie         = 0;
        g_cInits                   = 0;
    }
    else
        g_cInits--;

    return 0;
}


SUPR3DECL(bool) SUPR3IsDriverless(void)
{
    /* Assert(g_cInits > 0); - tstSSM does not initialize SUP, but SSM calls to
       check status, so return driverless if not initialized. */
    return g_supLibData.fDriverless || g_cInits == 0;
}


SUPR3DECL(SUPPAGINGMODE) SUPR3GetPagingMode(void)
{
    /*
     * Deal with driverless first.
     */
    if (g_supLibData.fDriverless)
#if defined(RT_ARCH_AMD64)
        return SUPPAGINGMODE_AMD64_GLOBAL_NX;
#elif defined(RT_ARCH_X86)
        return SUPPAGINGMODE_32_BIT_GLOBAL;
#else
        return SUPPAGINGMODE_INVALID;
#endif

    /*
     * Issue IOCtl to the SUPDRV kernel module.
     */
    SUPGETPAGINGMODE Req;
    Req.Hdr.u32Cookie = g_u32Cookie;
    Req.Hdr.u32SessionCookie = g_u32SessionCookie;
    Req.Hdr.cbIn = SUP_IOCTL_GET_PAGING_MODE_SIZE_IN;
    Req.Hdr.cbOut = SUP_IOCTL_GET_PAGING_MODE_SIZE_OUT;
    Req.Hdr.fFlags = SUPREQHDR_FLAGS_DEFAULT;
    Req.Hdr.rc = VERR_INTERNAL_ERROR;
    int rc = suplibOsIOCtl(&g_supLibData, SUP_IOCTL_GET_PAGING_MODE, &Req, SUP_IOCTL_GET_PAGING_MODE_SIZE);
    if (    RT_FAILURE(rc)
        ||  RT_FAILURE(Req.Hdr.rc))
    {
        LogRel(("SUPR3GetPagingMode: %Rrc %Rrc\n", rc, Req.Hdr.rc));
        Req.u.Out.enmMode = SUPPAGINGMODE_INVALID;
    }

    return Req.u.Out.enmMode;
}


/**
 * For later.
 */
static int supCallVMMR0ExFake(PVMR0 pVMR0, unsigned uOperation, uint64_t u64Arg, PSUPVMMR0REQHDR pReqHdr)
{
    AssertMsgFailed(("%d\n", uOperation)); NOREF(pVMR0); NOREF(uOperation); NOREF(u64Arg); NOREF(pReqHdr);
    return VERR_NOT_SUPPORTED;
}


SUPR3DECL(int) SUPR3CallVMMR0Fast(PVMR0 pVMR0, unsigned uOperation, VMCPUID idCpu)
{
    NOREF(pVMR0);
    static const uintptr_t s_auFunctions[3] =
    {
        SUP_IOCTL_FAST_DO_HM_RUN,
        SUP_IOCTL_FAST_DO_NEM_RUN,
        SUP_IOCTL_FAST_DO_NOP,
    };
    AssertCompile(SUP_VMMR0_DO_HM_RUN  == 0);
    AssertCompile(SUP_VMMR0_DO_NEM_RUN == 1);
    AssertCompile(SUP_VMMR0_DO_NOP     == 2);
    AssertMsgReturn(uOperation < RT_ELEMENTS(s_auFunctions), ("%#x\n", uOperation), VERR_INTERNAL_ERROR);
    return suplibOsIOCtlFast(&g_supLibData, s_auFunctions[uOperation], idCpu);
}


SUPR3DECL(int) SUPR3CallVMMR0Ex(PVMR0 pVMR0, VMCPUID idCpu, unsigned uOperation, uint64_t u64Arg, PSUPVMMR0REQHDR pReqHdr)
{
    /*
     * The following operations don't belong here.
     */
    AssertMsgReturn(    uOperation != SUP_VMMR0_DO_HM_RUN
                    &&  uOperation != SUP_VMMR0_DO_NEM_RUN
                    &&  uOperation != SUP_VMMR0_DO_NOP,
                    ("%#x\n", uOperation),
                    VERR_INTERNAL_ERROR);

    /* fake */
    if (RT_UNLIKELY(g_uSupFakeMode))
        return supCallVMMR0ExFake(pVMR0, uOperation, u64Arg, pReqHdr);

    int rc;
    if (!pReqHdr)
    {
        /* no data. */
        SUPCALLVMMR0 Req;
        Req.Hdr.u32Cookie = g_u32Cookie;
        Req.Hdr.u32SessionCookie = g_u32SessionCookie;
        Req.Hdr.cbIn = SUP_IOCTL_CALL_VMMR0_SIZE_IN(0);
        Req.Hdr.cbOut = SUP_IOCTL_CALL_VMMR0_SIZE_OUT(0);
        Req.Hdr.fFlags = SUPREQHDR_FLAGS_DEFAULT;
        Req.Hdr.rc = VERR_INTERNAL_ERROR;
        Req.u.In.pVMR0 = pVMR0;
        Req.u.In.idCpu = idCpu;
        Req.u.In.uOperation = uOperation;
        Req.u.In.u64Arg = u64Arg;
        rc = suplibOsIOCtl(&g_supLibData, SUP_IOCTL_CALL_VMMR0(0), &Req, SUP_IOCTL_CALL_VMMR0_SIZE(0));
        if (RT_SUCCESS(rc))
            rc = Req.Hdr.rc;
    }
    else if (SUP_IOCTL_CALL_VMMR0_SIZE(pReqHdr->cbReq) < _4K) /* FreeBSD won't copy more than 4K. */
    {
        AssertPtrReturn(pReqHdr, VERR_INVALID_POINTER);
        AssertReturn(pReqHdr->u32Magic == SUPVMMR0REQHDR_MAGIC, VERR_INVALID_MAGIC);
        const size_t cbReq = pReqHdr->cbReq;

        PSUPCALLVMMR0 pReq = (PSUPCALLVMMR0)alloca(SUP_IOCTL_CALL_VMMR0_SIZE(cbReq));
        pReq->Hdr.u32Cookie = g_u32Cookie;
        pReq->Hdr.u32SessionCookie = g_u32SessionCookie;
        pReq->Hdr.cbIn = SUP_IOCTL_CALL_VMMR0_SIZE_IN(cbReq);
        pReq->Hdr.cbOut = SUP_IOCTL_CALL_VMMR0_SIZE_OUT(cbReq);
        pReq->Hdr.fFlags = SUPREQHDR_FLAGS_DEFAULT;
        pReq->Hdr.rc = VERR_INTERNAL_ERROR;
        pReq->u.In.pVMR0 = pVMR0;
        pReq->u.In.idCpu = idCpu;
        pReq->u.In.uOperation = uOperation;
        pReq->u.In.u64Arg = u64Arg;
        memcpy(&pReq->abReqPkt[0], pReqHdr, cbReq);
        rc = suplibOsIOCtl(&g_supLibData, SUP_IOCTL_CALL_VMMR0(cbReq), pReq, SUP_IOCTL_CALL_VMMR0_SIZE(cbReq));
        if (RT_SUCCESS(rc))
            rc = pReq->Hdr.rc;
        memcpy(pReqHdr, &pReq->abReqPkt[0], cbReq);
    }
    else if (pReqHdr->cbReq <= _512K)
    {
        AssertPtrReturn(pReqHdr, VERR_INVALID_POINTER);
        AssertReturn(pReqHdr->u32Magic == SUPVMMR0REQHDR_MAGIC, VERR_INVALID_MAGIC);
        const size_t cbReq = pReqHdr->cbReq;

        PSUPCALLVMMR0 pReq = (PSUPCALLVMMR0)RTMemTmpAlloc(SUP_IOCTL_CALL_VMMR0_BIG_SIZE(cbReq));
        pReq->Hdr.u32Cookie         = g_u32Cookie;
        pReq->Hdr.u32SessionCookie  = g_u32SessionCookie;
        pReq->Hdr.cbIn              = SUP_IOCTL_CALL_VMMR0_BIG_SIZE_IN(cbReq);
        pReq->Hdr.cbOut             = SUP_IOCTL_CALL_VMMR0_BIG_SIZE_OUT(cbReq);
        pReq->Hdr.fFlags            = SUPREQHDR_FLAGS_DEFAULT;
        pReq->Hdr.rc                = VERR_INTERNAL_ERROR;
        pReq->u.In.pVMR0            = pVMR0;
        pReq->u.In.idCpu            = idCpu;
        pReq->u.In.uOperation       = uOperation;
        pReq->u.In.u64Arg           = u64Arg;
        memcpy(&pReq->abReqPkt[0], pReqHdr, cbReq);
        rc = suplibOsIOCtl(&g_supLibData, SUP_IOCTL_CALL_VMMR0_BIG, pReq, SUP_IOCTL_CALL_VMMR0_BIG_SIZE(cbReq));
        if (RT_SUCCESS(rc))
            rc = pReq->Hdr.rc;
        memcpy(pReqHdr, &pReq->abReqPkt[0], cbReq);
        RTMemTmpFree(pReq);
    }
    else
        AssertMsgFailedReturn(("cbReq=%#x\n", pReqHdr->cbReq), VERR_OUT_OF_RANGE);
    return rc;
}


SUPR3DECL(int) SUPR3CallVMMR0(PVMR0 pVMR0, VMCPUID idCpu, unsigned uOperation, void *pvArg)
{
    /*
     * The following operations don't belong here.
     */
    AssertMsgReturn(    uOperation != SUP_VMMR0_DO_HM_RUN
                    &&  uOperation != SUP_VMMR0_DO_NEM_RUN
                    &&  uOperation != SUP_VMMR0_DO_NOP,
                    ("%#x\n", uOperation),
                    VERR_INTERNAL_ERROR);
    return SUPR3CallVMMR0Ex(pVMR0, idCpu, uOperation, (uintptr_t)pvArg, NULL);
}


SUPR3DECL(int) SUPR3SetVMForFastIOCtl(PVMR0 pVMR0)
{
    if (RT_UNLIKELY(g_uSupFakeMode))
        return VINF_SUCCESS;

    SUPSETVMFORFAST Req;
    Req.Hdr.u32Cookie = g_u32Cookie;
    Req.Hdr.u32SessionCookie = g_u32SessionCookie;
    Req.Hdr.cbIn = SUP_IOCTL_SET_VM_FOR_FAST_SIZE_IN;
    Req.Hdr.cbOut = SUP_IOCTL_SET_VM_FOR_FAST_SIZE_OUT;
    Req.Hdr.fFlags = SUPREQHDR_FLAGS_DEFAULT;
    Req.Hdr.rc = VERR_INTERNAL_ERROR;
    Req.u.In.pVMR0 = pVMR0;
    int rc = suplibOsIOCtl(&g_supLibData, SUP_IOCTL_SET_VM_FOR_FAST, &Req, SUP_IOCTL_SET_VM_FOR_FAST_SIZE);
    if (RT_SUCCESS(rc))
        rc = Req.Hdr.rc;
    return rc;
}


SUPR3DECL(int) SUPR3CallR0Service(const char *pszService, size_t cchService, uint32_t uOperation, uint64_t u64Arg, PSUPR0SERVICEREQHDR pReqHdr)
{
    AssertReturn(cchService < RT_SIZEOFMEMB(SUPCALLSERVICE, u.In.szName), VERR_INVALID_PARAMETER);
    Assert(strlen(pszService) == cchService);

    /* fake */
    if (RT_UNLIKELY(g_uSupFakeMode))
        return VERR_NOT_SUPPORTED;

    int rc;
    if (!pReqHdr)
    {
        /* no data. */
        SUPCALLSERVICE Req;
        Req.Hdr.u32Cookie = g_u32Cookie;
        Req.Hdr.u32SessionCookie = g_u32SessionCookie;
        Req.Hdr.cbIn = SUP_IOCTL_CALL_SERVICE_SIZE_IN(0);
        Req.Hdr.cbOut = SUP_IOCTL_CALL_SERVICE_SIZE_OUT(0);
        Req.Hdr.fFlags = SUPREQHDR_FLAGS_DEFAULT;
        Req.Hdr.rc = VERR_INTERNAL_ERROR;
        memcpy(Req.u.In.szName, pszService, cchService);
        Req.u.In.szName[cchService] = '\0';
        Req.u.In.uOperation = uOperation;
        Req.u.In.u64Arg = u64Arg;
        rc = suplibOsIOCtl(&g_supLibData, SUP_IOCTL_CALL_SERVICE(0), &Req, SUP_IOCTL_CALL_SERVICE_SIZE(0));
        if (RT_SUCCESS(rc))
            rc = Req.Hdr.rc;
    }
    else if (SUP_IOCTL_CALL_SERVICE_SIZE(pReqHdr->cbReq) < _4K) /* FreeBSD won't copy more than 4K. */
    {
        AssertPtrReturn(pReqHdr, VERR_INVALID_POINTER);
        AssertReturn(pReqHdr->u32Magic == SUPR0SERVICEREQHDR_MAGIC, VERR_INVALID_MAGIC);
        const size_t cbReq = pReqHdr->cbReq;

        PSUPCALLSERVICE pReq = (PSUPCALLSERVICE)alloca(SUP_IOCTL_CALL_SERVICE_SIZE(cbReq));
        pReq->Hdr.u32Cookie = g_u32Cookie;
        pReq->Hdr.u32SessionCookie = g_u32SessionCookie;
        pReq->Hdr.cbIn = SUP_IOCTL_CALL_SERVICE_SIZE_IN(cbReq);
        pReq->Hdr.cbOut = SUP_IOCTL_CALL_SERVICE_SIZE_OUT(cbReq);
        pReq->Hdr.fFlags = SUPREQHDR_FLAGS_DEFAULT;
        pReq->Hdr.rc = VERR_INTERNAL_ERROR;
        memcpy(pReq->u.In.szName, pszService, cchService);
        pReq->u.In.szName[cchService] = '\0';
        pReq->u.In.uOperation = uOperation;
        pReq->u.In.u64Arg = u64Arg;
        memcpy(&pReq->abReqPkt[0], pReqHdr, cbReq);
        rc = suplibOsIOCtl(&g_supLibData, SUP_IOCTL_CALL_SERVICE(cbReq), pReq, SUP_IOCTL_CALL_SERVICE_SIZE(cbReq));
        if (RT_SUCCESS(rc))
            rc = pReq->Hdr.rc;
        memcpy(pReqHdr, &pReq->abReqPkt[0], cbReq);
    }
    else /** @todo may have to remove the size limits one this request... */
        AssertMsgFailedReturn(("cbReq=%#x\n", pReqHdr->cbReq), VERR_INTERNAL_ERROR);
    return rc;
}


/**
 * Worker for the SUPR3Logger* APIs.
 *
 * @returns VBox status code.
 * @param   enmWhich    Which logger.
 * @param   fWhat       What to do with the logger.
 * @param   pszFlags    The flags settings.
 * @param   pszGroups   The groups settings.
 * @param   pszDest     The destination specificier.
 */
static int supR3LoggerSettings(SUPLOGGER enmWhich, uint32_t fWhat, const char *pszFlags, const char *pszGroups, const char *pszDest)
{
    uint32_t const cchFlags  = pszFlags  ? (uint32_t)strlen(pszFlags)  : 0;
    uint32_t const cchGroups = pszGroups ? (uint32_t)strlen(pszGroups) : 0;
    uint32_t const cchDest   = pszDest   ? (uint32_t)strlen(pszDest)   : 0;
    uint32_t const cbStrTab  = cchFlags  + !!cchFlags
                             + cchGroups + !!cchGroups
                             + cchDest   + !!cchDest
                             + (!cchFlags && !cchGroups && !cchDest);

    PSUPLOGGERSETTINGS pReq  = (PSUPLOGGERSETTINGS)alloca(SUP_IOCTL_LOGGER_SETTINGS_SIZE(cbStrTab));
    pReq->Hdr.u32Cookie = g_u32Cookie;
    pReq->Hdr.u32SessionCookie = g_u32SessionCookie;
    pReq->Hdr.cbIn  = SUP_IOCTL_LOGGER_SETTINGS_SIZE_IN(cbStrTab);
    pReq->Hdr.cbOut = SUP_IOCTL_LOGGER_SETTINGS_SIZE_OUT;
    pReq->Hdr.fFlags= SUPREQHDR_FLAGS_DEFAULT;
    pReq->Hdr.rc    = VERR_INTERNAL_ERROR;
    switch (enmWhich)
    {
        case SUPLOGGER_DEBUG:   pReq->u.In.fWhich = SUPLOGGERSETTINGS_WHICH_DEBUG; break;
        case SUPLOGGER_RELEASE: pReq->u.In.fWhich = SUPLOGGERSETTINGS_WHICH_RELEASE; break;
        default:
            return VERR_INVALID_PARAMETER;
    }
    pReq->u.In.fWhat = fWhat;

    uint32_t off = 0;
    if (cchFlags)
    {
        pReq->u.In.offFlags = off;
        memcpy(&pReq->u.In.szStrings[off], pszFlags, cchFlags + 1);
        off += cchFlags + 1;
    }
    else
        pReq->u.In.offFlags = cbStrTab - 1;

    if (cchGroups)
    {
        pReq->u.In.offGroups = off;
        memcpy(&pReq->u.In.szStrings[off], pszGroups, cchGroups + 1);
        off += cchGroups + 1;
    }
    else
        pReq->u.In.offGroups = cbStrTab - 1;

    if (cchDest)
    {
        pReq->u.In.offDestination = off;
        memcpy(&pReq->u.In.szStrings[off], pszDest, cchDest + 1);
        off += cchDest + 1;
    }
    else
        pReq->u.In.offDestination = cbStrTab - 1;

    if (!off)
    {
        pReq->u.In.szStrings[0] = '\0';
        off++;
    }
    Assert(off == cbStrTab);
    Assert(pReq->u.In.szStrings[cbStrTab - 1] == '\0');


    int rc = suplibOsIOCtl(&g_supLibData, SUP_IOCTL_LOGGER_SETTINGS(cbStrTab), pReq, SUP_IOCTL_LOGGER_SETTINGS_SIZE(cbStrTab));
    if (RT_SUCCESS(rc))
        rc = pReq->Hdr.rc;
    return rc;
}


SUPR3DECL(int) SUPR3LoggerSettings(SUPLOGGER enmWhich, const char *pszFlags, const char *pszGroups, const char *pszDest)
{
    return supR3LoggerSettings(enmWhich, SUPLOGGERSETTINGS_WHAT_SETTINGS, pszFlags, pszGroups, pszDest);
}


SUPR3DECL(int) SUPR3LoggerCreate(SUPLOGGER enmWhich, const char *pszFlags, const char *pszGroups, const char *pszDest)
{
    return supR3LoggerSettings(enmWhich, SUPLOGGERSETTINGS_WHAT_CREATE, pszFlags, pszGroups, pszDest);
}


SUPR3DECL(int) SUPR3LoggerDestroy(SUPLOGGER enmWhich)
{
    return supR3LoggerSettings(enmWhich, SUPLOGGERSETTINGS_WHAT_DESTROY, NULL, NULL, NULL);
}


SUPR3DECL(int) SUPR3PageAlloc(size_t cPages, uint32_t fFlags, void **ppvPages)
{
    /*
     * Validate.
     */
    AssertPtrReturn(ppvPages, VERR_INVALID_POINTER);
    *ppvPages = NULL;
    AssertReturn(cPages > 0, VERR_PAGE_COUNT_OUT_OF_RANGE);
    AssertReturn(!(fFlags & ~SUP_PAGE_ALLOC_F_VALID_MASK), VERR_INVALID_FLAGS);

    /*
     * Call OS specific worker.
     */
    return suplibOsPageAlloc(&g_supLibData, cPages, fFlags, ppvPages);
}


SUPR3DECL(int) SUPR3PageFree(void *pvPages, size_t cPages)
{
    /*
     * Validate.
     */
    AssertPtrReturn(pvPages, VERR_INVALID_POINTER);
    AssertReturn(cPages > 0, VERR_PAGE_COUNT_OUT_OF_RANGE);

    /*
     * Call OS specific worker.
     */
    return suplibOsPageFree(&g_supLibData, pvPages, cPages);
}


/**
 * Locks down the physical memory backing a virtual memory
 * range in the current process.
 *
 * @returns VBox status code.
 * @param   pvStart         Start of virtual memory range.
 *                          Must be page aligned.
 * @param   cPages          Number of pages.
 * @param   paPages         Where to store the physical page addresses returned.
 *                          On entry this will point to an array of with cbMemory >> PAGE_SHIFT entries.
 */
SUPR3DECL(int) supR3PageLock(void *pvStart, size_t cPages, PSUPPAGE paPages)
{
    /*
     * Validate.
     */
    AssertPtr(pvStart);
    AssertMsg(RT_ALIGN_P(pvStart, PAGE_SIZE) == pvStart, ("pvStart (%p) must be page aligned\n", pvStart));
    AssertPtr(paPages);

    /* fake */
    if (RT_UNLIKELY(g_uSupFakeMode))
    {
        RTHCPHYS    Phys = (uintptr_t)pvStart + PAGE_SIZE * 1024;
        size_t      iPage = cPages;
        while (iPage-- > 0)
            paPages[iPage].Phys = Phys + (iPage << PAGE_SHIFT);
        return VINF_SUCCESS;
    }

    /*
     * Issue IOCtl to the SUPDRV kernel module.
     */
    int rc;
    PSUPPAGELOCK pReq = (PSUPPAGELOCK)RTMemTmpAllocZ(SUP_IOCTL_PAGE_LOCK_SIZE(cPages));
    if (RT_LIKELY(pReq))
    {
        pReq->Hdr.u32Cookie = g_u32Cookie;
        pReq->Hdr.u32SessionCookie = g_u32SessionCookie;
        pReq->Hdr.cbIn = SUP_IOCTL_PAGE_LOCK_SIZE_IN;
        pReq->Hdr.cbOut = SUP_IOCTL_PAGE_LOCK_SIZE_OUT(cPages);
        pReq->Hdr.fFlags = SUPREQHDR_FLAGS_MAGIC | SUPREQHDR_FLAGS_EXTRA_OUT;
        pReq->Hdr.rc = VERR_INTERNAL_ERROR;
        pReq->u.In.pvR3 = pvStart;
        pReq->u.In.cPages = (uint32_t)cPages; AssertRelease(pReq->u.In.cPages == cPages);
        rc = suplibOsIOCtl(&g_supLibData, SUP_IOCTL_PAGE_LOCK, pReq, SUP_IOCTL_PAGE_LOCK_SIZE(cPages));
        if (RT_SUCCESS(rc))
            rc = pReq->Hdr.rc;
        if (RT_SUCCESS(rc))
        {
            for (uint32_t iPage = 0; iPage < cPages; iPage++)
            {
                paPages[iPage].uReserved = 0;
                paPages[iPage].Phys = pReq->u.Out.aPages[iPage];
                Assert(!(paPages[iPage].Phys & ~X86_PTE_PAE_PG_MASK));
            }
        }
        RTMemTmpFree(pReq);
    }
    else
        rc = VERR_NO_TMP_MEMORY;

    return rc;
}


/**
 * Releases locked down pages.
 *
 * @returns VBox status code.
 * @param   pvStart         Start of virtual memory range previously locked
 *                          down by SUPPageLock().
 */
SUPR3DECL(int) supR3PageUnlock(void *pvStart)
{
    /*
     * Validate.
     */
    AssertPtr(pvStart);
    AssertMsg(RT_ALIGN_P(pvStart, PAGE_SIZE) == pvStart, ("pvStart (%p) must be page aligned\n", pvStart));

    /* fake */
    if (RT_UNLIKELY(g_uSupFakeMode))
        return VINF_SUCCESS;

    /*
     * Issue IOCtl to the SUPDRV kernel module.
     */
    SUPPAGEUNLOCK Req;
    Req.Hdr.u32Cookie = g_u32Cookie;
    Req.Hdr.u32SessionCookie = g_u32SessionCookie;
    Req.Hdr.cbIn = SUP_IOCTL_PAGE_UNLOCK_SIZE_IN;
    Req.Hdr.cbOut = SUP_IOCTL_PAGE_UNLOCK_SIZE_OUT;
    Req.Hdr.fFlags = SUPREQHDR_FLAGS_DEFAULT;
    Req.Hdr.rc = VERR_INTERNAL_ERROR;
    Req.u.In.pvR3 = pvStart;
    int rc = suplibOsIOCtl(&g_supLibData, SUP_IOCTL_PAGE_UNLOCK, &Req, SUP_IOCTL_PAGE_UNLOCK_SIZE);
    if (RT_SUCCESS(rc))
        rc = Req.Hdr.rc;
    return rc;
}


SUPR3DECL(int) SUPR3LockDownLoader(PRTERRINFO pErrInfo)
{
    /* fake */
    if (RT_UNLIKELY(g_uSupFakeMode))
        return VINF_SUCCESS;

    /*
     * Lock down the module loader interface.
     */
    SUPREQHDR ReqHdr;
    ReqHdr.u32Cookie = g_u32Cookie;
    ReqHdr.u32SessionCookie = g_u32SessionCookie;
    ReqHdr.cbIn = SUP_IOCTL_LDR_LOCK_DOWN_SIZE_IN;
    ReqHdr.cbOut = SUP_IOCTL_LDR_LOCK_DOWN_SIZE_OUT;
    ReqHdr.fFlags = SUPREQHDR_FLAGS_DEFAULT;
    ReqHdr.rc = VERR_INTERNAL_ERROR;
    int rc = suplibOsIOCtl(&g_supLibData, SUP_IOCTL_LDR_LOCK_DOWN, &ReqHdr, SUP_IOCTL_LDR_LOCK_DOWN_SIZE);
    if (RT_FAILURE(rc))
        return RTErrInfoSetF(pErrInfo, rc,
                             "SUPR3LockDownLoader: SUP_IOCTL_LDR_LOCK_DOWN ioctl returned %Rrc", rc);

    return ReqHdr.rc;
}


/**
 * Fallback for SUPR3PageAllocEx on systems where RTR0MemObjPhysAllocNC isn't
 * supported.
 */
static int supPagePageAllocNoKernelFallback(size_t cPages, void **ppvPages, PSUPPAGE paPages)
{
    int rc = suplibOsPageAlloc(&g_supLibData, cPages, 0, ppvPages);
    if (RT_SUCCESS(rc))
    {
        Assert(ASMMemIsZero(*ppvPages, cPages << PAGE_SHIFT));
        if (!paPages)
            paPages = (PSUPPAGE)alloca(sizeof(paPages[0]) * cPages);
        rc = supR3PageLock(*ppvPages, cPages, paPages);
        if (RT_FAILURE(rc))
            suplibOsPageFree(&g_supLibData, *ppvPages, cPages);
    }
    return rc;
}


SUPR3DECL(int) SUPR3PageAllocEx(size_t cPages, uint32_t fFlags, void **ppvPages, PRTR0PTR pR0Ptr, PSUPPAGE paPages)
{
    /*
     * Validate.
     */
    AssertPtrReturn(ppvPages, VERR_INVALID_POINTER);
    *ppvPages = NULL;
    AssertPtrNullReturn(pR0Ptr, VERR_INVALID_POINTER);
    if (pR0Ptr)
        *pR0Ptr = NIL_RTR0PTR;
    AssertPtrNullReturn(paPages, VERR_INVALID_POINTER);
    AssertMsgReturn(cPages > 0 && cPages <= VBOX_MAX_ALLOC_PAGE_COUNT, ("cPages=%zu\n", cPages), VERR_PAGE_COUNT_OUT_OF_RANGE);
    AssertReturn(!fFlags, VERR_INVALID_FLAGS);

    /*
     * Deal with driverless mode first.
     */
    if (g_supLibData.fDriverless)
    {
        int rc = SUPR3PageAlloc(cPages, 0 /*fFlags*/, ppvPages);
        Assert(RT_FAILURE(rc) || ASMMemIsZero(*ppvPages, cPages << PAGE_SHIFT));
        if (pR0Ptr)
            *pR0Ptr = NIL_RTR0PTR;
        if (paPages)
            for (size_t iPage = 0; iPage < cPages; iPage++)
            {
                paPages[iPage].uReserved = 0;
                paPages[iPage].Phys      = NIL_RTHCPHYS;
            }
        return rc;
    }

    /* Check that we've got a kernel connection so rtMemSaferSupR3AllocPages
       can do fallback without first having to hit assertions. */
    if (g_supLibData.hDevice != SUP_HDEVICE_NIL)
    { /* likely */ }
    else
        return VERR_WRONG_ORDER;

    /*
     * Use fallback for non-R0 mapping?
     */
    if (    !pR0Ptr
        &&  !g_fSupportsPageAllocNoKernel)
        return supPagePageAllocNoKernelFallback(cPages, ppvPages, paPages);

    /*
     * Issue IOCtl to the SUPDRV kernel module.
     */
    int rc;
    PSUPPAGEALLOCEX pReq = (PSUPPAGEALLOCEX)RTMemTmpAllocZ(SUP_IOCTL_PAGE_ALLOC_EX_SIZE(cPages));
    if (pReq)
    {
        pReq->Hdr.u32Cookie = g_u32Cookie;
        pReq->Hdr.u32SessionCookie = g_u32SessionCookie;
        pReq->Hdr.cbIn = SUP_IOCTL_PAGE_ALLOC_EX_SIZE_IN;
        pReq->Hdr.cbOut = SUP_IOCTL_PAGE_ALLOC_EX_SIZE_OUT(cPages);
        pReq->Hdr.fFlags = SUPREQHDR_FLAGS_MAGIC | SUPREQHDR_FLAGS_EXTRA_OUT;
        pReq->Hdr.rc = VERR_INTERNAL_ERROR;
        pReq->u.In.cPages = (uint32_t)cPages; AssertRelease(pReq->u.In.cPages == cPages);
        pReq->u.In.fKernelMapping = pR0Ptr != NULL;
        pReq->u.In.fUserMapping = true;
        pReq->u.In.fReserved0 = false;
        pReq->u.In.fReserved1 = false;
        rc = suplibOsIOCtl(&g_supLibData, SUP_IOCTL_PAGE_ALLOC_EX, pReq, SUP_IOCTL_PAGE_ALLOC_EX_SIZE(cPages));
        if (RT_SUCCESS(rc))
        {
            rc = pReq->Hdr.rc;
            if (RT_SUCCESS(rc))
            {
                *ppvPages = pReq->u.Out.pvR3;
                if (pR0Ptr)
                {
                    *pR0Ptr = pReq->u.Out.pvR0;
                    Assert(ASMMemIsZero(pReq->u.Out.pvR3, cPages << PAGE_SHIFT));
#ifdef RT_OS_DARWIN /* HACK ALERT! */
                    supR3TouchPages(pReq->u.Out.pvR3, cPages);
#endif
                }
                else
                    RT_BZERO(pReq->u.Out.pvR3, cPages << PAGE_SHIFT);

                if (paPages)
                    for (size_t iPage = 0; iPage < cPages; iPage++)
                    {
                        paPages[iPage].uReserved = 0;
                        paPages[iPage].Phys = pReq->u.Out.aPages[iPage];
                        Assert(!(paPages[iPage].Phys & ~X86_PTE_PAE_PG_MASK));
                    }
            }
            else if (   rc == VERR_NOT_SUPPORTED
                     && !pR0Ptr)
            {
                g_fSupportsPageAllocNoKernel = false;
                rc = supPagePageAllocNoKernelFallback(cPages, ppvPages, paPages);
            }
        }

        RTMemTmpFree(pReq);
    }
    else
        rc = VERR_NO_TMP_MEMORY;
    return rc;

}


SUPR3DECL(int) SUPR3PageMapKernel(void *pvR3, uint32_t off, uint32_t cb, uint32_t fFlags, PRTR0PTR pR0Ptr)
{
    /*
     * Validate.
     */
    AssertPtrReturn(pvR3, VERR_INVALID_POINTER);
    AssertPtrReturn(pR0Ptr, VERR_INVALID_POINTER);
    Assert(!(off & PAGE_OFFSET_MASK));
    Assert(!(cb & PAGE_OFFSET_MASK) && cb);
    Assert(!fFlags);
    *pR0Ptr = NIL_RTR0PTR;

    /*
     * Not a valid operation in driverless mode.
     */
    AssertReturn(g_supLibData.fDriverless, VERR_SUP_DRIVERLESS);

    /*
     * Issue IOCtl to the SUPDRV kernel module.
     */
    SUPPAGEMAPKERNEL Req;
    Req.Hdr.u32Cookie = g_u32Cookie;
    Req.Hdr.u32SessionCookie = g_u32SessionCookie;
    Req.Hdr.cbIn = SUP_IOCTL_PAGE_MAP_KERNEL_SIZE_IN;
    Req.Hdr.cbOut = SUP_IOCTL_PAGE_MAP_KERNEL_SIZE_OUT;
    Req.Hdr.fFlags = SUPREQHDR_FLAGS_DEFAULT;
    Req.Hdr.rc = VERR_INTERNAL_ERROR;
    Req.u.In.pvR3 = pvR3;
    Req.u.In.offSub = off;
    Req.u.In.cbSub = cb;
    Req.u.In.fFlags = fFlags;
    int rc = suplibOsIOCtl(&g_supLibData, SUP_IOCTL_PAGE_MAP_KERNEL, &Req, SUP_IOCTL_PAGE_MAP_KERNEL_SIZE);
    if (RT_SUCCESS(rc))
        rc = Req.Hdr.rc;
    if (RT_SUCCESS(rc))
        *pR0Ptr = Req.u.Out.pvR0;
    return rc;
}


SUPR3DECL(int) SUPR3PageProtect(void *pvR3, RTR0PTR R0Ptr, uint32_t off, uint32_t cb, uint32_t fProt)
{
    /*
     * Validate.
     */
    AssertPtrReturn(pvR3, VERR_INVALID_POINTER);
    Assert(!(off & PAGE_OFFSET_MASK));
    Assert(!(cb & PAGE_OFFSET_MASK) && cb);
    AssertReturn(!(fProt & ~(RTMEM_PROT_NONE | RTMEM_PROT_READ | RTMEM_PROT_WRITE | RTMEM_PROT_EXEC)), VERR_INVALID_PARAMETER);

    /*
     * Deal with driverless mode first.
     */
    if (g_supLibData.fDriverless)
        return RTMemProtect((uint8_t *)pvR3 + off, cb, fProt);

    /*
     * Some OSes can do this from ring-3, so try that before we
     * issue the IOCtl to the SUPDRV kernel module.
     * (Yea, this isn't very nice, but just try get the job done for now.)
     */
#if !defined(RT_OS_SOLARIS)
    RTMemProtect((uint8_t *)pvR3 + off, cb, fProt);
#endif

    SUPPAGEPROTECT Req;
    Req.Hdr.u32Cookie = g_u32Cookie;
    Req.Hdr.u32SessionCookie = g_u32SessionCookie;
    Req.Hdr.cbIn = SUP_IOCTL_PAGE_PROTECT_SIZE_IN;
    Req.Hdr.cbOut = SUP_IOCTL_PAGE_PROTECT_SIZE_OUT;
    Req.Hdr.fFlags = SUPREQHDR_FLAGS_DEFAULT;
    Req.Hdr.rc = VERR_INTERNAL_ERROR;
    Req.u.In.pvR3 = pvR3;
    Req.u.In.pvR0 = R0Ptr;
    Req.u.In.offSub = off;
    Req.u.In.cbSub = cb;
    Req.u.In.fProt = fProt;
    int rc = suplibOsIOCtl(&g_supLibData, SUP_IOCTL_PAGE_PROTECT, &Req, SUP_IOCTL_PAGE_PROTECT_SIZE);
    if (RT_SUCCESS(rc))
        rc = Req.Hdr.rc;
    return rc;
}


SUPR3DECL(int) SUPR3PageFreeEx(void *pvPages, size_t cPages)
{
    /*
     * Validate.
     */
    AssertPtrReturn(pvPages, VERR_INVALID_POINTER);
    AssertReturn(cPages > 0, VERR_PAGE_COUNT_OUT_OF_RANGE);

    /*
     * Deal with driverless mode first.
     */
    if (g_supLibData.fDriverless)
    {
        SUPR3PageFree(pvPages, cPages);
        return VINF_SUCCESS;
    }

    /*
     * Try normal free first, then if it fails check if we're using the fallback
     * for the allocations without kernel mappings and attempt unlocking it.
     */
    NOREF(cPages);
    SUPPAGEFREE Req;
    Req.Hdr.u32Cookie = g_u32Cookie;
    Req.Hdr.u32SessionCookie = g_u32SessionCookie;
    Req.Hdr.cbIn = SUP_IOCTL_PAGE_FREE_SIZE_IN;
    Req.Hdr.cbOut = SUP_IOCTL_PAGE_FREE_SIZE_OUT;
    Req.Hdr.fFlags = SUPREQHDR_FLAGS_DEFAULT;
    Req.Hdr.rc = VERR_INTERNAL_ERROR;
    Req.u.In.pvR3 = pvPages;
    int rc = suplibOsIOCtl(&g_supLibData, SUP_IOCTL_PAGE_FREE, &Req, SUP_IOCTL_PAGE_FREE_SIZE);
    if (RT_SUCCESS(rc))
    {
        rc = Req.Hdr.rc;
        if (    rc == VERR_INVALID_PARAMETER
            &&  !g_fSupportsPageAllocNoKernel)
        {
            int rc2 = supR3PageUnlock(pvPages);
            if (RT_SUCCESS(rc2))
                rc = suplibOsPageFree(&g_supLibData, pvPages, cPages);
        }
    }
    return rc;
}


SUPR3DECL(void *) SUPR3ContAlloc(size_t cPages, PRTR0PTR pR0Ptr, PRTHCPHYS pHCPhys)
{
    /*
     * Validate.
     */
    AssertPtrReturn(pHCPhys, NULL);
    *pHCPhys = NIL_RTHCPHYS;
    AssertPtrNullReturn(pR0Ptr, NULL);
    if (pR0Ptr)
        *pR0Ptr = NIL_RTR0PTR;
    AssertPtrNullReturn(pHCPhys, NULL);
    AssertMsgReturn(cPages > 0 && cPages < 256, ("cPages=%d must be > 0 and < 256\n", cPages), NULL);

    /*
     * Deal with driverless mode first.
     */
    if (g_supLibData.fDriverless)
    {
        void *pvPages = NULL;
        int rc = SUPR3PageAlloc(cPages, 0 /*fFlags*/, &pvPages);
        if (pR0Ptr)
            *pR0Ptr = NIL_RTR0PTR;
        if (pHCPhys)
            *pHCPhys = NIL_RTHCPHYS;
        return RT_SUCCESS(rc) ? pvPages : NULL;
    }

    /*
     * Issue IOCtl to the SUPDRV kernel module.
     */
    SUPCONTALLOC Req;
    Req.Hdr.u32Cookie = g_u32Cookie;
    Req.Hdr.u32SessionCookie = g_u32SessionCookie;
    Req.Hdr.cbIn = SUP_IOCTL_CONT_ALLOC_SIZE_IN;
    Req.Hdr.cbOut = SUP_IOCTL_CONT_ALLOC_SIZE_OUT;
    Req.Hdr.fFlags = SUPREQHDR_FLAGS_DEFAULT;
    Req.Hdr.rc = VERR_INTERNAL_ERROR;
    Req.u.In.cPages = (uint32_t)cPages;
    int rc = suplibOsIOCtl(&g_supLibData, SUP_IOCTL_CONT_ALLOC, &Req, SUP_IOCTL_CONT_ALLOC_SIZE);
    if (    RT_SUCCESS(rc)
        &&  RT_SUCCESS(Req.Hdr.rc))
    {
        *pHCPhys = Req.u.Out.HCPhys;
        if (pR0Ptr)
            *pR0Ptr = Req.u.Out.pvR0;
#ifdef RT_OS_DARWIN /* HACK ALERT! */
        supR3TouchPages(Req.u.Out.pvR3, cPages);
#endif
        return Req.u.Out.pvR3;
    }

    return NULL;
}


SUPR3DECL(int) SUPR3ContFree(void *pv, size_t cPages)
{
    /*
     * Validate.
     */
    if (!pv)
        return VINF_SUCCESS;
    AssertPtrReturn(pv, VERR_INVALID_POINTER);
    AssertReturn(cPages > 0, VERR_PAGE_COUNT_OUT_OF_RANGE);

    /*
     * Deal with driverless mode first.
     */
    if (g_supLibData.fDriverless)
        return SUPR3PageFree(pv, cPages);

    /*
     * Issue IOCtl to the SUPDRV kernel module.
     */
    SUPCONTFREE Req;
    Req.Hdr.u32Cookie = g_u32Cookie;
    Req.Hdr.u32SessionCookie = g_u32SessionCookie;
    Req.Hdr.cbIn = SUP_IOCTL_CONT_FREE_SIZE_IN;
    Req.Hdr.cbOut = SUP_IOCTL_CONT_FREE_SIZE_OUT;
    Req.Hdr.fFlags = SUPREQHDR_FLAGS_DEFAULT;
    Req.Hdr.rc = VERR_INTERNAL_ERROR;
    Req.u.In.pvR3 = pv;
    int rc = suplibOsIOCtl(&g_supLibData, SUP_IOCTL_CONT_FREE, &Req, SUP_IOCTL_CONT_FREE_SIZE);
    if (RT_SUCCESS(rc))
        rc = Req.Hdr.rc;
    return rc;
}


SUPR3DECL(int) SUPR3LowAlloc(size_t cPages, void **ppvPages, PRTR0PTR ppvPagesR0, PSUPPAGE paPages)
{
    /*
     * Validate.
     */
    AssertPtrReturn(ppvPages, VERR_INVALID_POINTER);
    *ppvPages = NULL;
    AssertPtrReturn(paPages, VERR_INVALID_POINTER);
    AssertMsgReturn(cPages > 0 && cPages < 256, ("cPages=%d must be > 0 and < 256\n", cPages), VERR_PAGE_COUNT_OUT_OF_RANGE);

    /* fake */
    if (RT_UNLIKELY(g_uSupFakeMode))
    {
        *ppvPages = RTMemPageAllocZ((size_t)cPages * PAGE_SIZE);
        if (!*ppvPages)
            return VERR_NO_LOW_MEMORY;

        /* fake physical addresses. */
        RTHCPHYS    Phys = (uintptr_t)*ppvPages + PAGE_SIZE * 1024;
        size_t      iPage = cPages;
        while (iPage-- > 0)
            paPages[iPage].Phys = Phys + (iPage << PAGE_SHIFT);
        return VINF_SUCCESS;
    }

    /*
     * Issue IOCtl to the SUPDRV kernel module.
     */
    int rc;
    PSUPLOWALLOC pReq = (PSUPLOWALLOC)RTMemTmpAllocZ(SUP_IOCTL_LOW_ALLOC_SIZE(cPages));
    if (pReq)
    {
        pReq->Hdr.u32Cookie = g_u32Cookie;
        pReq->Hdr.u32SessionCookie = g_u32SessionCookie;
        pReq->Hdr.cbIn = SUP_IOCTL_LOW_ALLOC_SIZE_IN;
        pReq->Hdr.cbOut = SUP_IOCTL_LOW_ALLOC_SIZE_OUT(cPages);
        pReq->Hdr.fFlags = SUPREQHDR_FLAGS_MAGIC | SUPREQHDR_FLAGS_EXTRA_OUT;
        pReq->Hdr.rc = VERR_INTERNAL_ERROR;
        pReq->u.In.cPages = (uint32_t)cPages; AssertRelease(pReq->u.In.cPages == cPages);
        rc = suplibOsIOCtl(&g_supLibData, SUP_IOCTL_LOW_ALLOC, pReq, SUP_IOCTL_LOW_ALLOC_SIZE(cPages));
        if (RT_SUCCESS(rc))
            rc = pReq->Hdr.rc;
        if (RT_SUCCESS(rc))
        {
            *ppvPages = pReq->u.Out.pvR3;
            if (ppvPagesR0)
                *ppvPagesR0 = pReq->u.Out.pvR0;
            if (paPages)
                for (size_t iPage = 0; iPage < cPages; iPage++)
                {
                    paPages[iPage].uReserved = 0;
                    paPages[iPage].Phys = pReq->u.Out.aPages[iPage];
                    Assert(!(paPages[iPage].Phys & ~X86_PTE_PAE_PG_MASK));
                    Assert(paPages[iPage].Phys <= UINT32_C(0xfffff000));
                }
#ifdef RT_OS_DARWIN /* HACK ALERT! */
            supR3TouchPages(pReq->u.Out.pvR3, cPages);
#endif
        }
        RTMemTmpFree(pReq);
    }
    else
        rc = VERR_NO_TMP_MEMORY;

    return rc;
}


SUPR3DECL(int) SUPR3LowFree(void *pv, size_t cPages)
{
    /*
     * Validate.
     */
    if (!pv)
        return VINF_SUCCESS;
    AssertPtrReturn(pv, VERR_INVALID_POINTER);
    AssertReturn(cPages > 0, VERR_PAGE_COUNT_OUT_OF_RANGE);

    /* fake */
    if (RT_UNLIKELY(g_uSupFakeMode))
    {
        RTMemPageFree(pv, cPages * PAGE_SIZE);
        return VINF_SUCCESS;
    }

    /*
     * Issue IOCtl to the SUPDRV kernel module.
     */
    SUPCONTFREE Req;
    Req.Hdr.u32Cookie = g_u32Cookie;
    Req.Hdr.u32SessionCookie = g_u32SessionCookie;
    Req.Hdr.cbIn = SUP_IOCTL_LOW_FREE_SIZE_IN;
    Req.Hdr.cbOut = SUP_IOCTL_LOW_FREE_SIZE_OUT;
    Req.Hdr.fFlags = SUPREQHDR_FLAGS_DEFAULT;
    Req.Hdr.rc = VERR_INTERNAL_ERROR;
    Req.u.In.pvR3 = pv;
    int rc = suplibOsIOCtl(&g_supLibData, SUP_IOCTL_LOW_FREE, &Req, SUP_IOCTL_LOW_FREE_SIZE);
    if (RT_SUCCESS(rc))
        rc = Req.Hdr.rc;
    return rc;
}


SUPR3DECL(int) SUPR3HardenedVerifyInit(void)
{
#ifdef RT_OS_WINDOWS
    if (g_cInits == 0)
        return suplibOsHardenedVerifyInit();
#endif
    return VINF_SUCCESS;
}


SUPR3DECL(int) SUPR3HardenedVerifyTerm(void)
{
#ifdef RT_OS_WINDOWS
    if (g_cInits == 0)
        return suplibOsHardenedVerifyTerm();
#endif
    return VINF_SUCCESS;
}


SUPR3DECL(int) SUPR3HardenedVerifyFile(const char *pszFilename, const char *pszMsg, PRTFILE phFile)
{
    /*
     * Quick input validation.
     */
    AssertPtr(pszFilename);
    AssertPtr(pszMsg);
    AssertReturn(!phFile, VERR_NOT_IMPLEMENTED); /** @todo Implement this. The deal is that we make sure the
                                                     file is the same we verified after opening it. */
    RT_NOREF2(pszFilename, pszMsg);

    /*
     * Only do the actual check in hardened builds.
     */
#ifdef VBOX_WITH_HARDENING
    int rc = supR3HardenedVerifyFixedFile(pszFilename, false /* fFatal */);
    if (RT_FAILURE(rc))
        LogRel(("SUPR3HardenedVerifyFile: %s: Verification of \"%s\" failed, rc=%Rrc\n", pszMsg, pszFilename, rc));
    return rc;
#else
    return VINF_SUCCESS;
#endif
}


SUPR3DECL(int) SUPR3HardenedVerifySelf(const char *pszArgv0, bool fInternal, PRTERRINFO pErrInfo)
{
    /*
     * Quick input validation.
     */
    AssertPtr(pszArgv0);
    RTErrInfoClear(pErrInfo);

    /*
     * Get the executable image path as we need it for all the tests here.
     */
    char szExecPath[RTPATH_MAX];
    if (!RTProcGetExecutablePath(szExecPath, sizeof(szExecPath)))
        return RTErrInfoSet(pErrInfo, VERR_INTERNAL_ERROR_2, "RTProcGetExecutablePath failed");

    int rc;
    if (fInternal)
    {
        /*
         * Internal applications must be launched directly without any PATH
         * searching involved.
         */
        if (RTPathCompare(pszArgv0, szExecPath) != 0)
            return RTErrInfoSetF(pErrInfo, VERR_SUPLIB_INVALID_ARGV0_INTERNAL,
                                 "argv[0] does not match the executable image path: '%s' != '%s'", pszArgv0, szExecPath);

        /*
         * Internal applications must reside in or under the
         * RTPathAppPrivateArch directory.
         */
        char szAppPrivateArch[RTPATH_MAX];
        rc = RTPathAppPrivateArch(szAppPrivateArch, sizeof(szAppPrivateArch));
        if (RT_FAILURE(rc))
            return RTErrInfoSetF(pErrInfo, VERR_SUPLIB_INVALID_ARGV0_INTERNAL,
                                 "RTPathAppPrivateArch failed with rc=%Rrc", rc);
        size_t cchAppPrivateArch = strlen(szAppPrivateArch);
        if (   cchAppPrivateArch >= strlen(szExecPath)
            || !RTPATH_IS_SLASH(szExecPath[cchAppPrivateArch]))
            return RTErrInfoSet(pErrInfo, VERR_SUPLIB_INVALID_INTERNAL_APP_DIR,
                                "Internal executable does reside under RTPathAppPrivateArch");
        szExecPath[cchAppPrivateArch] = '\0';
        if (RTPathCompare(szExecPath, szAppPrivateArch) != 0)
            return RTErrInfoSet(pErrInfo, VERR_SUPLIB_INVALID_INTERNAL_APP_DIR,
                                "Internal executable does reside under RTPathAppPrivateArch");
        szExecPath[cchAppPrivateArch] = RTPATH_SLASH;
    }

#ifdef VBOX_WITH_HARDENING
    /*
     * Verify that the image file and parent directories are sane.
     */
    rc = supR3HardenedVerifyFile(szExecPath, RTHCUINTPTR_MAX, false /*fMaybe3rdParty*/, pErrInfo);
    if (RT_FAILURE(rc))
        return rc;
#endif

    return VINF_SUCCESS;
}


SUPR3DECL(int) SUPR3HardenedVerifyDir(const char *pszDirPath, bool fRecursive, bool fCheckFiles, PRTERRINFO pErrInfo)
{
    /*
     * Quick input validation
     */
    AssertPtr(pszDirPath);
    RTErrInfoClear(pErrInfo);

    /*
     * Only do the actual check in hardened builds.
     */
#ifdef VBOX_WITH_HARDENING
    int rc = supR3HardenedVerifyDir(pszDirPath, fRecursive, fCheckFiles, pErrInfo);
    if (RT_FAILURE(rc) && !RTErrInfoIsSet(pErrInfo))
        LogRel(("supR3HardenedVerifyDir: Verification of \"%s\" failed, rc=%Rrc\n", pszDirPath, rc));
    return rc;
#else
    NOREF(pszDirPath); NOREF(fRecursive); NOREF(fCheckFiles);
    return VINF_SUCCESS;
#endif
}


SUPR3DECL(int) SUPR3HardenedVerifyPlugIn(const char *pszFilename, PRTERRINFO pErrInfo)
{
    /*
     * Quick input validation
     */
    AssertPtr(pszFilename);
    RTErrInfoClear(pErrInfo);

    /*
     * Only do the actual check in hardened builds.
     */
#ifdef VBOX_WITH_HARDENING
    int rc = supR3HardenedVerifyFile(pszFilename, RTHCUINTPTR_MAX, true /*fMaybe3rdParty*/, pErrInfo);
    if (RT_FAILURE(rc) && !RTErrInfoIsSet(pErrInfo))
        LogRel(("supR3HardenedVerifyFile: Verification of \"%s\" failed, rc=%Rrc\n", pszFilename, rc));
    return rc;
#else
    RT_NOREF1(pszFilename);
    return VINF_SUCCESS;
#endif
}


SUPR3DECL(int) SUPR3GipGetPhys(PRTHCPHYS pHCPhys)
{
    if (g_pSUPGlobalInfoPage)
    {
        *pHCPhys = g_HCPhysSUPGlobalInfoPage;
        return VINF_SUCCESS;
    }
    *pHCPhys = NIL_RTHCPHYS;
    return VERR_WRONG_ORDER;
}


SUPR3DECL(int) SUPR3QueryVTxSupported(const char **ppszWhy)
{
    *ppszWhy = NULL;
#ifdef RT_OS_LINUX
    return suplibOsQueryVTxSupported(ppszWhy);
#else
    return VINF_SUCCESS;
#endif
}


SUPR3DECL(int) SUPR3QueryVTCaps(uint32_t *pfCaps)
{
    AssertPtrReturn(pfCaps, VERR_INVALID_POINTER);

    *pfCaps = 0;

    int rc;
    if (!g_supLibData.fDriverless)
    {
        /*
         * Issue IOCtl to the SUPDRV kernel module.
         */
        SUPVTCAPS Req;
        Req.Hdr.u32Cookie = g_u32Cookie;
        Req.Hdr.u32SessionCookie = g_u32SessionCookie;
        Req.Hdr.cbIn = SUP_IOCTL_VT_CAPS_SIZE_IN;
        Req.Hdr.cbOut = SUP_IOCTL_VT_CAPS_SIZE_OUT;
        Req.Hdr.fFlags = SUPREQHDR_FLAGS_DEFAULT;
        Req.Hdr.rc = VERR_INTERNAL_ERROR;
        Req.u.Out.fCaps = 0;
        rc = suplibOsIOCtl(&g_supLibData, SUP_IOCTL_VT_CAPS, &Req, SUP_IOCTL_VT_CAPS_SIZE);
        if (RT_SUCCESS(rc))
        {
            rc = Req.Hdr.rc;
            if (RT_SUCCESS(rc))
                *pfCaps = Req.u.Out.fCaps;
        }
    }
    /*
     * Fail this call in driverless mode.
     */
    else
        rc = VERR_SUP_DRIVERLESS;
    return rc;
}


SUPR3DECL(bool) SUPR3IsNemSupportedWhenNoVtxOrAmdV(void)
{
#if defined(RT_OS_WINDOWS) || defined(RT_OS_DARWIN)
    return suplibOsIsNemSupportedWhenNoVtxOrAmdV();
#else
    return false;
#endif
}


SUPR3DECL(int) SUPR3QueryMicrocodeRev(uint32_t *puMicrocodeRev)
{
    AssertPtrReturn(puMicrocodeRev, VERR_INVALID_POINTER);

    *puMicrocodeRev = 0;

    int rc;
    if (!g_supLibData.fDriverless)
    {
        /*
         * Issue IOCtl to the SUPDRV kernel module.
         */
        SUPUCODEREV Req;
        Req.Hdr.u32Cookie = g_u32Cookie;
        Req.Hdr.u32SessionCookie = g_u32SessionCookie;
        Req.Hdr.cbIn = SUP_IOCTL_UCODE_REV_SIZE_IN;
        Req.Hdr.cbOut = SUP_IOCTL_UCODE_REV_SIZE_OUT;
        Req.Hdr.fFlags = SUPREQHDR_FLAGS_DEFAULT;
        Req.Hdr.rc = VERR_INTERNAL_ERROR;
        Req.u.Out.MicrocodeRev = 0;
        rc = suplibOsIOCtl(&g_supLibData, SUP_IOCTL_UCODE_REV, &Req, SUP_IOCTL_UCODE_REV_SIZE);
        if (RT_SUCCESS(rc))
        {
            rc = Req.Hdr.rc;
            if (RT_SUCCESS(rc))
                *puMicrocodeRev = Req.u.Out.MicrocodeRev;
        }
    }
    /*
     * Just fail the call in driverless mode if there is a host specific way of
     * getting the information.
     */
    else
#if defined(RT_OS_DARWIN) && defined(RT_ARCH_AMD64)
        rc = suplibOsQueryMicrocodeRev(puMicrocodeRev);
#else
        rc = VERR_SUP_DRIVERLESS;
#endif
    return rc;
}


SUPR3DECL(int) SUPR3TracerOpen(uint32_t uCookie, uintptr_t uArg)
{
    /* fake */
    if (RT_UNLIKELY(g_uSupFakeMode))
        return VINF_SUCCESS;

    /*
     * Issue IOCtl to the SUPDRV kernel module.
     */
    SUPTRACEROPEN Req;
    Req.Hdr.u32Cookie       = g_u32Cookie;
    Req.Hdr.u32SessionCookie= g_u32SessionCookie;
    Req.Hdr.cbIn            = SUP_IOCTL_TRACER_OPEN_SIZE_IN;
    Req.Hdr.cbOut           = SUP_IOCTL_TRACER_OPEN_SIZE_OUT;
    Req.Hdr.fFlags          = SUPREQHDR_FLAGS_DEFAULT;
    Req.Hdr.rc              = VERR_INTERNAL_ERROR;
    Req.u.In.uCookie        = uCookie;
    Req.u.In.uArg           = uArg;
    int rc = suplibOsIOCtl(&g_supLibData, SUP_IOCTL_TRACER_OPEN, &Req, SUP_IOCTL_TRACER_OPEN_SIZE);
    if (RT_SUCCESS(rc))
        rc = Req.Hdr.rc;
    return rc;
}


SUPR3DECL(int) SUPR3TracerClose(void)
{
    /* fake */
    if (RT_UNLIKELY(g_uSupFakeMode))
        return VINF_SUCCESS;

    /*
     * Issue IOCtl to the SUPDRV kernel module.
     */
    SUPREQHDR Req;
    Req.u32Cookie       = g_u32Cookie;
    Req.u32SessionCookie= g_u32SessionCookie;
    Req.cbIn            = SUP_IOCTL_TRACER_OPEN_SIZE_IN;
    Req.cbOut           = SUP_IOCTL_TRACER_OPEN_SIZE_OUT;
    Req.fFlags          = SUPREQHDR_FLAGS_DEFAULT;
    Req.rc              = VERR_INTERNAL_ERROR;
    int rc = suplibOsIOCtl(&g_supLibData, SUP_IOCTL_TRACER_CLOSE, &Req, SUP_IOCTL_TRACER_CLOSE_SIZE);
    if (RT_SUCCESS(rc))
        rc = Req.rc;
    return rc;
}


SUPR3DECL(int) SUPR3TracerIoCtl(uintptr_t uCmd, uintptr_t uArg, int32_t *piRetVal)
{
    /* fake */
    if (RT_UNLIKELY(g_uSupFakeMode))
    {
        *piRetVal = -1;
        return VERR_NOT_SUPPORTED;
    }

    /*
     * Issue IOCtl to the SUPDRV kernel module.
     */
    SUPTRACERIOCTL Req;
    Req.Hdr.u32Cookie       = g_u32Cookie;
    Req.Hdr.u32SessionCookie= g_u32SessionCookie;
    Req.Hdr.cbIn            = SUP_IOCTL_TRACER_IOCTL_SIZE_IN;
    Req.Hdr.cbOut           = SUP_IOCTL_TRACER_IOCTL_SIZE_OUT;
    Req.Hdr.fFlags          = SUPREQHDR_FLAGS_DEFAULT;
    Req.Hdr.rc              = VERR_INTERNAL_ERROR;
    Req.u.In.uCmd           = uCmd;
    Req.u.In.uArg           = uArg;
    int rc = suplibOsIOCtl(&g_supLibData, SUP_IOCTL_TRACER_IOCTL, &Req, SUP_IOCTL_TRACER_IOCTL_SIZE);
    if (RT_SUCCESS(rc))
    {
        rc = Req.Hdr.rc;
        *piRetVal = Req.u.Out.iRetVal;
    }
    return rc;
}



typedef struct SUPDRVTRACERSTRTAB
{
    /** Pointer to the string table. */
    char       *pchStrTab;
    /** The actual string table size. */
    uint32_t    cbStrTab;
    /** The original string pointers. */
    RTUINTPTR   apszOrgFunctions[1];
} SUPDRVTRACERSTRTAB, *PSUPDRVTRACERSTRTAB;


/**
 * Destroys a string table, restoring the original pszFunction member valus.
 *
 * @param   pThis               The string table structure.
 * @param   paProbeLocs32       The probe location array, 32-bit type variant.
 * @param   paProbeLocs64       The probe location array, 64-bit type variant.
 * @param   cProbeLocs          The number of elements in the array.
 * @param   f32Bit              Set if @a paProbeLocs32 should be used, when
 *                              clear use @a paProbeLocs64.
 */
static void supr3TracerDestroyStrTab(PSUPDRVTRACERSTRTAB pThis, PVTGPROBELOC32 paProbeLocs32, PVTGPROBELOC64 paProbeLocs64,
                                     uint32_t cProbeLocs, bool f32Bit)
{
    /* Restore. */
    size_t i = cProbeLocs;
    if (f32Bit)
        while (i--)
            paProbeLocs32[i].pszFunction = (uint32_t)pThis->apszOrgFunctions[i];
    else
        while (i--)
            paProbeLocs64[i].pszFunction = pThis->apszOrgFunctions[i];

    /* Free. */
    RTMemFree(pThis->pchStrTab);
    RTMemFree(pThis);
}


/**
 * Creates a string table for the pszFunction members in the probe location
 * array.
 *
 * This will save and replace the pszFunction members with offsets.
 *
 * @returns Pointer to a string table structure.  NULL on failure.
 * @param   paProbeLocs32       The probe location array, 32-bit type variant.
 * @param   paProbeLocs64       The probe location array, 64-bit type variant.
 * @param   cProbeLocs          The number of elements in the array.
 * @param   offDelta            Relocation offset for the string pointers.
 * @param   f32Bit              Set if @a paProbeLocs32 should be used, when
 *                              clear use @a paProbeLocs64.
 */
static PSUPDRVTRACERSTRTAB supr3TracerCreateStrTab(PVTGPROBELOC32 paProbeLocs32,
                                                   PVTGPROBELOC64 paProbeLocs64,
                                                   uint32_t cProbeLocs,
                                                   RTUINTPTR offDelta,
                                                   bool f32Bit)
{
    if (cProbeLocs > _128K)
        return NULL;

    /*
     * Allocate the string table structures.
     */
    size_t              cbThis    = RT_UOFFSETOF_DYN(SUPDRVTRACERSTRTAB, apszOrgFunctions[cProbeLocs]);
    PSUPDRVTRACERSTRTAB pThis     = (PSUPDRVTRACERSTRTAB)RTMemAlloc(cbThis);
    if (!pThis)
        return NULL;

    uint32_t const      cHashBits = cProbeLocs * 2 - 1;
    uint32_t           *pbmHash   = (uint32_t *)RTMemAllocZ(RT_ALIGN_32(cHashBits, 64) / 8 );
    if (!pbmHash)
    {
        RTMemFree(pThis);
        return NULL;
    }

    /*
     * Calc the max string table size and save the orignal pointers so we can
     * replace them later.
     */
    size_t cbMax = 1;
    for (uint32_t i = 0; i < cProbeLocs; i++)
    {
        pThis->apszOrgFunctions[i] = f32Bit ? paProbeLocs32[i].pszFunction : paProbeLocs64[i].pszFunction;
        const char *pszFunction = (const char *)(uintptr_t)(pThis->apszOrgFunctions[i] + offDelta);
        size_t cch = strlen(pszFunction);
        if (cch > _1K)
        {
            cbMax = 0;
            break;
        }
        cbMax += cch + 1;
    }

    /* Alloc space for it. */
    if (cbMax > 0)
        pThis->pchStrTab = (char *)RTMemAlloc(cbMax);
    else
        pThis->pchStrTab = NULL;
    if (!pThis->pchStrTab)
    {
        RTMemFree(pbmHash);
        RTMemFree(pThis);
        return NULL;
    }

    /*
     * Create the string table.
     */
    uint32_t off = 0;
    uint32_t offPrev = 0;

    for (uint32_t i = 0; i < cProbeLocs; i++)
    {
        const char * const psz      = (const char *)(uintptr_t)(pThis->apszOrgFunctions[i] + offDelta);
        size_t       const cch      = strlen(psz);
        uint32_t     const iHashBit = RTStrHash1(psz) % cHashBits;
        if (ASMBitTestAndSet(pbmHash, iHashBit))
        {
            /* Often it's the most recent string. */
            if (   off - offPrev < cch + 1
                || memcmp(&pThis->pchStrTab[offPrev], psz, cch + 1))
            {
                /* It wasn't, search the entire string table. (lazy bird) */
                offPrev = 0;
                while (offPrev < off)
                {
                    size_t cchCur = strlen(&pThis->pchStrTab[offPrev]);
                    if (   cchCur == cch
                        && !memcmp(&pThis->pchStrTab[offPrev], psz, cch + 1))
                        break;
                    offPrev += (uint32_t)cchCur + 1;
                }
            }
        }
        else
            offPrev = off;

        /* Add the string to the table. */
        if (offPrev >= off)
        {
            memcpy(&pThis->pchStrTab[off], psz, cch + 1);
            offPrev = off;
            off += (uint32_t)cch + 1;
        }

        /* Update the entry */
        if (f32Bit)
            paProbeLocs32[i].pszFunction = offPrev;
        else
            paProbeLocs64[i].pszFunction = offPrev;
    }

    pThis->cbStrTab = off;
    RTMemFree(pbmHash);
    return pThis;
}



SUPR3DECL(int) SUPR3TracerRegisterModule(uintptr_t hModNative, const char *pszModule, struct VTGOBJHDR *pVtgHdr,
                                         RTUINTPTR uVtgHdrAddr, uint32_t fFlags)
{
    /* Validate input. */
    NOREF(hModNative);
    AssertPtrReturn(pVtgHdr, VERR_INVALID_POINTER);
    AssertReturn(!memcmp(pVtgHdr->szMagic, VTGOBJHDR_MAGIC, sizeof(pVtgHdr->szMagic)), VERR_SUPDRV_VTG_MAGIC);
    AssertPtrReturn(pszModule, VERR_INVALID_POINTER);
    size_t cchModule = strlen(pszModule);
    AssertReturn(cchModule < RT_SIZEOFMEMB(SUPTRACERUMODREG, u.In.szName), VERR_FILENAME_TOO_LONG);
    AssertReturn(!RTPathHavePath(pszModule), VERR_INVALID_PARAMETER);
    AssertReturn(fFlags == SUP_TRACER_UMOD_FLAGS_EXE || fFlags == SUP_TRACER_UMOD_FLAGS_SHARED, VERR_INVALID_PARAMETER);

    /*
     * Set the probe location array offset and size members. If the size is
     * zero, don't bother ring-0 with it.
     */
    if (!pVtgHdr->offProbeLocs)
    {
        uint64_t u64Tmp = pVtgHdr->uProbeLocsEnd.u64 - pVtgHdr->uProbeLocs.u64;
        if (u64Tmp >= UINT32_MAX)
            return VERR_SUPDRV_VTG_BAD_HDR_TOO_MUCH;
        pVtgHdr->cbProbeLocs  = (uint32_t)u64Tmp;

        u64Tmp = pVtgHdr->uProbeLocs.u64 - uVtgHdrAddr;
        if ((int64_t)u64Tmp != (int32_t)u64Tmp)
        {
            LogRel(("SUPR3TracerRegisterModule: VERR_SUPDRV_VTG_BAD_HDR_PTR - u64Tmp=%#llx uProbeLocs=%#llx uVtgHdrAddr=%RTptr\n",
                    u64Tmp, pVtgHdr->uProbeLocs.u64, uVtgHdrAddr));
            return VERR_SUPDRV_VTG_BAD_HDR_PTR;
        }
        pVtgHdr->offProbeLocs = (int32_t)u64Tmp;
    }

    if (   !pVtgHdr->cbProbeLocs
        || !pVtgHdr->cbProbes)
        return VINF_SUCCESS;

    /*
     * Fake out.
     */
    if (RT_UNLIKELY(g_uSupFakeMode))
        return VINF_SUCCESS;

    /*
     * Create a string table for the function names in the location array.
     * It's somewhat easier to do that here than from ring-0.
     */
    uint32_t const      cProbeLocs  = pVtgHdr->cbProbeLocs
                                    / (pVtgHdr->cBits == 32 ? sizeof(VTGPROBELOC32) : sizeof(VTGPROBELOC64));
    PVTGPROBELOC        paProbeLocs = (PVTGPROBELOC)((uintptr_t)pVtgHdr + pVtgHdr->offProbeLocs);
    PSUPDRVTRACERSTRTAB pStrTab     = supr3TracerCreateStrTab((PVTGPROBELOC32)paProbeLocs,
                                                              (PVTGPROBELOC64)paProbeLocs,
                                                              cProbeLocs, (uintptr_t)pVtgHdr - uVtgHdrAddr,
                                                              pVtgHdr->cBits == 32);
    if (!pStrTab)
        return VERR_NO_MEMORY;


    /*
     * Issue IOCtl to the SUPDRV kernel module.
     */
    SUPTRACERUMODREG Req;
    Req.Hdr.u32Cookie       = g_u32Cookie;
    Req.Hdr.u32SessionCookie= g_u32SessionCookie;
    Req.Hdr.cbIn            = SUP_IOCTL_TRACER_UMOD_REG_SIZE_IN;
    Req.Hdr.cbOut           = SUP_IOCTL_TRACER_UMOD_REG_SIZE_OUT;
    Req.Hdr.fFlags          = SUPREQHDR_FLAGS_DEFAULT;
    Req.Hdr.rc              = VERR_INTERNAL_ERROR;
    Req.u.In.uVtgHdrAddr    = uVtgHdrAddr;
    Req.u.In.R3PtrVtgHdr    = pVtgHdr;
    Req.u.In.R3PtrStrTab    = pStrTab->pchStrTab;
    Req.u.In.cbStrTab       = pStrTab->cbStrTab;
    Req.u.In.fFlags         = fFlags;

    memcpy(Req.u.In.szName, pszModule, cchModule + 1);
    if (!RTPathHasSuffix(Req.u.In.szName))
    {
        /* Add the default suffix if none is given. */
        switch (fFlags & SUP_TRACER_UMOD_FLAGS_TYPE_MASK)
        {
#if defined(RT_OS_WINDOWS) || defined(RT_OS_OS2)
            case SUP_TRACER_UMOD_FLAGS_EXE:
                if (cchModule + sizeof(".exe") <= sizeof(Req.u.In.szName))
                    strcpy(&Req.u.In.szName[cchModule], ".exe");
                break;
#endif

            case SUP_TRACER_UMOD_FLAGS_SHARED:
            {
                const char *pszSuff = RTLdrGetSuff();
                size_t      cchSuff = strlen(pszSuff);
                if (cchModule + cchSuff < sizeof(Req.u.In.szName))
                    memcpy(&Req.u.In.szName[cchModule], pszSuff, cchSuff + 1);
                break;
            }
        }
    }

    int rc = suplibOsIOCtl(&g_supLibData, SUP_IOCTL_TRACER_UMOD_REG, &Req, SUP_IOCTL_TRACER_UMOD_REG_SIZE);
    if (RT_SUCCESS(rc))
        rc = Req.Hdr.rc;

    supr3TracerDestroyStrTab(pStrTab, (PVTGPROBELOC32)paProbeLocs, (PVTGPROBELOC64)paProbeLocs,
                             cProbeLocs,  pVtgHdr->cBits == 32);
    return rc;
}


SUPR3DECL(int) SUPR3TracerDeregisterModule(struct VTGOBJHDR *pVtgHdr)
{
    /* Validate input. */
    AssertPtrReturn(pVtgHdr, VERR_INVALID_POINTER);
    AssertReturn(!memcmp(pVtgHdr->szMagic, VTGOBJHDR_MAGIC, sizeof(pVtgHdr->szMagic)), VERR_SUPDRV_VTG_MAGIC);

    /*
     * Don't bother if the object is empty.
     */
    if (   !pVtgHdr->cbProbeLocs
        || !pVtgHdr->cbProbes)
        return VINF_SUCCESS;

    /*
     * Fake out.
     */
    if (RT_UNLIKELY(g_uSupFakeMode))
        return VINF_SUCCESS;

    /*
     * Issue IOCtl to the SUPDRV kernel module.
     */
    SUPTRACERUMODDEREG Req;
    Req.Hdr.u32Cookie       = g_u32Cookie;
    Req.Hdr.u32SessionCookie= g_u32SessionCookie;
    Req.Hdr.cbIn            = SUP_IOCTL_TRACER_UMOD_REG_SIZE_IN;
    Req.Hdr.cbOut           = SUP_IOCTL_TRACER_UMOD_REG_SIZE_OUT;
    Req.Hdr.fFlags          = SUPREQHDR_FLAGS_DEFAULT;
    Req.Hdr.rc              = VERR_INTERNAL_ERROR;
    Req.u.In.pVtgHdr        = pVtgHdr;

    int rc = suplibOsIOCtl(&g_supLibData, SUP_IOCTL_TRACER_UMOD_DEREG, &Req, SUP_IOCTL_TRACER_UMOD_DEREG_SIZE);
    if (RT_SUCCESS(rc))
        rc = Req.Hdr.rc;
    return rc;
}


DECLASM(void) suplibTracerFireProbe(PVTGPROBELOC pProbeLoc, PSUPTRACERUMODFIREPROBE pReq)
{
    RT_NOREF1(pProbeLoc);

    pReq->Hdr.u32Cookie         = g_u32Cookie;
    pReq->Hdr.u32SessionCookie  = g_u32SessionCookie;
    Assert(pReq->Hdr.cbIn  == SUP_IOCTL_TRACER_UMOD_FIRE_PROBE_SIZE_IN);
    Assert(pReq->Hdr.cbOut == SUP_IOCTL_TRACER_UMOD_FIRE_PROBE_SIZE_OUT);
    pReq->Hdr.fFlags            = SUPREQHDR_FLAGS_DEFAULT;
    pReq->Hdr.rc                = VINF_SUCCESS;

    suplibOsIOCtl(&g_supLibData, SUP_IOCTL_TRACER_UMOD_FIRE_PROBE, pReq, SUP_IOCTL_TRACER_UMOD_FIRE_PROBE_SIZE);
}


SUPR3DECL(int) SUPR3MsrProberRead(uint32_t uMsr, RTCPUID idCpu, uint64_t *puValue, bool *pfGp)
{
    SUPMSRPROBER  Req;
    Req.Hdr.u32Cookie           = g_u32Cookie;
    Req.Hdr.u32SessionCookie    = g_u32SessionCookie;
    Req.Hdr.cbIn                = SUP_IOCTL_MSR_PROBER_SIZE_IN;
    Req.Hdr.cbOut               = SUP_IOCTL_MSR_PROBER_SIZE_OUT;
    Req.Hdr.fFlags              = SUPREQHDR_FLAGS_DEFAULT;
    Req.Hdr.rc                  = VERR_INTERNAL_ERROR;

    Req.u.In.enmOp              = SUPMSRPROBEROP_READ;
    Req.u.In.uMsr               = uMsr;
    Req.u.In.idCpu              = idCpu == NIL_RTCPUID ? UINT32_MAX : idCpu;

    int rc = suplibOsIOCtl(&g_supLibData, SUP_IOCTL_MSR_PROBER, &Req, SUP_IOCTL_MSR_PROBER_SIZE);
    if (RT_SUCCESS(rc))
        rc = Req.Hdr.rc;
    if (RT_SUCCESS(rc))
    {
        if (puValue)
            *puValue = Req.u.Out.uResults.Read.uValue;
        if (pfGp)
            *pfGp    = Req.u.Out.uResults.Read.fGp;
    }

    return rc;
}


SUPR3DECL(int) SUPR3MsrProberWrite(uint32_t uMsr, RTCPUID idCpu, uint64_t uValue, bool *pfGp)
{
    SUPMSRPROBER  Req;
    Req.Hdr.u32Cookie           = g_u32Cookie;
    Req.Hdr.u32SessionCookie    = g_u32SessionCookie;
    Req.Hdr.cbIn                = SUP_IOCTL_MSR_PROBER_SIZE_IN;
    Req.Hdr.cbOut               = SUP_IOCTL_MSR_PROBER_SIZE_OUT;
    Req.Hdr.fFlags              = SUPREQHDR_FLAGS_DEFAULT;
    Req.Hdr.rc                  = VERR_INTERNAL_ERROR;

    Req.u.In.enmOp                  = SUPMSRPROBEROP_WRITE;
    Req.u.In.uMsr                   = uMsr;
    Req.u.In.idCpu                  = idCpu == NIL_RTCPUID ? UINT32_MAX : idCpu;
    Req.u.In.uArgs.Write.uToWrite   = uValue;

    int rc = suplibOsIOCtl(&g_supLibData, SUP_IOCTL_MSR_PROBER, &Req, SUP_IOCTL_MSR_PROBER_SIZE);
    if (RT_SUCCESS(rc))
        rc = Req.Hdr.rc;
    if (RT_SUCCESS(rc) && pfGp)
        *pfGp = Req.u.Out.uResults.Write.fGp;

    return rc;
}


SUPR3DECL(int) SUPR3MsrProberModify(uint32_t uMsr, RTCPUID idCpu, uint64_t fAndMask, uint64_t fOrMask,
                                    PSUPMSRPROBERMODIFYRESULT pResult)
{
    return SUPR3MsrProberModifyEx(uMsr, idCpu, fAndMask, fOrMask, false /*fFaster*/, pResult);
}


SUPR3DECL(int) SUPR3MsrProberModifyEx(uint32_t uMsr, RTCPUID idCpu, uint64_t fAndMask, uint64_t fOrMask, bool fFaster,
                                      PSUPMSRPROBERMODIFYRESULT pResult)
{
    SUPMSRPROBER  Req;
    Req.Hdr.u32Cookie           = g_u32Cookie;
    Req.Hdr.u32SessionCookie    = g_u32SessionCookie;
    Req.Hdr.cbIn                = SUP_IOCTL_MSR_PROBER_SIZE_IN;
    Req.Hdr.cbOut               = SUP_IOCTL_MSR_PROBER_SIZE_OUT;
    Req.Hdr.fFlags              = SUPREQHDR_FLAGS_DEFAULT;
    Req.Hdr.rc                  = VERR_INTERNAL_ERROR;

    Req.u.In.enmOp                  = fFaster ? SUPMSRPROBEROP_MODIFY_FASTER : SUPMSRPROBEROP_MODIFY;
    Req.u.In.uMsr                   = uMsr;
    Req.u.In.idCpu                  = idCpu == NIL_RTCPUID ? UINT32_MAX : idCpu;
    Req.u.In.uArgs.Modify.fAndMask  = fAndMask;
    Req.u.In.uArgs.Modify.fOrMask   = fOrMask;

    int rc = suplibOsIOCtl(&g_supLibData, SUP_IOCTL_MSR_PROBER, &Req, SUP_IOCTL_MSR_PROBER_SIZE);
    if (RT_SUCCESS(rc))
        rc = Req.Hdr.rc;
    if (RT_SUCCESS(rc))
        *pResult = Req.u.Out.uResults.Modify;

    return rc;
}


SUPR3DECL(int) SUPR3ResumeSuspendedKeyboards(void)
{
#ifdef RT_OS_DARWIN
    /*
     * Issue IOCtl to the SUPDRV kernel module.
     */
    SUPREQHDR Req;
    Req.u32Cookie       = g_u32Cookie;
    Req.u32SessionCookie= g_u32SessionCookie;
    Req.cbIn            = SUP_IOCTL_RESUME_SUSPENDED_KBDS_SIZE_IN;
    Req.cbOut           = SUP_IOCTL_RESUME_SUSPENDED_KBDS_SIZE_OUT;
    Req.fFlags          = SUPREQHDR_FLAGS_DEFAULT;
    Req.rc              = VERR_INTERNAL_ERROR;
    int rc = suplibOsIOCtl(&g_supLibData, SUP_IOCTL_RESUME_SUSPENDED_KBDS, &Req, SUP_IOCTL_RESUME_SUSPENDED_KBDS_SIZE);
    if (RT_SUCCESS(rc))
        rc = Req.rc;
    return rc;
#else /* !RT_OS_DARWIN */
    return VERR_NOT_SUPPORTED;
#endif
}


SUPR3DECL(int) SUPR3TscDeltaMeasure(RTCPUID idCpu, bool fAsync, bool fForce, uint8_t cRetries, uint8_t cMsWaitRetry)
{
    SUPTSCDELTAMEASURE Req;
    Req.Hdr.u32Cookie        = g_u32Cookie;
    Req.Hdr.u32SessionCookie = g_u32SessionCookie;
    Req.Hdr.cbIn             = SUP_IOCTL_TSC_DELTA_MEASURE_SIZE_IN;
    Req.Hdr.cbOut            = SUP_IOCTL_TSC_DELTA_MEASURE_SIZE_OUT;
    Req.Hdr.fFlags           = SUPREQHDR_FLAGS_DEFAULT;
    Req.Hdr.rc               = VERR_INTERNAL_ERROR;

    Req.u.In.cRetries     = cRetries;
    Req.u.In.fAsync       = fAsync;
    Req.u.In.fForce       = fForce;
    Req.u.In.idCpu        = idCpu;
    Req.u.In.cMsWaitRetry = cMsWaitRetry;

    int rc = suplibOsIOCtl(&g_supLibData, SUP_IOCTL_TSC_DELTA_MEASURE, &Req, SUP_IOCTL_TSC_DELTA_MEASURE_SIZE);
    if (RT_SUCCESS(rc))
        rc = Req.Hdr.rc;
    return rc;
}


SUPR3DECL(int) SUPR3ReadTsc(uint64_t *puTsc, uint16_t *pidApic)
{
    AssertReturn(puTsc, VERR_INVALID_PARAMETER);

    SUPTSCREAD Req;
    Req.Hdr.u32Cookie        = g_u32Cookie;
    Req.Hdr.u32SessionCookie = g_u32SessionCookie;
    Req.Hdr.cbIn             = SUP_IOCTL_TSC_READ_SIZE_IN;
    Req.Hdr.cbOut            = SUP_IOCTL_TSC_READ_SIZE_OUT;
    Req.Hdr.fFlags           = SUPREQHDR_FLAGS_DEFAULT;
    Req.Hdr.rc               = VERR_INTERNAL_ERROR;

    int rc = suplibOsIOCtl(&g_supLibData, SUP_IOCTL_TSC_READ, &Req, SUP_IOCTL_TSC_READ_SIZE);
    if (RT_SUCCESS(rc))
    {
        rc = Req.Hdr.rc;
        *puTsc = Req.u.Out.u64AdjustedTsc;
        if (pidApic)
            *pidApic = Req.u.Out.idApic;
    }
    return rc;
}


SUPR3DECL(int) SUPR3GipSetFlags(uint32_t fOrMask, uint32_t fAndMask)
{
    AssertMsgReturn(!(fOrMask & ~SUPGIP_FLAGS_VALID_MASK),
                    ("fOrMask=%#x ValidMask=%#x\n", fOrMask, SUPGIP_FLAGS_VALID_MASK), VERR_INVALID_PARAMETER);
    AssertMsgReturn((fAndMask & ~SUPGIP_FLAGS_VALID_MASK) == ~SUPGIP_FLAGS_VALID_MASK,
                    ("fAndMask=%#x ValidMask=%#x\n", fAndMask, SUPGIP_FLAGS_VALID_MASK), VERR_INVALID_PARAMETER);

    SUPGIPSETFLAGS Req;
    Req.Hdr.u32Cookie        = g_u32Cookie;
    Req.Hdr.u32SessionCookie = g_u32SessionCookie;
    Req.Hdr.cbIn             = SUP_IOCTL_GIP_SET_FLAGS_SIZE_IN;
    Req.Hdr.cbOut            = SUP_IOCTL_GIP_SET_FLAGS_SIZE_OUT;
    Req.Hdr.fFlags           = SUPREQHDR_FLAGS_DEFAULT;
    Req.Hdr.rc               = VERR_INTERNAL_ERROR;

    Req.u.In.fAndMask        = fAndMask;
    Req.u.In.fOrMask         = fOrMask;

    int rc = suplibOsIOCtl(&g_supLibData, SUP_IOCTL_GIP_SET_FLAGS, &Req, SUP_IOCTL_GIP_SET_FLAGS_SIZE);
    if (RT_SUCCESS(rc))
        rc = Req.Hdr.rc;
    return rc;
}


SUPR3DECL(int) SUPR3GetHwvirtMsrs(PSUPHWVIRTMSRS pHwvirtMsrs, bool fForceRequery)
{
    AssertReturn(pHwvirtMsrs, VERR_INVALID_PARAMETER);

    SUPGETHWVIRTMSRS Req;
    Req.Hdr.u32Cookie        = g_u32Cookie;
    Req.Hdr.u32SessionCookie = g_u32SessionCookie;
    Req.Hdr.cbIn             = SUP_IOCTL_GET_HWVIRT_MSRS_SIZE_IN;
    Req.Hdr.cbOut            = SUP_IOCTL_GET_HWVIRT_MSRS_SIZE_OUT;
    Req.Hdr.fFlags           = SUPREQHDR_FLAGS_DEFAULT;
    Req.Hdr.rc               = VERR_INTERNAL_ERROR;

    Req.u.In.fForce          = fForceRequery;
    Req.u.In.fReserved0      = false;
    Req.u.In.fReserved1      = false;
    Req.u.In.fReserved2      = false;

    int rc = suplibOsIOCtl(&g_supLibData, SUP_IOCTL_GET_HWVIRT_MSRS, &Req, SUP_IOCTL_GET_HWVIRT_MSRS_SIZE);
    if (RT_SUCCESS(rc))
    {
        rc = Req.Hdr.rc;
        *pHwvirtMsrs = Req.u.Out.HwvirtMsrs;
    }
    else
        RT_ZERO(*pHwvirtMsrs);
    return rc;
}

