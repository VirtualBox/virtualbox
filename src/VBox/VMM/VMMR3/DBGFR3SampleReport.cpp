/* $Id: DBGFR3SampleReport.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * DBGF - Debugger Facility, Sample report creation.
 */

/*
 * Copyright (C) 2021-2024 Oracle and/or its affiliates.
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


/** @page pg_dbgf_sample_report DBGFR3SampleReport - Sample Report Interface
 *
 * @todo
 */


/*********************************************************************************************************************************
*   Header Files                                                                                                                 *
*********************************************************************************************************************************/
#define LOG_GROUP LOG_GROUP_DBGF
#include <VBox/vmm/dbgf.h>
#include "DBGFInternal.h"
#include <VBox/vmm/mm.h>
#include <VBox/vmm/uvm.h>
#include <VBox/vmm/vm.h>
#include <VBox/err.h>
#include <VBox/log.h>

#include <iprt/assert.h>
#include <iprt/semaphore.h>
#include <iprt/list.h>
#include <iprt/mem.h>
#include <iprt/time.h>
#include <iprt/timer.h>
#include <iprt/sort.h>
#include <iprt/string.h>
#include <iprt/stream.h>


/*********************************************************************************************************************************
*   Defined Constants And Macros                                                                                                 *
*********************************************************************************************************************************/

/** Maximum stack frame depth. */
#define DBGF_SAMPLE_REPORT_FRAME_DEPTH_MAX 64


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/

/**
 * Sample report state.
 */
typedef enum DBGFSAMPLEREPORTSTATE
{
    /** Invalid state do not use. */
    DBGFSAMPLEREPORTSTATE_INVALID = 0,
    /** The sample report is ready to run. */
    DBGFSAMPLEREPORTSTATE_READY,
    /** The sampple process is running currently. */
    DBGFSAMPLEREPORTSTATE_RUNNING,
    /** The sample process is about to stop. */
    DBGFSAMPLEREPORTSTATE_STOPPING,
    /** 32bit hack. */
    DBGFSAMPLEREPORTSTATE_32BIT_HACK = 0x7fffffff
} DBGFSAMPLEREPORTSTATE;

/** Pointer to a single sample frame. */
typedef struct DBGFSAMPLEFRAME *PDBGFSAMPLEFRAME;

/**
 * Frame information.
 */
typedef struct DBGFSAMPLEFRAME
{
    /** Frame address. */
    DBGFADDRESS                     AddrFrame;
    /** Number of times this frame was encountered. */
    uint64_t                        cSamples;
    /** Pointer to the array of frames below in the call stack. */
    PDBGFSAMPLEFRAME                paFrames;
    /** Number of valid entries in the frams array. */
    uint64_t                        cFramesValid;
    /** Maximum number of entries in the frames array. */
    uint64_t                        cFramesMax;
} DBGFSAMPLEFRAME;
typedef const DBGFSAMPLEFRAME *PCDBGFSAMPLEFRAME;


/**
 * Per VCPU sample report data.
 */
typedef struct DBGFSAMPLEREPORTVCPU
{
    /** The root frame. */
    DBGFSAMPLEFRAME                 FrameRoot;
} DBGFSAMPLEREPORTVCPU;
/** Pointer to the per VCPU sample report data. */
typedef DBGFSAMPLEREPORTVCPU *PDBGFSAMPLEREPORTVCPU;
/** Pointer to const per VCPU sample report data. */
typedef const DBGFSAMPLEREPORTVCPU *PCDBGFSAMPLEREPORTVCPU;


/**
 * Internal sample report instance data.
 */
typedef struct DBGFSAMPLEREPORTINT
{
    /** References hold for this trace module. */
    volatile uint32_t                cRefs;
    /** The user mode VM handle. */
    PUVM                             pUVM;
    /** State the sample report is currently in. */
    volatile DBGFSAMPLEREPORTSTATE   enmState;
    /** Flags passed during report creation. */
    uint32_t                         fFlags;
    /** The timer handle for the sample report collector. */
    PRTTIMER                         hTimer;
    /** The sample interval in microseconds. */
    uint32_t                         cSampleIntervalUs;
    /** THe progress callback if set. */
    PFNDBGFPROGRESS                  pfnProgress;
    /** Opaque user data passed with the progress callback. */
    void                             *pvProgressUser;
    /** Number of microseconds left for sampling. */
    uint64_t                         cSampleUsLeft;
    /** The report created after sampling was stopped. */
    char                             *pszReport;
    /** Number of EMTs having a guest sample operation queued. */
    volatile uint32_t                cEmtsActive;
    /** Array of per VCPU samples collected. */
    DBGFSAMPLEREPORTVCPU             aCpus[1];
} DBGFSAMPLEREPORTINT;
/** Pointer to a const internal trace module instance data. */
typedef DBGFSAMPLEREPORTINT *PDBGFSAMPLEREPORTINT;
/** Pointer to a const internal trace module instance data. */
typedef const DBGFSAMPLEREPORTINT *PCDBGFSAMPLEREPORTINT;


/**
 * Structure to pass to DBGFR3Info() and for doing all other
 * output during fatal dump.
 */
typedef struct DBGFSAMPLEREPORTINFOHLP
{
    /** The helper core. */
    DBGFINFOHLP Core;
    /** Pointer to the allocated character buffer. */
    char        *pachBuf;
    /** Number of bytes allocated for the character buffer. */
    size_t      cbBuf;
    /** Offset into the character buffer. */
    size_t      offBuf;
} DBGFSAMPLEREPORTINFOHLP, *PDBGFSAMPLEREPORTINFOHLP;
/** Pointer to a DBGFSAMPLEREPORTINFOHLP structure. */
typedef const DBGFSAMPLEREPORTINFOHLP *PCDBGFSAMPLEREPORTINFOHLP;


/*********************************************************************************************************************************
*   Internal Functions                                                                                                           *
*********************************************************************************************************************************/

/**
 * Print formatted string.
 *
 * @param   pHlp        Pointer to this structure.
 * @param   pszFormat   The format string.
 * @param   ...         Arguments.
 */
static DECLCALLBACK(void) dbgfR3SampleReportInfoHlp_pfnPrintf(PCDBGFINFOHLP pHlp, const char *pszFormat, ...)
{
    va_list args;
    va_start(args, pszFormat);
    pHlp->pfnPrintfV(pHlp, pszFormat, args);
    va_end(args);
}


/**
 * Print formatted string.
 *
 * @param   pHlp        Pointer to this structure.
 * @param   pszFormat   The format string.
 * @param   args        Argument list.
 */
static DECLCALLBACK(void) dbgfR3SampleReportInfoHlp_pfnPrintfV(PCDBGFINFOHLP pHlp, const char *pszFormat, va_list args)
{
    PDBGFSAMPLEREPORTINFOHLP pMyHlp = (PDBGFSAMPLEREPORTINFOHLP)pHlp;

    va_list args2;
    va_copy(args2, args);
    ssize_t cch = RTStrPrintf2V(&pMyHlp->pachBuf[pMyHlp->offBuf], pMyHlp->cbBuf - pMyHlp->offBuf, pszFormat, args2);
    if (cch < 0)
    {
        /* Try increase the buffer. */
        char *pachBufNew = (char *)RTMemRealloc(pMyHlp->pachBuf, pMyHlp->cbBuf + RT_MAX(_4K, -cch));
        if (pachBufNew)
        {
            pMyHlp->pachBuf = pachBufNew;
            pMyHlp->cbBuf  += RT_MAX(_4K, -cch);
            cch = RTStrPrintf2V(&pMyHlp->pachBuf[pMyHlp->offBuf], pMyHlp->cbBuf - pMyHlp->offBuf, pszFormat, args2);
            Assert(cch > 0);
            pMyHlp->offBuf += cch;
        }
    }
    else
        pMyHlp->offBuf += cch;
    va_end(args2);
}


/**
 * Initializes the sample report output helper.
 *
 * @param   pHlp        The structure to initialize.
 */
static void dbgfR3SampleReportInfoHlpInit(PDBGFSAMPLEREPORTINFOHLP pHlp)
{
    RT_BZERO(pHlp, sizeof(*pHlp));

    pHlp->Core.pfnPrintf      = dbgfR3SampleReportInfoHlp_pfnPrintf;
    pHlp->Core.pfnPrintfV     = dbgfR3SampleReportInfoHlp_pfnPrintfV;
    pHlp->Core.pfnGetOptError = DBGFR3InfoGenericGetOptError;

    pHlp->pachBuf = (char *)RTMemAllocZ(_4K);
    if (pHlp->pachBuf)
        pHlp->cbBuf = _4K;
}


/**
 * Deletes the sample report output helper.
 *
 * @param   pHlp        The structure to delete.
 */
static void dbgfR3SampleReportInfoHlpDelete(PDBGFSAMPLEREPORTINFOHLP pHlp)
{
    if (pHlp->pachBuf)
        RTMemFree(pHlp->pachBuf);
}


/**
 * Frees the given frame and all its descendants.
 *
 * @param   pFrame                  The frame to free.
 */
static void dbgfR3SampleReportFrameFree(PDBGFSAMPLEFRAME pFrame)
{
    for (uint32_t i = 0; i < pFrame->cFramesValid; i++)
        dbgfR3SampleReportFrameFree(&pFrame->paFrames[i]); /** @todo Recursion... */

    MMR3HeapFree(pFrame->paFrames);
    memset(pFrame, 0, sizeof(*pFrame));
}


/**
 * Destroys the given sample report freeing all allocated resources.
 *
 * @param   pThis                   The sample report instance data.
 */
static void dbgfR3SampleReportDestroy(PDBGFSAMPLEREPORTINT pThis)
{
    for (uint32_t i = 0; i < pThis->pUVM->cCpus; i++)
        dbgfR3SampleReportFrameFree(&pThis->aCpus[i].FrameRoot);
    MMR3HeapFree(pThis);
}


/**
 * Returns the frame belonging to the given address or NULL if not found.
 *
 * @returns Pointer to the descendant frame or NULL if not found.
 * @param   pFrame                  The frame to look for descendants with the matching address.
 * @param   pAddr                   The guest address to search for.
 */
static PDBGFSAMPLEFRAME dbgfR3SampleReportFrameFindByAddr(PCDBGFSAMPLEFRAME pFrame, PCDBGFADDRESS pAddr)
{
    for (uint32_t i = 0; i < pFrame->cFramesValid; i++)
        if (!memcmp(pAddr, &pFrame->paFrames[i].AddrFrame, sizeof(*pAddr)))
            return &pFrame->paFrames[i];

    return NULL;
}


/**
 * Adds the given address to as a descendant to the given frame.
 *
 * @returns Pointer to the newly inserted frame identified by the given address.
 * @param   pUVM                    The usermode VM handle.
 * @param   pFrame                  The frame to add the new one to as a descendant.
 * @param   pAddr                   The guest address to add.
 */
static PDBGFSAMPLEFRAME dbgfR3SampleReportAddFrameByAddr(PUVM pUVM, PDBGFSAMPLEFRAME pFrame, PCDBGFADDRESS pAddr)
{
    if (pFrame->cFramesValid == pFrame->cFramesMax)
    {
        uint32_t cFramesMaxNew = pFrame->cFramesMax + 10;
        PDBGFSAMPLEFRAME paFramesNew = NULL;
        if (pFrame->paFrames)
            paFramesNew = (PDBGFSAMPLEFRAME)MMR3HeapRealloc(pFrame->paFrames, sizeof(*pFrame->paFrames) * cFramesMaxNew);
        else
            paFramesNew = (PDBGFSAMPLEFRAME)MMR3HeapAllocZU(pUVM, MM_TAG_DBGF, sizeof(*pFrame->paFrames) * cFramesMaxNew);

        if (!paFramesNew)
            return NULL;

        pFrame->cFramesMax = cFramesMaxNew;
        pFrame->paFrames   = paFramesNew;
    }

    PDBGFSAMPLEFRAME pFrameNew = &pFrame->paFrames[pFrame->cFramesValid++];
    pFrameNew->AddrFrame    = *pAddr;
    pFrameNew->cSamples     = 1;
    pFrameNew->paFrames     = NULL;
    pFrameNew->cFramesMax   = 0;
    pFrameNew->cFramesValid = 0;
    return pFrameNew;
}


/**
 * @copydoc FNRTSORTCMP
 */
static DECLCALLBACK(int) dbgfR3SampleReportFrameSortCmp(void const *pvElement1, void const *pvElement2, void *pvUser)
{
    RT_NOREF(pvUser);
    PCDBGFSAMPLEFRAME pFrame1 = (PCDBGFSAMPLEFRAME)pvElement1;
    PCDBGFSAMPLEFRAME pFrame2 = (PCDBGFSAMPLEFRAME)pvElement2;

    if (pFrame1->cSamples < pFrame2->cSamples)
        return 1;
    if (pFrame1->cSamples > pFrame2->cSamples)
        return -1;

    return 0;
}


/**
 * Dumps a single given frame to the release log.
 *
 * @param   pHlp                    The debug info helper used for printing.
 * @param   pUVM                    The usermode VM handle.
 * @param   pFrame                  The frame to dump.
 * @param   idxFrame                The frame number.
 */
static void dbgfR3SampleReportDumpFrame(PCDBGFINFOHLP pHlp, PUVM pUVM, PCDBGFSAMPLEFRAME pFrame, uint32_t idxFrame)
{
    RTGCINTPTR offDisp;
    RTDBGMOD hMod;
    RTDBGSYMBOL SymPC;

    if (DBGFR3AddrIsValid(pUVM, &pFrame->AddrFrame))
    {
        int rc = DBGFR3AsSymbolByAddr(pUVM, DBGF_AS_GLOBAL, &pFrame->AddrFrame,
                                      RTDBGSYMADDR_FLAGS_LESS_OR_EQUAL | RTDBGSYMADDR_FLAGS_SKIP_ABS_IN_DEFERRED,
                                      &offDisp, &SymPC, &hMod);
        if (RT_SUCCESS(rc))
        {
            const char *pszModName = hMod != NIL_RTDBGMOD ? RTDbgModName(hMod) : NULL;

            pHlp->pfnPrintf(pHlp,
                            "%*s%RU64 %s+%llx (%s) [%RGv]\n", idxFrame * 4, " ",
                                                              pFrame->cSamples,
                                                              SymPC.szName, offDisp,
                                                              hMod ? pszModName : "",
                                                              pFrame->AddrFrame.FlatPtr);
            RTDbgModRelease(hMod);
        }
        else
            pHlp->pfnPrintf(pHlp, "%*s%RU64 %RGv\n", idxFrame * 4, " ", pFrame->cSamples, pFrame->AddrFrame.FlatPtr);
    }
    else
        pHlp->pfnPrintf(pHlp, "%*s%RU64 %RGv\n", idxFrame * 4, " ", pFrame->cSamples, pFrame->AddrFrame.FlatPtr);

    /* Sort by sample count. */
    RTSortShell(pFrame->paFrames, pFrame->cFramesValid, sizeof(*pFrame->paFrames), dbgfR3SampleReportFrameSortCmp, NULL);

    for (uint32_t i = 0; i < pFrame->cFramesValid; i++)
        dbgfR3SampleReportDumpFrame(pHlp, pUVM, &pFrame->paFrames[i], idxFrame + 1);
}


/**
 * Worker for dbgfR3SampleReportTakeSample(), doing the work in an EMT rendezvous point on
 * each VCPU.
 *
 * @param   pThis                    Pointer to the sample report instance.
 */
static DECLCALLBACK(void) dbgfR3SampleReportSample(PDBGFSAMPLEREPORTINT pThis)
{
    PVM pVM = pThis->pUVM->pVM;
    PVMCPU pVCpu = VMMGetCpu(pVM);

    PCDBGFSTACKFRAME pFrameFirst;
    int rc = DBGFR3StackWalkBegin(pThis->pUVM, pVCpu->idCpu, DBGFCODETYPE_GUEST, &pFrameFirst);
    if (RT_SUCCESS(rc))
    {
        DBGFADDRESS aFrameAddresses[DBGF_SAMPLE_REPORT_FRAME_DEPTH_MAX];
        uint32_t idxFrame = 0;

        PDBGFSAMPLEFRAME pFrame = &pThis->aCpus[pVCpu->idCpu].FrameRoot;
        pFrame->cSamples++;

        for (PCDBGFSTACKFRAME pStackFrame = pFrameFirst;
             pStackFrame && idxFrame < RT_ELEMENTS(aFrameAddresses);
             pStackFrame = DBGFR3StackWalkNext(pStackFrame))
        {
            if (pThis->fFlags & DBGF_SAMPLE_REPORT_F_STACK_REVERSE)
            {
                PDBGFSAMPLEFRAME pFrameNext = dbgfR3SampleReportFrameFindByAddr(pFrame, &pStackFrame->AddrPC);
                if (!pFrameNext)
                    pFrameNext = dbgfR3SampleReportAddFrameByAddr(pThis->pUVM, pFrame, &pStackFrame->AddrPC);
                else
                    pFrameNext->cSamples++;

                pFrame = pFrameNext;
            }
            else
                aFrameAddresses[idxFrame] = pStackFrame->AddrPC;

            idxFrame++;
        }

        DBGFR3StackWalkEnd(pFrameFirst);

        if (!(pThis->fFlags & DBGF_SAMPLE_REPORT_F_STACK_REVERSE))
        {
            /* Walk the frame stack backwards and construct the call stack. */
            while (idxFrame--)
            {
                PDBGFSAMPLEFRAME pFrameNext = dbgfR3SampleReportFrameFindByAddr(pFrame, &aFrameAddresses[idxFrame]);
                if (!pFrameNext)
                    pFrameNext = dbgfR3SampleReportAddFrameByAddr(pThis->pUVM, pFrame, &aFrameAddresses[idxFrame]);
                else
                    pFrameNext->cSamples++;

                pFrame = pFrameNext;
            }
        }
    }
    else
        LogRelMax(10, ("Sampling guest stack on VCPU %u failed with rc=%Rrc\n", pVCpu->idCpu, rc));

    /* Last EMT finishes the report when sampling was stopped. */
    uint32_t cEmtsActive = ASMAtomicDecU32(&pThis->cEmtsActive);
    if (   ASMAtomicReadU32((volatile uint32_t *)&pThis->enmState) == DBGFSAMPLEREPORTSTATE_STOPPING
        && !cEmtsActive)
    {
        rc = RTTimerDestroy(pThis->hTimer); AssertRC(rc); RT_NOREF(rc);
        pThis->hTimer = NULL;

        DBGFSAMPLEREPORTINFOHLP Hlp;
        PCDBGFINFOHLP           pHlp = &Hlp.Core;

        dbgfR3SampleReportInfoHlpInit(&Hlp);

        /* Some early dump code. */
        for (uint32_t i = 0; i < pThis->pUVM->cCpus; i++)
        {
            PCDBGFSAMPLEREPORTVCPU pSampleVCpu = &pThis->aCpus[i];

            pHlp->pfnPrintf(pHlp, "Sample report for vCPU %u:\n", i);
            dbgfR3SampleReportDumpFrame(pHlp, pThis->pUVM, &pSampleVCpu->FrameRoot, 0);
        }

        /* Shameless copy from VMMGuruMeditation.cpp */
        static struct
        {
            const char *pszInfo;
            const char *pszArgs;
        } const     aInfo[] =
        {
            { "mappings",        NULL },
            { "mode",            "all" },
            { "handlers",        "phys virt hyper stats" },
            { "timers",          NULL },
            { "activetimers",    NULL },
        };
        for (unsigned i = 0; i < RT_ELEMENTS(aInfo); i++)
        {
            pHlp->pfnPrintf(pHlp,
                            "!!\n"
                            "!! {%s, %s}\n"
                            "!!\n",
                            aInfo[i].pszInfo, aInfo[i].pszArgs);
            DBGFR3Info(pVM->pUVM, aInfo[i].pszInfo, aInfo[i].pszArgs, pHlp);
        }

        /* All other info items */
        DBGFR3InfoMulti(pVM,
                        "*",
                        "mappings|hma|cpum|cpumguest|cpumguesthwvirt|cpumguestinstr|cpumhyper|cpumhost|cpumvmxfeat|mode|cpuid"
                        "|pgmpd|pgmcr3|timers|activetimers|handlers|help|cfgm",
                        "!!\n"
                        "!! {%s}\n"
                        "!!\n",
                        pHlp);


        /* done */
        pHlp->pfnPrintf(pHlp,
                        "!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!\n");

        if (pThis->pszReport)
            RTMemFree(pThis->pszReport);
        pThis->pszReport = Hlp.pachBuf;
        Hlp.pachBuf = NULL;
        dbgfR3SampleReportInfoHlpDelete(&Hlp);

        ASMAtomicXchgU32((volatile uint32_t *)&pThis->enmState, DBGFSAMPLEREPORTSTATE_READY);

        if (pThis->pfnProgress)
        {
            pThis->pfnProgress(pThis->pvProgressUser, 100);
            pThis->pfnProgress    = NULL;
            pThis->pvProgressUser = NULL;
        }

        DBGFR3SampleReportRelease(pThis);
    }
}


/**
 * @copydoc FNRTTIMER
 */
static DECLCALLBACK(void) dbgfR3SampleReportTakeSample(PRTTIMER pTimer, void *pvUser, uint64_t iTick)
{
    PDBGFSAMPLEREPORTINT pThis = (PDBGFSAMPLEREPORTINT)pvUser;

    if (pThis->cSampleUsLeft != UINT32_MAX)
    {
        int rc = VINF_SUCCESS;
        uint64_t cUsSampled = iTick * pThis->cSampleIntervalUs; /** @todo Wrong if the timer resolution is different from what we've requested. */

        /* Update progress. */
        if (pThis->pfnProgress)
            rc = pThis->pfnProgress(pThis->pvProgressUser, cUsSampled * 99 / pThis->cSampleUsLeft);

        if (   cUsSampled >= pThis->cSampleUsLeft
            || rc == VERR_DBGF_CANCELLED)
        {
            /*
             * Let the EMTs do one last round in order to be able to destroy the timer (can't do this on the timer thread)
             * and gather information from the devices.
             */
            ASMAtomicCmpXchgU32((volatile uint32_t *)&pThis->enmState, DBGFSAMPLEREPORTSTATE_STOPPING,
                                DBGFSAMPLEREPORTSTATE_RUNNING);

            rc = RTTimerStop(pTimer); AssertRC(rc); RT_NOREF(rc);
        }
    }

    ASMAtomicAddU32(&pThis->cEmtsActive, pThis->pUVM->cCpus);

    for (uint32_t i = 0; i < pThis->pUVM->cCpus; i++)
    {
        int rc = VMR3ReqCallVoidNoWait(pThis->pUVM->pVM, i, (PFNRT)dbgfR3SampleReportSample, 1, pThis);
        AssertRC(rc);
        if (RT_FAILURE(rc))
            ASMAtomicDecU32(&pThis->cEmtsActive);
    }
}


/**
 * Creates a new sample report instance for the specified VM.
 *
 * @returns VBox status code.
 * @param   pUVM                    The usermode VM handle.
 * @param   cSampleIntervalUs       The sample interval in micro seconds.
 * @param   fFlags                  Combination of DBGF_SAMPLE_REPORT_F_XXX.
 * @param   phSample                Where to return the handle to the sample report on success.
 */
VMMR3DECL(int) DBGFR3SampleReportCreate(PUVM pUVM, uint32_t cSampleIntervalUs, uint32_t fFlags, PDBGFSAMPLEREPORT phSample)
{
    UVM_ASSERT_VALID_EXT_RETURN(pUVM, VERR_INVALID_VM_HANDLE);
    AssertReturn(!(fFlags & ~DBGF_SAMPLE_REPORT_F_VALID_MASK), VERR_INVALID_PARAMETER);
    AssertPtrReturn(phSample, VERR_INVALID_POINTER);

    int rc = VINF_SUCCESS;
    PDBGFSAMPLEREPORTINT pThis = (PDBGFSAMPLEREPORTINT)MMR3HeapAllocZU(pUVM, MM_TAG_DBGF,
                                                                       RT_UOFFSETOF_DYN(DBGFSAMPLEREPORTINT, aCpus[pUVM->cCpus]));
    if (RT_LIKELY(pThis))
    {
        pThis->cRefs             = 1;
        pThis->pUVM              = pUVM;
        pThis->fFlags            = fFlags;
        pThis->cSampleIntervalUs = cSampleIntervalUs;
        pThis->enmState          = DBGFSAMPLEREPORTSTATE_READY;
        pThis->cEmtsActive       = 0;

        for (uint32_t i = 0; i < pUVM->cCpus; i++)
        {
            pThis->aCpus[i].FrameRoot.paFrames     = NULL;
            pThis->aCpus[i].FrameRoot.cSamples     = 0;
            pThis->aCpus[i].FrameRoot.cFramesValid = 0;
            pThis->aCpus[i].FrameRoot.cFramesMax   = 0;
        }

        *phSample = pThis;
        return VINF_SUCCESS;
    }
    else
        rc = VERR_NO_MEMORY;

    return rc;
}


/**
 * Retains a reference to the given sample report handle.
 *
 * @returns New reference count.
 * @param   hSample                 Sample report handle.
 */
VMMR3DECL(uint32_t) DBGFR3SampleReportRetain(DBGFSAMPLEREPORT hSample)
{
    PDBGFSAMPLEREPORTINT pThis = hSample;
    AssertPtrReturn(pThis, UINT32_MAX);

    uint32_t cRefs = ASMAtomicIncU32(&pThis->cRefs);
    AssertMsg(cRefs > 1 && cRefs < _1M, ("%#x %p\n", cRefs, pThis));
    return cRefs;
}


/**
 * Release a given sample report handle reference.
 *
 * @returns New reference count, on 0 the sample report instance is destroyed.
 * @param   hSample                 Sample report handle.
 */
VMMR3DECL(uint32_t) DBGFR3SampleReportRelease(DBGFSAMPLEREPORT hSample)
{
    PDBGFSAMPLEREPORTINT pThis = hSample;
    if (!pThis)
        return 0;
    AssertPtrReturn(pThis, UINT32_MAX);
    AssertReturn(ASMAtomicReadU32((volatile uint32_t *)&pThis->enmState) == DBGFSAMPLEREPORTSTATE_READY,
                 0);

    uint32_t cRefs = ASMAtomicDecU32(&pThis->cRefs);
    AssertMsg(cRefs < _1M, ("%#x %p\n", cRefs, pThis));
    if (cRefs == 0)
        dbgfR3SampleReportDestroy(pThis);
    return cRefs;
}


/**
 * Starts collecting samples for the given sample report.
 *
 * @returns VBox status code.
 * @param   hSample                 Sample report handle.
 * @param   cSampleUs               Number of microseconds to sample at the interval given during creation.
 *                                  Use UINT32_MAX to sample for an indefinite amount of time.
 * @param   pfnProgress             Optional progress callback.
 * @param   pvUser                  Opaque user data to pass to the progress callback.
 */
VMMR3DECL(int) DBGFR3SampleReportStart(DBGFSAMPLEREPORT hSample, uint64_t cSampleUs, PFNDBGFPROGRESS pfnProgress, void *pvUser)
{
    PDBGFSAMPLEREPORTINT pThis = hSample;

    AssertPtrReturn(pThis, VERR_INVALID_HANDLE);
    AssertReturn(ASMAtomicCmpXchgU32((volatile uint32_t *)&pThis->enmState, DBGFSAMPLEREPORTSTATE_RUNNING, DBGFSAMPLEREPORTSTATE_READY),
                 VERR_INVALID_STATE);

    pThis->pfnProgress    = pfnProgress;
    pThis->pvProgressUser = pvUser;
    pThis->cSampleUsLeft  = cSampleUs;

    /* Try to detect the guest OS first so we can get more accurate symbols and addressing. */
    char szName[64];
    int rc = DBGFR3OSDetect(pThis->pUVM, &szName[0], sizeof(szName));
    if (RT_SUCCESS(rc))
    {
        LogRel(("DBGF/SampleReport: Detected guest OS \"%s\"\n", szName));
        char szVersion[512];
        int rc2 = DBGFR3OSQueryNameAndVersion(pThis->pUVM, NULL, 0, szVersion, sizeof(szVersion));
        if (RT_SUCCESS(rc2))
            LogRel(("DBGF/SampleReport: Version : \"%s\"\n", szVersion));
    }
    else
        LogRel(("DBGF/SampleReport: Couldn't detect guest operating system rc=%Rcr\n", rc));

    /*
     * We keep an additional reference to ensure that the sample report stays alive,
     * it will be dropped when the sample process is stopped.
     */
    DBGFR3SampleReportRetain(pThis);

    rc = RTTimerCreateEx(&pThis->hTimer, pThis->cSampleIntervalUs * 1000,
                         RTTIMER_FLAGS_CPU_ANY | RTTIMER_FLAGS_HIGH_RES,
                         dbgfR3SampleReportTakeSample, pThis);
    if (RT_SUCCESS(rc))
        rc = RTTimerStart(pThis->hTimer, 0 /*u64First*/);
    if (RT_FAILURE(rc))
    {
        if (pThis->hTimer)
        {
            int rc2 = RTTimerDestroy(pThis->hTimer);
            AssertRC(rc2); RT_NOREF(rc2);
            pThis->hTimer = NULL;
        }

        bool fXchg = ASMAtomicCmpXchgU32((volatile uint32_t *)&pThis->enmState, DBGFSAMPLEREPORTSTATE_READY,
                                         DBGFSAMPLEREPORTSTATE_RUNNING);
        Assert(fXchg); RT_NOREF(fXchg);
        DBGFR3SampleReportRelease(pThis);
    }

    return rc;
}


/**
 * Stops collecting samples for the given sample report.
 *
 * @returns VBox status code.
 * @param   hSample                 Sample report handle.
 */
VMMR3DECL(int) DBGFR3SampleReportStop(DBGFSAMPLEREPORT hSample)
{
    PDBGFSAMPLEREPORTINT pThis = hSample;

    AssertPtrReturn(pThis, VERR_INVALID_HANDLE);
    AssertReturn(ASMAtomicCmpXchgU32((volatile uint32_t *)&pThis->enmState, DBGFSAMPLEREPORTSTATE_STOPPING,
                                     DBGFSAMPLEREPORTSTATE_RUNNING),
                 VERR_INVALID_STATE);
    return VINF_SUCCESS;
}


/**
 * Dumps the current sample report to the given file.
 *
 * @returns VBox status code.
 * @retval  VERR_INVALID_STATE if nothing was sampled so far for reporting.
 * @param   hSample                 Sample report handle.
 * @param   pszFilename             The filename to dump the report to.
 */
VMMR3DECL(int) DBGFR3SampleReportDumpToFile(DBGFSAMPLEREPORT hSample, const char *pszFilename)
{
    PDBGFSAMPLEREPORTINT pThis = hSample;

    AssertReturn(pThis->pszReport, VERR_INVALID_STATE);

    PRTSTREAM hStream;
    int rc = RTStrmOpen(pszFilename, "w", &hStream);
    if (RT_SUCCESS(rc))
    {
        rc = RTStrmPutStr(hStream, pThis->pszReport);
        RTStrmClose(hStream);
    }

    return rc;
}

