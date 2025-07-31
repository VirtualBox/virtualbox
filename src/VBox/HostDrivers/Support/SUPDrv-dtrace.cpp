/* $Id: SUPDrv-dtrace.cpp 110494 2025-07-31 10:11:09Z andreas.loeffler@oracle.com $ */
/** @file
 * VBoxDrv - The VirtualBox Support Driver - DTrace Provider.
 */

/*
 * Copyright (C) 2012-2024 Oracle and/or its affiliates.
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


/*********************************************************************************************************************************
*   Header Files                                                                                                                 *
*********************************************************************************************************************************/
#define LOG_GROUP LOG_GROUP_SUP_DRV
#include "SUPDrvInternal.h"

#include <VBox/err.h>
#include <VBox/log.h>
#include <VBox/VBoxTpG.h>

#include <iprt/assert.h>
#include <iprt/ctype.h>
#include <iprt/mem.h>
#include <iprt/errno.h>
#ifdef RT_OS_DARWIN
# include <iprt/dbg.h>
#endif

#ifdef RT_OS_DARWIN
# include VBOX_PATH_MACOSX_DTRACE_H
#elif defined(RT_OS_LINUX)
/* Avoid type and define conflicts. */
# undef UINT8_MAX
# undef UINT16_MAX
# undef UINT32_MAX
# undef UINT64_MAX
# undef INT64_MAX
# undef INT64_MIN
# define intptr_t dtrace_intptr_t

# if 0
/* DTrace experiments with the Unbreakable Enterprise Kernel (UEK2)
   (Oracle Linux).
   1. The dtrace.h here is from the dtrace module source, not
      /usr/include/sys/dtrace.h nor /usr/include/dtrace.h.
   2. To generate the missing entries for the dtrace module in Module.symvers
      of UEK:
      nm /lib/modules/....../kernel/drivers/dtrace/dtrace.ko  \
      | grep _crc_ \
      | sed -e 's/^......../0x/' -e 's/ A __crc_/\t/' \
            -e 's/$/\tdrivers\/dtrace\/dtrace\tEXPORT_SYMBOL/' \
      >> Module.symvers
      Update: Althernative workaround (active), resolve symbols dynamically.
   3. No tracepoints in vboxdrv, vboxnet* or vboxpci yet.  This requires yasm
      and VBoxTpG and build time. */
#  include "dtrace.h"
# else
/* DTrace experiments with the Unbreakable Enterprise Kernel (UEKR3)
   (Oracle Linux).
   1. To generate the missing entries for the dtrace module in Module.symvers
      of UEK:
      nm /lib/modules/....../kernel/drivers/dtrace/dtrace.ko  \
      | grep _crc_ \
      | sed -e 's/^......../0x/' -e 's/ A __crc_/\t/' \
            -e 's/$/\tdrivers\/dtrace\/dtrace\tEXPORT_SYMBOL/' \
      >> Module.symvers
      Update: Althernative workaround (active), resolve symbols dynamically.
   2. No tracepoints in vboxdrv, vboxnet* or vboxpci yet.  This requires yasm
      and VBoxTpG and build time. */
#  include <dtrace/provider.h>
#  include <dtrace/enabling.h> /* Missing from provider.h. */
#  include <dtrace/arg.h> /* Missing from provider.h. */
# endif
# include <linux/module.h>
/** Status code fixer (UEK uses linux convension unlike the others). */
# define FIX_UEK_RC(a_rc) (-(a_rc))
#else
# include <sys/dtrace.h>
# undef u
#endif


/**
 * The UEK DTrace port is trying to be smart and seems to have turned all
 * errno return codes negative.  While this conforms to the linux kernel way of
 * doing things, it breaks with the way the interfaces work on Solaris and
 * Mac OS X.
 */
#ifndef FIX_UEK_RC
# define FIX_UEK_RC(a_rc) (a_rc)
#endif


/** @name Macros for preserving EFLAGS.AC (despair / paranoid)
 * @remarks We have to restore it unconditionally on darwin.
 * @{ */
#if defined(RT_OS_DARWIN) \
 || (    defined(RT_OS_LINUX) \
     && (defined(CONFIG_X86_SMAP) || defined(RT_STRICT) || defined(IPRT_WITH_EFLAGS_AC_PRESERVING) ) )
# include <iprt/asm-amd64-x86.h>
# include <iprt/x86.h>
# define SUPDRV_SAVE_EFL_AC()          RTCCUINTREG const fSavedEfl = ASMGetFlags();
# define SUPDRV_RESTORE_EFL_AC()       ASMSetFlags(fSavedEfl)
# define SUPDRV_RESTORE_EFL_ONLY_AC()  ASMChangeFlags(~X86_EFL_AC, fSavedEfl & X86_EFL_AC)
#else
# define SUPDRV_SAVE_EFL_AC()          do { } while (0)
# define SUPDRV_RESTORE_EFL_AC()       do { } while (0)
# define SUPDRV_RESTORE_EFL_ONLY_AC()  do { } while (0)
#endif
/** @} */


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/
/* Seems there is some return code difference here. Keep the return code and
   case it to whatever the host desires. */
#ifdef RT_OS_DARWIN
# if MAC_OS_X_VERSION_MIN_REQUIRED < 1070
typedef void FNPOPS_ENABLE(void *, dtrace_id_t, void *);
# else
typedef int  FNPOPS_ENABLE(void *, dtrace_id_t, void *);
# endif
#else
# if !defined(RT_OS_SOLARIS) || defined(DOF_SEC_ISLOADABLE) /* DOF_SEC_ISLOADABLE was added in the next commit after dtps_enable change signature, but it's the simplest check we can do. */
typedef int  FNPOPS_ENABLE(void *, dtrace_id_t, void *);
# else
typedef void FNPOPS_ENABLE(void *, dtrace_id_t, void *);
# endif
#endif

/** Caller indicator. */
typedef enum VBOXDTCALLER
{
    kVBoxDtCaller_Invalid = 0,
    kVBoxDtCaller_Generic,
    kVBoxDtCaller_ProbeFireUser,
    kVBoxDtCaller_ProbeFireKernel
} VBOXDTCALLER;


/**
 * Stack data planted before calling dtrace_probe so that we can easily find the
 * stack argument later.
 */
typedef struct VBDTSTACKDATA
{
    /** Eyecatcher no. 1 (SUPDRVDT_STACK_DATA_MAGIC2). */
    uint32_t                    u32Magic1;
    /** Eyecatcher no. 2 (SUPDRVDT_STACK_DATA_MAGIC2). */
    uint32_t                    u32Magic2;
    /** The format of the caller specific data. */
    VBOXDTCALLER                enmCaller;
    /** Caller specific data.  */
    union
    {
        /** kVBoxDtCaller_ProbeFireKernel. */
        struct
        {
            /** Pointer to the stack arguments of a probe function call. */
            uintptr_t              *pauStackArgs;
        } ProbeFireKernel;
        /** kVBoxDtCaller_ProbeFireUser. */
        struct
        {
            /** The user context.  */
            PCSUPDRVTRACERUSRCTX    pCtx;
            /** The argument displacement caused by 64-bit arguments passed directly to
             *  dtrace_probe. */
            int                     offArg;
        } ProbeFireUser;
    } u;
    /** Pointer to this structure.
     * This is the final bit of integrity checking. */
    struct VBDTSTACKDATA           *pSelf;
} VBDTSTACKDATA;
/** Pointer to the on-stack thread specific data. */
typedef VBDTSTACKDATA *PVBDTSTACKDATA;


/*********************************************************************************************************************************
*   Defined Constants And Macros                                                                                                 *
*********************************************************************************************************************************/
/** The first magic value. */
#define SUPDRVDT_STACK_DATA_MAGIC1      RT_MAKE_U32_FROM_U8('S', 'U', 'P', 'D')
/** The second magic value. */
#define SUPDRVDT_STACK_DATA_MAGIC2      RT_MAKE_U32_FROM_U8('D', 'T', 'r', 'c')

/** The alignment of the stack data.
 * The data doesn't require more than sizeof(uintptr_t) alignment, but the
 * greater alignment the quicker lookup. */
#define SUPDRVDT_STACK_DATA_ALIGN       32

/** Plants the stack data. */
#define VBDT_SETUP_STACK_DATA(a_enmCaller) \
    uint8_t abBlob[sizeof(VBDTSTACKDATA) + SUPDRVDT_STACK_DATA_ALIGN - 1]; \
    PVBDTSTACKDATA pStackData = (PVBDTSTACKDATA)(   (uintptr_t)&abBlob[SUPDRVDT_STACK_DATA_ALIGN - 1] \
                                                 & ~(uintptr_t)(SUPDRVDT_STACK_DATA_ALIGN - 1)); \
    pStackData->u32Magic1   = SUPDRVDT_STACK_DATA_MAGIC1; \
    pStackData->u32Magic2   = SUPDRVDT_STACK_DATA_MAGIC2; \
    pStackData->enmCaller   = a_enmCaller; \
    pStackData->pSelf       = pStackData

/** Passifies the stack data and frees up resource held within it. */
#define VBDT_CLEAR_STACK_DATA() \
    do \
    { \
        pStackData->u32Magic1   = 0; \
        pStackData->u32Magic2   = 0; \
        pStackData->pSelf       = NULL; \
    } while (0)

/** Simple SUPR0Printf-style logging.  */
#if 0 /*def DEBUG_bird*/
# define LOG_DTRACE(a) SUPR0Printf a
#else
# define LOG_DTRACE(a) do { } while (0)
#endif


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
#if defined(RT_OS_DARWIN) || defined(RT_OS_LINUX)
/** @name DTrace kernel interface used on Darwin and Linux.
 * @{ */
static DECLCALLBACKPTR_EX(void,        RT_NOTHING, g_pfnDTraceProbeFire,(dtrace_id_t, uint64_t, uint64_t, uint64_t, uint64_t,
                                                                         uint64_t));
static DECLCALLBACKPTR_EX(dtrace_id_t, RT_NOTHING, g_pfnDTraceProbeCreate,(dtrace_provider_id_t, const char *, const char *,
                                                                           const char *, int, void *));
static DECLCALLBACKPTR_EX(dtrace_id_t, RT_NOTHING, g_pfnDTraceProbeLookup,(dtrace_provider_id_t, const char *, const char *,
                                                                           const char *));
static DECLCALLBACKPTR_EX(int,         RT_NOTHING, g_pfnDTraceProviderRegister,(const char *, const dtrace_pattr_t *, uint32_t,
                                                                                /*cred_t*/ void *, const dtrace_pops_t *,
                                                                                void *, dtrace_provider_id_t *));
static DECLCALLBACKPTR_EX(void,        RT_NOTHING, g_pfnDTraceProviderInvalidate,(dtrace_provider_id_t));
static DECLCALLBACKPTR_EX(int,         RT_NOTHING, g_pfnDTraceProviderUnregister,(dtrace_provider_id_t));

#define dtrace_probe            g_pfnDTraceProbeFire
#define dtrace_probe_create     g_pfnDTraceProbeCreate
#define dtrace_probe_lookup     g_pfnDTraceProbeLookup
#define dtrace_register         g_pfnDTraceProviderRegister
#define dtrace_invalidate       g_pfnDTraceProviderInvalidate
#define dtrace_unregister       g_pfnDTraceProviderUnregister

/** For dynamical resolving and releasing.   */
static const struct
{
    const char *pszName;
    uintptr_t  *ppfn; /**< @note Clang 11 nothrow weirdness forced this from PFNRT * to uintptr_t *. */
} g_aDTraceFunctions[] =
{
    { "dtrace_probe",        (uintptr_t *)&dtrace_probe        },
    { "dtrace_probe_create", (uintptr_t *)&dtrace_probe_create },
    { "dtrace_probe_lookup", (uintptr_t *)&dtrace_probe_lookup },
    { "dtrace_register",     (uintptr_t *)&dtrace_register     },
    { "dtrace_invalidate",   (uintptr_t *)&dtrace_invalidate   },
    { "dtrace_unregister",   (uintptr_t *)&dtrace_unregister   },
};

/** @} */
#endif


/**
 * Gets the stack data.
 *
 * @returns Pointer to the stack data.  Never NULL.
 */
static PVBDTSTACKDATA vboxDtGetStackData(void)
{
    /*
     * Locate the caller of probe_dtrace.
     */
    int volatile    iDummy = 1; /* use this to get the stack address. */
    PVBDTSTACKDATA  pData = (PVBDTSTACKDATA)(  ((uintptr_t)&iDummy + SUPDRVDT_STACK_DATA_ALIGN - 1)
                                             & ~(uintptr_t)(SUPDRVDT_STACK_DATA_ALIGN - 1));
    for (;;)
    {
        if (   pData->u32Magic1 == SUPDRVDT_STACK_DATA_MAGIC1
            && pData->u32Magic2 == SUPDRVDT_STACK_DATA_MAGIC2
            && pData->pSelf     == pData)
            return pData;
        pData = (PVBDTSTACKDATA)((uintptr_t)pData + SUPDRVDT_STACK_DATA_ALIGN);
    }
}


/*
 *
 * Helpers for handling VTG structures.
 * Helpers for handling VTG structures.
 * Helpers for handling VTG structures.
 *
 */



/**
 * Converts an attribute from VTG description speak to DTrace.
 *
 * @param   pDtAttr             The DTrace attribute (dst).
 * @param   pVtgAttr            The VTG attribute descriptor (src).
 */
static void vboxDtVtgConvAttr(dtrace_attribute_t *pDtAttr, PCVTGDESCATTR pVtgAttr)
{
    pDtAttr->dtat_name  = pVtgAttr->u8Code - 1;
    pDtAttr->dtat_data  = pVtgAttr->u8Data - 1;
    pDtAttr->dtat_class = pVtgAttr->u8DataDep - 1;
}

/**
 * Gets a string from the string table.
 *
 * @returns Pointer to the string.
 * @param   pVtgHdr             The VTG object header.
 * @param   offStrTab           The string table offset.
 */
static const char *vboxDtVtgGetString(PVTGOBJHDR pVtgHdr, uint32_t offStrTab)
{
    Assert(offStrTab < pVtgHdr->cbStrTab);
    return (const char *)pVtgHdr + pVtgHdr->offStrTab + offStrTab;
}



/*
 *
 * DTrace Provider Interface.
 * DTrace Provider Interface.
 * DTrace Provider Interface.
 *
 */


/**
 * @callback_method_impl{dtrace_pops_t,dtps_provide}
 */
static void     vboxDtPOps_Provide(void *pvProv, const dtrace_probedesc_t *pDtProbeDesc)
{
    PSUPDRVVDTPROVIDERCORE  pProv = (PSUPDRVVDTPROVIDERCORE)pvProv;
    AssertPtrReturnVoid(pProv);
    LOG_DTRACE(("%s: %p / %p pDtProbeDesc=%p\n", __FUNCTION__, pProv, pProv->TracerData.DTrace.idProvider, pDtProbeDesc));

    if (pDtProbeDesc)
        return;  /* We don't generate probes, so never mind these requests. */

    if (pProv->TracerData.DTrace.fZombie)
        return;

    dtrace_provider_id_t const idProvider = pProv->TracerData.DTrace.idProvider;
    AssertPtrReturnVoid(idProvider);

    AssertPtrReturnVoid(pProv->pHdr);
    AssertReturnVoid(pProv->pHdr->offProbeLocs != 0);
    uint32_t const  cProbeLocs    = pProv->pHdr->cbProbeLocs / sizeof(VTGPROBELOC);

    /* Need a buffer for extracting the function names and mangling them in
       case of collision. */
    size_t const cbFnNmBuf = _4K + _1K;
    char *pszFnNmBuf = (char *)RTMemAlloc(cbFnNmBuf);
    if (!pszFnNmBuf)
         return;

    /*
     * Itereate the probe location list and register all probes related to
     * this provider.
     */
    uint16_t const idxProv = (uint16_t)((PVTGDESCPROVIDER)((uintptr_t)pProv->pHdr + pProv->pHdr->offProviders) - pProv->pDesc);
    uint32_t idxProbeLoc;
    for (idxProbeLoc = 0; idxProbeLoc < cProbeLocs; idxProbeLoc++)
    {
        /* Skip probe location belonging to other providers or once that
           we've already reported. */
        PCVTGPROBELOC pProbeLocRO = &pProv->paProbeLocsRO[idxProbeLoc];
        PVTGDESCPROBE pProbeDesc  = pProbeLocRO->pProbe;
        if (pProbeDesc->idxProvider != idxProv)
            continue;

        uint32_t *pidProbe;
        if (!pProv->fUmod)
            pidProbe = (uint32_t *)&pProbeLocRO->idProbe;
        else
            pidProbe = &pProv->paR0ProbeLocs[idxProbeLoc].idProbe;
        if (*pidProbe != 0)
            continue;

         /* The function name may need to be stripped since we're using C++
            compilers for most of the code.  ASSUMES nobody are brave/stupid
            enough to use function pointer returns without typedef'ing
            properly them (e.g. signal). */
         const char *pszPrbName = vboxDtVtgGetString(pProv->pHdr, pProbeDesc->offName);
         const char *pszFunc    = pProbeLocRO->pszFunction;
         const char *psz        = strchr(pProbeLocRO->pszFunction, '(');
         size_t      cch;
         if (psz)
         {
             /* skip blanks preceding the parameter parenthesis. */
             while (   (uintptr_t)psz > (uintptr_t)pProbeLocRO->pszFunction
                    && RT_C_IS_BLANK(psz[-1]))
                 psz--;

             /* Find the start of the function name. */
             pszFunc = psz - 1;
             while ((uintptr_t)pszFunc > (uintptr_t)pProbeLocRO->pszFunction)
             {
                 char ch = pszFunc[-1];
                 if (!RT_C_IS_ALNUM(ch) && ch != '_' && ch != ':')
                     break;
                 pszFunc--;
             }
             cch = psz - pszFunc;
         }
         else
             cch = strlen(pszFunc);
         RTStrCopyEx(pszFnNmBuf, cbFnNmBuf, pszFunc, cch);

         /* Look up the probe, if we have one in the same function, mangle
            the function name a little to avoid having to deal with having
            multiple location entries with the same probe ID. (lazy bird) */
         Assert(!*pidProbe);
         if (dtrace_probe_lookup(idProvider, pProv->pszModName, pszFnNmBuf, pszPrbName) != DTRACE_IDNONE)
         {
             RTStrPrintf(pszFnNmBuf+cch, cbFnNmBuf - cch, "-%u", pProbeLocRO->uLine);
             if (dtrace_probe_lookup(idProvider, pProv->pszModName, pszFnNmBuf, pszPrbName) != DTRACE_IDNONE)
             {
                 unsigned iOrd = 2;
                 while (iOrd < 128)
                 {
                     RTStrPrintf(pszFnNmBuf+cch, cbFnNmBuf - cch, "-%u-%u", pProbeLocRO->uLine, iOrd);
                     if (dtrace_probe_lookup(idProvider, pProv->pszModName, pszFnNmBuf, pszPrbName) == DTRACE_IDNONE)
                         break;
                     iOrd++;
                 }
                 if (iOrd >= 128)
                 {
                     LogRel(("VBoxDrv: More than 128 duplicate probe location instances %s at line %u in function %s [%s], probe %s\n",
                             pProbeLocRO->uLine, pProbeLocRO->pszFunction, pszFnNmBuf, pszPrbName));
                     continue;
                 }
             }
         }

         /* Create the probe. */
         AssertCompile(sizeof(*pidProbe) == sizeof(dtrace_id_t));
         *pidProbe = dtrace_probe_create(idProvider, pProv->pszModName, pszFnNmBuf, pszPrbName,
                                         1 /*aframes*/, (void *)(uintptr_t)idxProbeLoc);
         pProv->TracerData.DTrace.cProvidedProbes++;
     }

     RTMemFree(pszFnNmBuf);
     LOG_DTRACE(("%s: returns\n", __FUNCTION__));
}


/**
 * @callback_method_impl{dtrace_pops_t,dtps_enable}
 */
static int      vboxDtPOps_Enable(void *pvProv, dtrace_id_t idProbe, void *pvProbe)
{
    PSUPDRVVDTPROVIDERCORE  pProv   = (PSUPDRVVDTPROVIDERCORE)pvProv;
    RT_NOREF(idProbe);
    LOG_DTRACE(("%s: %p / %p - %#x / %p\n", __FUNCTION__, pProv, pProv->TracerData.DTrace.idProvider, idProbe, pvProbe));
    AssertPtrReturn(pProv->TracerData.DTrace.idProvider, EINVAL);

    if (!pProv->TracerData.DTrace.fZombie)
    {
        uint32_t        idxProbeLoc = (uint32_t)(uintptr_t)pvProbe;
        PVTGPROBELOC32  pProbeLocEn = (PVTGPROBELOC32)(  (uintptr_t)pProv->pvProbeLocsEn + idxProbeLoc * pProv->cbProbeLocsEn);
        PCVTGPROBELOC   pProbeLocRO = (PVTGPROBELOC)&pProv->paProbeLocsRO[idxProbeLoc];
        PCVTGDESCPROBE  pProbeDesc  = pProbeLocRO->pProbe;
        uint32_t const  idxProbe    = pProbeDesc->idxEnabled;

        if (!pProv->fUmod)
        {
            if (!pProbeLocEn->fEnabled)
            {
                pProbeLocEn->fEnabled = 1;
                ASMAtomicIncU32(&pProv->pacProbeEnabled[idxProbe]);
                ASMAtomicIncU32(&pProv->pDesc->cProbesEnabled);
                ASMAtomicIncU32(&pProv->pDesc->uSettingsSerialNo);
            }
        }
        else
        {
            /* Update kernel mode structure */
            if (!pProv->paR0ProbeLocs[idxProbeLoc].fEnabled)
            {
                pProv->paR0ProbeLocs[idxProbeLoc].fEnabled = 1;
                ASMAtomicIncU32(&pProv->paR0Probes[idxProbe].cEnabled);
                ASMAtomicIncU32(&pProv->pDesc->cProbesEnabled);
                ASMAtomicIncU32(&pProv->pDesc->uSettingsSerialNo);
            }

            /* Update user mode structure. */
            pProbeLocEn->fEnabled = 1;
            pProv->pacProbeEnabled[idxProbe] = pProv->paR0Probes[idxProbe].cEnabled;
        }
    }

    return 0;
}


/**
 * @callback_method_impl{dtrace_pops_t,dtps_disable}
 */
static void     vboxDtPOps_Disable(void *pvProv, dtrace_id_t idProbe, void *pvProbe)
{
    PSUPDRVVDTPROVIDERCORE  pProv  = (PSUPDRVVDTPROVIDERCORE)pvProv;
    AssertPtrReturnVoid(pProv);
    RT_NOREF(idProbe);
    LOG_DTRACE(("%s: %p / %p - %#x / %p\n", __FUNCTION__, pProv, pProv->TracerData.DTrace.idProvider, idProbe, pvProbe));
    AssertPtrReturnVoid(pProv->TracerData.DTrace.idProvider);

    if (!pProv->TracerData.DTrace.fZombie)
    {
        uint32_t        idxProbeLoc = (uint32_t)(uintptr_t)pvProbe;
        PVTGPROBELOC32  pProbeLocEn = (PVTGPROBELOC32)(  (uintptr_t)pProv->pvProbeLocsEn + idxProbeLoc * pProv->cbProbeLocsEn);
        PCVTGPROBELOC   pProbeLocRO = (PVTGPROBELOC)&pProv->paProbeLocsRO[idxProbeLoc];
        PCVTGDESCPROBE  pProbeDesc  = pProbeLocRO->pProbe;
        uint32_t const  idxProbe    = pProbeDesc->idxEnabled;

        if (!pProv->fUmod)
        {
            if (pProbeLocEn->fEnabled)
            {
                pProbeLocEn->fEnabled = 0;
                ASMAtomicDecU32(&pProv->pacProbeEnabled[idxProbe]);
                ASMAtomicIncU32(&pProv->pDesc->cProbesEnabled);
                ASMAtomicIncU32(&pProv->pDesc->uSettingsSerialNo);
            }
        }
        else
        {
            /* Update kernel mode structure */
            if (pProv->paR0ProbeLocs[idxProbeLoc].fEnabled)
            {
                pProv->paR0ProbeLocs[idxProbeLoc].fEnabled = 0;
                ASMAtomicDecU32(&pProv->paR0Probes[idxProbe].cEnabled);
                ASMAtomicDecU32(&pProv->pDesc->cProbesEnabled);
                ASMAtomicIncU32(&pProv->pDesc->uSettingsSerialNo);
            }

            /* Update user mode structure. */
            pProbeLocEn->fEnabled = 0;
            pProv->pacProbeEnabled[idxProbe] = pProv->paR0Probes[idxProbe].cEnabled;
        }
    }
}


/**
 * @callback_method_impl{dtrace_pops_t,dtps_getargdesc}
 */
static void     vboxDtPOps_GetArgDesc(void *pvProv, dtrace_id_t idProbe, void *pvProbe,
                                      dtrace_argdesc_t *pArgDesc)
{
    PSUPDRVVDTPROVIDERCORE  pProv  = (PSUPDRVVDTPROVIDERCORE)pvProv;
    unsigned                uArg   = pArgDesc->dtargd_ndx;
    RT_NOREF(idProbe);

    pArgDesc->dtargd_ndx = DTRACE_ARGNONE;
    AssertPtrReturnVoid(pProv);
    LOG_DTRACE(("%s: %p / %p - %#x / %p uArg=%d\n", __FUNCTION__, pProv, pProv->TracerData.DTrace.idProvider, idProbe, pvProbe, uArg));
    AssertPtrReturnVoid(pProv->TracerData.DTrace.idProvider);

    if (!pProv->TracerData.DTrace.fZombie)
    {
        uint32_t         idxProbeLoc = (uint32_t)(uintptr_t)pvProbe;
        PCVTGPROBELOC    pProbeLocRO = (PVTGPROBELOC)&pProv->paProbeLocsRO[idxProbeLoc];
        PCVTGDESCPROBE   pProbeDesc  = pProbeLocRO->pProbe;
        PCVTGDESCARGLIST pArgList    = (PCVTGDESCARGLIST)(  (uintptr_t)pProv->pHdr
                                                          + pProv->pHdr->offArgLists
                                                          + pProbeDesc->offArgList);
        AssertReturnVoid(pProbeDesc->offArgList < pProv->pHdr->cbArgLists);

        if (uArg < pArgList->cArgs)
        {
            const char *pszType = vboxDtVtgGetString(pProv->pHdr, pArgList->aArgs[uArg].offType);
            size_t      cchType = strlen(pszType);
            if (cchType < sizeof(pArgDesc->dtargd_native))
            {
                memcpy(pArgDesc->dtargd_native, pszType, cchType + 1);
                /** @todo mapping? */
                pArgDesc->dtargd_ndx = uArg;
                LOG_DTRACE(("%s: returns dtargd_native = %s\n", __FUNCTION__, pArgDesc->dtargd_native));
                return;
            }
        }
    }
}


/**
 * @callback_method_impl{dtrace_pops_t,dtps_getargval}
 *
 *
 * We just cook our own stuff here, using a stack marker for finding the
 * required information.  That's more reliable than subjecting oneself to the
 * solaris bugs and 32-bit apple peculiarities.
 *
 *
 * @remarks Solaris Bug
 *
 * dtrace_getarg on AMD64 has a different opinion about how to use the cFrames
 * argument than dtrace_caller() and/or dtrace_getpcstack(), at least when the
 * probe is fired by dtrace_probe() the way we do.
 *
 * Setting aframes to 1 when calling dtrace_probe_create gives me the right
 * arguments, but the wrong 'caller'.  Since I cannot do anything about
 * 'caller', the only solution is this hack.
 *
 * Not sure why the  Solaris guys hasn't seen this issue before, but maybe there
 * isn't anyone using the default argument getter path for ring-0 dtrace_probe()
 * calls, SDT surely isn't.
 *
 * @todo File a solaris bug on dtrace_probe() + dtrace_getarg().
 *
 *
 * @remarks 32-bit XNU (Apple)
 *
 * The dtrace_probe arguments are 64-bit unsigned integers instead of uintptr_t,
 * so we need to make an extra call.
 *
 */
static uint64_t vboxDtPOps_GetArgVal(void *pvProv, dtrace_id_t idProbe, void *pvProbe,
                                     int iArg, int cFrames)
{
    PSUPDRVVDTPROVIDERCORE  pProv = (PSUPDRVVDTPROVIDERCORE)pvProv;
    AssertPtrReturn(pProv, UINT64_MAX);
    RT_NOREF(idProbe, cFrames);
    LOG_DTRACE(("%s: %p / %p - %#x / %p iArg=%d cFrames=%u\n", __FUNCTION__, pProv, pProv->TracerData.DTrace.idProvider, idProbe, pvProbe, iArg, cFrames));
    AssertReturn(iArg >= 5, UINT64_MAX);
    if (pProv->TracerData.DTrace.fZombie)
        return UINT64_MAX;

    uint32_t                idxProbeLoc = (uint32_t)(uintptr_t)pvProbe;
    PCVTGPROBELOC           pProbeLocRO = (PVTGPROBELOC)&pProv->paProbeLocsRO[idxProbeLoc];
    PCVTGDESCPROBE          pProbeDesc  = pProbeLocRO->pProbe;
    PCVTGDESCARGLIST        pArgList    = (PCVTGDESCARGLIST)(  (uintptr_t)pProv->pHdr
                                                             + pProv->pHdr->offArgLists
                                                             + pProbeDesc->offArgList);
    AssertReturn(pProbeDesc->offArgList < pProv->pHdr->cbArgLists, UINT64_MAX);

    PVBDTSTACKDATA          pData = vboxDtGetStackData();

    /*
     * Get the stack data. This is a wee bit complicated on 32-bit systems
     * since we want to support 64-bit integer arguments.
     */
    uint64_t u64Ret;
    if (iArg >= 20)
        u64Ret = UINT64_MAX;
    else if (pData->enmCaller == kVBoxDtCaller_ProbeFireKernel)
    {
#if ARCH_BITS == 64
        u64Ret = pData->u.ProbeFireKernel.pauStackArgs[iArg - 5];
#else
        if (   !pArgList->fHaveLargeArgs
            || iArg >= pArgList->cArgs)
            u64Ret = pData->u.ProbeFireKernel.pauStackArgs[iArg - 5];
        else
        {
            /* Similar to what we did for mac in when calling dtrace_probe(). */
            uint32_t offArg = 0;
            for (int i = 5; i < iArg; i++)
                if (VTG_TYPE_IS_LARGE(pArgList->aArgs[iArg].fType))
                    offArg++;
            u64Ret = pData->u.ProbeFireKernel.pauStackArgs[iArg - 5 + offArg];
            if (VTG_TYPE_IS_LARGE(pArgList->aArgs[iArg].fType))
                u64Ret |= (uint64_t)pData->u.ProbeFireKernel.pauStackArgs[iArg - 5 + offArg + 1] << 32;
        }
#endif
    }
    else if (pData->enmCaller == kVBoxDtCaller_ProbeFireUser)
    {
        int                     offArg    = pData->u.ProbeFireUser.offArg;
        PCSUPDRVTRACERUSRCTX    pCtx      = pData->u.ProbeFireUser.pCtx;
        AssertPtrReturn(pCtx, UINT64_MAX);

        if (pCtx->cBits == 32)
        {
            if (   !pArgList->fHaveLargeArgs
                || iArg >= pArgList->cArgs)
            {
                if (iArg + offArg < (int)RT_ELEMENTS(pCtx->u.X86.aArgs))
                    u64Ret = pCtx->u.X86.aArgs[iArg + offArg];
                else
                    u64Ret = UINT64_MAX;
            }
            else
            {
                int i;
                for (i = 5; i < iArg; i++)
                    if (VTG_TYPE_IS_LARGE(pArgList->aArgs[iArg].fType))
                        offArg++;
                if (offArg + iArg < (int)RT_ELEMENTS(pCtx->u.X86.aArgs))
                {
                    u64Ret = pCtx->u.X86.aArgs[iArg + offArg];
                    if (   VTG_TYPE_IS_LARGE(pArgList->aArgs[iArg].fType)
                        && offArg + iArg + 1 < (int)RT_ELEMENTS(pCtx->u.X86.aArgs))
                        u64Ret |= (uint64_t)pCtx->u.X86.aArgs[iArg + offArg + 1] << 32;
                }
                else
                    u64Ret = UINT64_MAX;
            }
        }
        else
        {
            if (iArg + offArg < (int)RT_ELEMENTS(pCtx->u.Amd64.aArgs))
                u64Ret = pCtx->u.Amd64.aArgs[iArg + offArg];
            else
                u64Ret = UINT64_MAX;
        }
    }
    else
        AssertFailedReturn(UINT64_MAX);

    LOG_DTRACE(("%s: returns %#llx\n", __FUNCTION__, u64Ret));
    return u64Ret;
}


/**
 * @callback_method_impl{dtrace_pops_t,dtps_destroy}
 */
static void    vboxDtPOps_Destroy(void *pvProv, dtrace_id_t idProbe, void *pvProbe)
{
    PSUPDRVVDTPROVIDERCORE  pProv  = (PSUPDRVVDTPROVIDERCORE)pvProv;
    AssertPtrReturnVoid(pProv);
    LOG_DTRACE(("%s: %p / %p - %#x / %p\n", __FUNCTION__, pProv, pProv->TracerData.DTrace.idProvider, idProbe, pvProbe));
    AssertReturnVoid(pProv->TracerData.DTrace.cProvidedProbes > 0);
    AssertPtrReturnVoid(pProv->TracerData.DTrace.idProvider);

    if (!pProv->TracerData.DTrace.fZombie)
    {
        uint32_t        idxProbeLoc = (uint32_t)(uintptr_t)pvProbe;
        PCVTGPROBELOC   pProbeLocRO = (PVTGPROBELOC)&pProv->paProbeLocsRO[idxProbeLoc];
        uint32_t       *pidProbe;
        if (!pProv->fUmod)
        {
            pidProbe = (uint32_t *)&pProbeLocRO->idProbe;
            Assert(!pProbeLocRO->fEnabled);
            Assert(*pidProbe == idProbe);
        }
        else
        {
            pidProbe = &pProv->paR0ProbeLocs[idxProbeLoc].idProbe;
            Assert(!pProv->paR0ProbeLocs[idxProbeLoc].fEnabled);
            Assert(*pidProbe == idProbe); NOREF(idProbe);
        }
        *pidProbe = 0;
    }
    pProv->TracerData.DTrace.cProvidedProbes--;
}



/**
 * DTrace provider method table.
 */
static const dtrace_pops_t g_vboxDtVtgProvOps =
{
    /* .dtps_provide         = */ vboxDtPOps_Provide,
    /* .dtps_provide_module  = */ NULL,
    /* .dtps_enable          = */ (FNPOPS_ENABLE *)vboxDtPOps_Enable,
    /* .dtps_disable         = */ vboxDtPOps_Disable,
    /* .dtps_suspend         = */ NULL,
    /* .dtps_resume          = */ NULL,
    /* .dtps_getargdesc      = */ vboxDtPOps_GetArgDesc,
    /* .dtps_getargval       = */ vboxDtPOps_GetArgVal,
    /* .dtps_usermode        = */ NULL,
    /* .dtps_destroy         = */ vboxDtPOps_Destroy
};




/*
 *
 * Support Driver Tracer Interface.
 * Support Driver Tracer Interface.
 * Support Driver Tracer Interface.
 *
 */



/**
 * interface_method_impl{SUPDRVTRACERREG,pfnProbeFireKernel}
 */
static DECLCALLBACK(void) vboxDtTOps_ProbeFireKernel(struct VTGPROBELOC *pVtgProbeLoc, uintptr_t uArg0, uintptr_t uArg1, uintptr_t uArg2,
                                                     uintptr_t uArg3, uintptr_t uArg4)
{
    AssertPtrReturnVoid(pVtgProbeLoc);
    LOG_DTRACE(("%s: %p / %p\n", __FUNCTION__, pVtgProbeLoc, pVtgProbeLoc->idProbe));
    AssertPtrReturnVoid(pVtgProbeLoc->pProbe);
    AssertPtrReturnVoid(pVtgProbeLoc->pszFunction);

    SUPDRV_SAVE_EFL_AC();
    VBDT_SETUP_STACK_DATA(kVBoxDtCaller_ProbeFireKernel);

    pStackData->u.ProbeFireKernel.pauStackArgs  = &uArg4 + 1;

#if defined(RT_OS_DARWIN) && ARCH_BITS == 32
    /*
     * Convert arguments from uintptr_t to uint64_t.
     */
    PVTGDESCPROBE   pProbe   = (PVTGDESCPROBE)((PVTGPROBELOC)pVtgProbeLoc)->pProbe;
    AssertPtrReturnVoid(pProbe);
    PVTGOBJHDR      pVtgHdr  = (PVTGOBJHDR)((uintptr_t)pProbe + pProbe->offObjHdr);
    AssertPtrReturnVoid(pVtgHdr);
    PVTGDESCARGLIST pArgList = (PVTGDESCARGLIST)((uintptr_t)pVtgHdr + pVtgHdr->offArgLists + pProbe->offArgList);
    AssertPtrReturnVoid(pArgList);
    if (!pArgList->fHaveLargeArgs)
        dtrace_probe(pVtgProbeLoc->idProbe, uArg0, uArg1, uArg2, uArg3, uArg4);
    else
    {
        uintptr_t *auSrcArgs = &uArg0;
        uint32_t   iSrcArg   = 0;
        uint32_t   iDstArg   = 0;
        uint64_t   au64DstArgs[5];

        while (   iDstArg < RT_ELEMENTS(au64DstArgs)
               && iSrcArg < pArgList->cArgs)
        {
            au64DstArgs[iDstArg] = auSrcArgs[iSrcArg];
            if (VTG_TYPE_IS_LARGE(pArgList->aArgs[iDstArg].fType))
                au64DstArgs[iDstArg] |= (uint64_t)auSrcArgs[++iSrcArg] << 32;
            iSrcArg++;
            iDstArg++;
        }
        while (iDstArg < RT_ELEMENTS(au64DstArgs))
            au64DstArgs[iDstArg++] = auSrcArgs[iSrcArg++];

        pStackData->u.ProbeFireKernel.pauStackArgs = &auSrcArgs[iSrcArg];
        dtrace_probe(pVtgProbeLoc->idProbe, au64DstArgs[0], au64DstArgs[1], au64DstArgs[2], au64DstArgs[3], au64DstArgs[4]);
    }
#else
    dtrace_probe(pVtgProbeLoc->idProbe, uArg0, uArg1, uArg2, uArg3, uArg4);
#endif

    VBDT_CLEAR_STACK_DATA();
    SUPDRV_RESTORE_EFL_AC();
    LOG_DTRACE(("%s: returns\n", __FUNCTION__));
}


/**
 * interface_method_impl{SUPDRVTRACERREG,pfnProbeFireUser}
 */
static DECLCALLBACK(void) vboxDtTOps_ProbeFireUser(PCSUPDRVTRACERREG pThis, PSUPDRVSESSION pSession, PCSUPDRVTRACERUSRCTX pCtx,
                                                   PCVTGOBJHDR pVtgHdr, PCVTGPROBELOC pProbeLocRO)
{
    RT_NOREF(pThis, pSession);
    LOG_DTRACE(("%s: %p / %p\n", __FUNCTION__, pCtx, pCtx->idProbe));
    AssertPtrReturnVoid(pProbeLocRO);
    AssertPtrReturnVoid(pVtgHdr);

    SUPDRV_SAVE_EFL_AC();
    VBDT_SETUP_STACK_DATA(kVBoxDtCaller_ProbeFireUser);

    if (pCtx->cBits == 32)
    {
        pStackData->u.ProbeFireUser.pCtx   = pCtx;
        pStackData->u.ProbeFireUser.offArg = 0;

#if ARCH_BITS == 64 || defined(RT_OS_DARWIN)
        /*
         * Combine two 32-bit arguments into one 64-bit argument where needed.
         */
        PVTGDESCPROBE   pProbeDesc = pProbeLocRO->pProbe;
        AssertPtrReturnVoid(pProbeDesc);
        PVTGDESCARGLIST pArgList   = (PVTGDESCARGLIST)((uintptr_t)pVtgHdr + pVtgHdr->offArgLists + pProbeDesc->offArgList);
        AssertPtrReturnVoid(pArgList);

        if (!pArgList->fHaveLargeArgs)
            dtrace_probe(pCtx->idProbe,
                         pCtx->u.X86.aArgs[0],
                         pCtx->u.X86.aArgs[1],
                         pCtx->u.X86.aArgs[2],
                         pCtx->u.X86.aArgs[3],
                         pCtx->u.X86.aArgs[4]);
        else
        {
            uint32_t const *auSrcArgs = &pCtx->u.X86.aArgs[0];
            uint32_t        iSrcArg   = 0;
            uint32_t        iDstArg   = 0;
            uint64_t        au64DstArgs[5];

            while (   iDstArg < RT_ELEMENTS(au64DstArgs)
                   && iSrcArg < pArgList->cArgs)
            {
                au64DstArgs[iDstArg] = auSrcArgs[iSrcArg];
                if (VTG_TYPE_IS_LARGE(pArgList->aArgs[iDstArg].fType))
                    au64DstArgs[iDstArg] |= (uint64_t)auSrcArgs[++iSrcArg] << 32;
                iSrcArg++;
                iDstArg++;
            }
            while (iDstArg < RT_ELEMENTS(au64DstArgs))
                au64DstArgs[iDstArg++] = auSrcArgs[iSrcArg++];

            pStackData->u.ProbeFireUser.offArg = iSrcArg - RT_ELEMENTS(au64DstArgs);
            dtrace_probe(pCtx->idProbe, au64DstArgs[0], au64DstArgs[1], au64DstArgs[2], au64DstArgs[3], au64DstArgs[4]);
        }
#else
        dtrace_probe(pCtx->idProbe,
                     pCtx->u.X86.aArgs[0],
                     pCtx->u.X86.aArgs[1],
                     pCtx->u.X86.aArgs[2],
                     pCtx->u.X86.aArgs[3],
                     pCtx->u.X86.aArgs[4]);
#endif
    }
    else if (pCtx->cBits == 64)
    {
        pStackData->u.ProbeFireUser.pCtx   = pCtx;
        pStackData->u.ProbeFireUser.offArg = 0;
        dtrace_probe(pCtx->idProbe,
                     pCtx->u.Amd64.aArgs[0],
                     pCtx->u.Amd64.aArgs[1],
                     pCtx->u.Amd64.aArgs[2],
                     pCtx->u.Amd64.aArgs[3],
                     pCtx->u.Amd64.aArgs[4]);
    }
    else
        AssertFailed();

    VBDT_CLEAR_STACK_DATA();
    SUPDRV_RESTORE_EFL_AC();
    LOG_DTRACE(("%s: returns\n", __FUNCTION__));
}


/**
 * interface_method_impl{SUPDRVTRACERREG,pfnTracerOpen}
 */
static DECLCALLBACK(int) vboxDtTOps_TracerOpen(PCSUPDRVTRACERREG pThis, PSUPDRVSESSION pSession, uint32_t uCookie,
                                               uintptr_t uArg, uintptr_t *puSessionData)
{
    NOREF(pThis); NOREF(pSession); NOREF(uCookie); NOREF(uArg);
    *puSessionData = 0;
    return VERR_NOT_SUPPORTED;
}


/**
 * interface_method_impl{SUPDRVTRACERREG,pfnTracerClose}
 */
static DECLCALLBACK(int) vboxDtTOps_TracerIoCtl(PCSUPDRVTRACERREG pThis, PSUPDRVSESSION pSession, uintptr_t uSessionData,
                                                uintptr_t uCmd, uintptr_t uArg, int32_t *piRetVal)
{
    NOREF(pThis); NOREF(pSession); NOREF(uSessionData);
    NOREF(uCmd); NOREF(uArg); NOREF(piRetVal);
    return VERR_NOT_SUPPORTED;
}


/**
 * interface_method_impl{SUPDRVTRACERREG,pfnTracerClose}
 */
static DECLCALLBACK(void) vboxDtTOps_TracerClose(PCSUPDRVTRACERREG pThis, PSUPDRVSESSION pSession, uintptr_t uSessionData)
{
    NOREF(pThis); NOREF(pSession); NOREF(uSessionData);
    return;
}


/**
 * interface_method_impl{SUPDRVTRACERREG,pfnProviderRegister}
 */
static DECLCALLBACK(int) vboxDtTOps_ProviderRegister(PCSUPDRVTRACERREG pThis, PSUPDRVVDTPROVIDERCORE pCore)
{
    RT_NOREF(pThis);
    LOG_DTRACE(("%s: %p %s/%s\n", __FUNCTION__, pThis, pCore->pszModName, pCore->pszName));
    AssertReturn(pCore->TracerData.DTrace.idProvider == 0, VERR_INTERNAL_ERROR_3);

    PVTGDESCPROVIDER    pDesc = pCore->pDesc;
    dtrace_pattr_t      DtAttrs;
    vboxDtVtgConvAttr(&DtAttrs.dtpa_provider, &pDesc->AttrSelf);
    vboxDtVtgConvAttr(&DtAttrs.dtpa_mod,      &pDesc->AttrModules);
    vboxDtVtgConvAttr(&DtAttrs.dtpa_func,     &pDesc->AttrFunctions);
    vboxDtVtgConvAttr(&DtAttrs.dtpa_name,     &pDesc->AttrNames);
    vboxDtVtgConvAttr(&DtAttrs.dtpa_args,     &pDesc->AttrArguments);

    /* Note! DTrace may call us back before dtrace_register returns, so we
             have to point it to pCore->TracerData.DTrace.idProvider. */
    AssertCompile(sizeof(dtrace_provider_id_t) == sizeof(pCore->TracerData.DTrace.idProvider));
    SUPDRV_SAVE_EFL_AC();
    int rc = dtrace_register(pCore->pszName,
                             &DtAttrs,
                             DTRACE_PRIV_KERNEL,
                             NULL /* cred */,
                             &g_vboxDtVtgProvOps,
                             pCore,
                             &pCore->TracerData.DTrace.idProvider);
    SUPDRV_RESTORE_EFL_AC();
    if (!rc)
    {
        LOG_DTRACE(("%s: idProvider=%p\n", __FUNCTION__, pCore->TracerData.DTrace.idProvider));
        AssertPtr(pCore->TracerData.DTrace.idProvider);
        rc = VINF_SUCCESS;
    }
    else
    {
        pCore->TracerData.DTrace.idProvider = 0;
        rc = RTErrConvertFromErrno(FIX_UEK_RC(rc));
    }

    LOG_DTRACE(("%s: returns %Rrc\n", __FUNCTION__, rc));
    return rc;
}


/**
 * interface_method_impl{SUPDRVTRACERREG,pfnProviderDeregister}
 */
static DECLCALLBACK(int) vboxDtTOps_ProviderDeregister(PCSUPDRVTRACERREG pThis, PSUPDRVVDTPROVIDERCORE pCore)
{
    uintptr_t idProvider = pCore->TracerData.DTrace.idProvider;
    RT_NOREF(pThis);
    LOG_DTRACE(("%s: %p / %p\n", __FUNCTION__, pThis, idProvider));
    AssertPtrReturn(idProvider, VERR_INTERNAL_ERROR_3);

    SUPDRV_SAVE_EFL_AC();
    dtrace_invalidate(idProvider);
    int rc = dtrace_unregister(idProvider);
    SUPDRV_RESTORE_EFL_AC();
    if (!rc)
    {
        pCore->TracerData.DTrace.idProvider = 0;
        rc = VINF_SUCCESS;
    }
    else
    {
        AssertMsg(FIX_UEK_RC(rc) == EBUSY, ("%d\n", rc));
        pCore->TracerData.DTrace.fZombie = true;
        rc = VERR_TRY_AGAIN;
    }

    LOG_DTRACE(("%s: returns %Rrc\n", __FUNCTION__, rc));
    return rc;
}


/**
 * interface_method_impl{SUPDRVTRACERREG,pfnProviderDeregisterZombie}
 */
static DECLCALLBACK(int) vboxDtTOps_ProviderDeregisterZombie(PCSUPDRVTRACERREG pThis, PSUPDRVVDTPROVIDERCORE pCore)
{
    uintptr_t idProvider = pCore->TracerData.DTrace.idProvider;
    RT_NOREF(pThis);
    LOG_DTRACE(("%s: %p / %p\n", __FUNCTION__, pThis, idProvider));
    AssertPtrReturn(idProvider, VERR_INTERNAL_ERROR_3);
    Assert(pCore->TracerData.DTrace.fZombie);

    SUPDRV_SAVE_EFL_AC();
    int rc = dtrace_unregister(idProvider);
    SUPDRV_RESTORE_EFL_AC();
    if (!rc)
    {
        pCore->TracerData.DTrace.idProvider = 0;
        rc = VINF_SUCCESS;
    }
    else
    {
        AssertMsg(FIX_UEK_RC(rc) == EBUSY, ("%d\n", rc));
        rc = VERR_TRY_AGAIN;
    }

    LOG_DTRACE(("%s: returns %Rrc\n", __FUNCTION__, rc));
    return rc;
}



/**
 * The tracer registration record of the VBox DTrace implementation
 */
static SUPDRVTRACERREG g_VBoxDTraceReg =
{
    SUPDRVTRACERREG_MAGIC,
    SUPDRVTRACERREG_VERSION,
    vboxDtTOps_ProbeFireKernel,
    vboxDtTOps_ProbeFireUser,
    vboxDtTOps_TracerOpen,
    vboxDtTOps_TracerIoCtl,
    vboxDtTOps_TracerClose,
    vboxDtTOps_ProviderRegister,
    vboxDtTOps_ProviderDeregister,
    vboxDtTOps_ProviderDeregisterZombie,
    SUPDRVTRACERREG_MAGIC
};



/**
 * Module initialization code.
 */
const SUPDRVTRACERREG * VBOXCALL supdrvDTraceInit(void)
{
#if defined(RT_OS_DARWIN) || defined(RT_OS_LINUX)
    /*
     * Resolve the kernel symbols we need.
     */
# ifndef RT_OS_LINUX
    RTDBGKRNLINFO hKrnlInfo;
    int rc = RTR0DbgKrnlInfoOpen(&hKrnlInfo, 0);
    if (RT_FAILURE(rc))
    {
        SUPR0Printf("supdrvDTraceInit: RTR0DbgKrnlInfoOpen failed with rc=%d.\n", rc);
        return NULL;
    }
# endif

    unsigned i;
    for (i = 0; i < RT_ELEMENTS(g_aDTraceFunctions); i++)
    {
# ifndef RT_OS_LINUX
        rc = RTR0DbgKrnlInfoQuerySymbol(hKrnlInfo, NULL, g_aDTraceFunctions[i].pszName,
                                        (void **)g_aDTraceFunctions[i].ppfn);
        if (RT_FAILURE(rc))
        {
            SUPR0Printf("supdrvDTraceInit: Failed to resolved '%s' (rc=%Rrc, i=%u).\n", g_aDTraceFunctions[i].pszName, rc, i);
            break;
        }
# else /* RT_OS_LINUX */
        uintptr_t ulAddr = (uintptr_t)__symbol_get(g_aDTraceFunctions[i].pszName);
        if (!ulAddr)
        {
            SUPR0Printf("supdrvDTraceInit: Failed to resolved '%s' (i=%u).\n", g_aDTraceFunctions[i].pszName, i);
            while (i-- > 0)
            {
                __symbol_put(g_aDTraceFunctions[i].pszName);
                *g_aDTraceFunctions[i].ppfn = NULL;
            }
            return NULL;
        }
        *g_aDTraceFunctions[i].ppfn = (PFNRT)ulAddr;
# endif /* RT_OS_LINUX */
    }

# ifndef RT_OS_LINUX
    RTR0DbgKrnlInfoRelease(hKrnlInfo);
    if (RT_FAILURE(rc))
        return NULL;
# endif
#endif

    return &g_VBoxDTraceReg;
}

/**
 * Module teardown code.
 */
void VBOXCALL supdrvDTraceFini(void)
{
#ifdef RT_OS_LINUX
    /* Release the references. */
    unsigned i;
    for (i = 0; i < RT_ELEMENTS(g_aDTraceFunctions); i++)
        if (*g_aDTraceFunctions[i].ppfn)
        {
            __symbol_put(g_aDTraceFunctions[i].pszName);
            *g_aDTraceFunctions[i].ppfn = NULL;
        }
#endif
}

#ifndef VBOX_WITH_NATIVE_DTRACE
# error "VBOX_WITH_NATIVE_DTRACE is not defined as it should"
#endif

