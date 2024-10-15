/* $Id: VMMGuruMeditation.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VMM - The Virtual Machine Monitor, Guru Meditation Code.
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
 * SPDX-License-Identifier: GPL-3.0-only
 */


/*********************************************************************************************************************************
*   Header Files                                                                                                                 *
*********************************************************************************************************************************/
#define LOG_GROUP LOG_GROUP_VMM
#include <VBox/vmm/vmm.h>
#include <VBox/vmm/pdmapi.h>
#include <VBox/vmm/pdmcritsect.h>
#include <VBox/vmm/trpm.h>
#include <VBox/vmm/dbgf.h>
#include "VMMInternal.h"
#include <VBox/vmm/vm.h>
#include <VBox/vmm/mm.h>
#include <VBox/vmm/iom.h>
#include <VBox/vmm/em.h>

#include <VBox/err.h>
#include <VBox/param.h>
#include <VBox/version.h>
#include <VBox/vmm/hm.h>
#include <iprt/assert.h>
#include <iprt/dbg.h>
#include <iprt/time.h>
#include <iprt/stream.h>
#include <iprt/string.h>
#include <iprt/stdarg.h>


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/
/**
 * Structure to pass to DBGFR3Info() and for doing all other
 * output during fatal dump.
 */
typedef struct VMMR3FATALDUMPINFOHLP
{
    /** The helper core. */
    DBGFINFOHLP Core;
    /** The release logger instance. */
    PRTLOGGER   pRelLogger;
    /** The saved release logger flags. */
    uint32_t    fRelLoggerFlags;
    /** The logger instance. */
    PRTLOGGER   pLogger;
    /** The saved logger flags. */
    uint32_t    fLoggerFlags;
    /** The saved logger destination flags. */
    uint32_t    fLoggerDestFlags;
    /** Whether to output to stderr or not. */
    bool        fStdErr;
    /** Whether we're still recording the summary or not. */
    bool        fRecSummary;
    /** Buffer for the summary. */
    char        szSummary[4096 - 2];
    /** The current summary offset. */
    size_t      offSummary;
    /** Standard error buffer.   */
    char        achStdErrBuf[4096 - 8];
    /** Standard error buffer offset. */
    size_t      offStdErrBuf;
} VMMR3FATALDUMPINFOHLP, *PVMMR3FATALDUMPINFOHLP;
/** Pointer to a VMMR3FATALDUMPINFOHLP structure. */
typedef const VMMR3FATALDUMPINFOHLP *PCVMMR3FATALDUMPINFOHLP;


/**
 * Flushes the content of achStdErrBuf, setting offStdErrBuf to zero.
 *
 * @param   pHlp        The instance to flush.
 */
static void vmmR3FatalDumpInfoHlpFlushStdErr(PVMMR3FATALDUMPINFOHLP pHlp)
{
    size_t cch = pHlp->offStdErrBuf;
    if (cch)
    {
        RTStrmWrite(g_pStdErr, pHlp->achStdErrBuf, cch);
        pHlp->offStdErrBuf = 0;
    }
}

/**
 * @callback_method_impl{FNRTSTROUTPUT, For buffering stderr output.}
 */
static DECLCALLBACK(size_t) vmmR3FatalDumpInfoHlp_BufferedStdErrOutput(void *pvArg, const char *pachChars, size_t cbChars)
{
    PVMMR3FATALDUMPINFOHLP pHlp = (PVMMR3FATALDUMPINFOHLP)pvArg;
    if (cbChars)
    {
        size_t offBuf = pHlp->offStdErrBuf;
        if (cbChars < sizeof(pHlp->achStdErrBuf) - offBuf)
        { /* likely */ }
        else
        {
            vmmR3FatalDumpInfoHlpFlushStdErr(pHlp);
            if (cbChars < sizeof(pHlp->achStdErrBuf))
                offBuf = 0;
            else
            {
                RTStrmWrite(g_pStdErr, pachChars, cbChars);
                return cbChars;
            }
        }
        memcpy(&pHlp->achStdErrBuf[offBuf], pachChars, cbChars);
        pHlp->offStdErrBuf = offBuf + cbChars;
    }
    return cbChars;
}


/**
 * Print formatted string.
 *
 * @param   pHlp        Pointer to this structure.
 * @param   pszFormat   The format string.
 * @param   ...         Arguments.
 */
static DECLCALLBACK(void) vmmR3FatalDumpInfoHlp_pfnPrintf(PCDBGFINFOHLP pHlp, const char *pszFormat, ...)
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
static DECLCALLBACK(void) vmmR3FatalDumpInfoHlp_pfnPrintfV(PCDBGFINFOHLP pHlp, const char *pszFormat, va_list args)
{
    PVMMR3FATALDUMPINFOHLP pMyHlp = (PVMMR3FATALDUMPINFOHLP)pHlp;

    if (pMyHlp->pRelLogger)
    {
        va_list args2;
        va_copy(args2, args);
        RTLogLoggerV(pMyHlp->pRelLogger, pszFormat, args2);
        va_end(args2);
    }
    if (pMyHlp->pLogger)
    {
        va_list args2;
        va_copy(args2, args);
        RTLogLoggerV(pMyHlp->pLogger, pszFormat, args);
        va_end(args2);
    }
    if (pMyHlp->fStdErr)
    {
        va_list args2;
        va_copy(args2, args);
        RTStrFormatV(vmmR3FatalDumpInfoHlp_BufferedStdErrOutput, pMyHlp, NULL, NULL, pszFormat, args2);
        //RTStrmPrintfV(g_pStdErr, pszFormat, args2);
        va_end(args2);
    }
    if (pMyHlp->fRecSummary)
    {
        size_t cchLeft = sizeof(pMyHlp->szSummary) - pMyHlp->offSummary;
        if (cchLeft > 1)
        {
            va_list args2;
            va_copy(args2, args);
            size_t cch = RTStrPrintfV(&pMyHlp->szSummary[pMyHlp->offSummary], cchLeft, pszFormat, args);
            va_end(args2);
            Assert(cch <= cchLeft);
            pMyHlp->offSummary += cch;
        }
    }
}


/**
 * Initializes the fatal dump output helper.
 *
 * @param   pHlp        The structure to initialize.
 */
static void vmmR3FatalDumpInfoHlpInit(PVMMR3FATALDUMPINFOHLP pHlp)
{
    RT_BZERO(pHlp, sizeof(*pHlp));

    pHlp->Core.pfnPrintf      = vmmR3FatalDumpInfoHlp_pfnPrintf;
    pHlp->Core.pfnPrintfV     = vmmR3FatalDumpInfoHlp_pfnPrintfV;
    pHlp->Core.pfnGetOptError = DBGFR3InfoGenericGetOptError;

    /*
     * The loggers.
     */
    pHlp->pRelLogger  = RTLogRelGetDefaultInstance();
#ifdef LOG_ENABLED
    pHlp->pLogger     = RTLogDefaultInstance();
#else
    if (pHlp->pRelLogger)
        pHlp->pLogger = RTLogGetDefaultInstance();
    else
        pHlp->pLogger = RTLogDefaultInstance();
#endif

    if (pHlp->pRelLogger)
    {
        pHlp->fRelLoggerFlags = RTLogGetFlags(pHlp->pRelLogger);
        RTLogChangeFlags(pHlp->pRelLogger, RTLOGFLAGS_BUFFERED, RTLOGFLAGS_DISABLED);
    }

    if (pHlp->pLogger)
    {
        pHlp->fLoggerFlags     = RTLogGetFlags(pHlp->pLogger);
        pHlp->fLoggerDestFlags = RTLogGetDestinations(pHlp->pLogger);
        RTLogChangeFlags(pHlp->pLogger, RTLOGFLAGS_BUFFERED, RTLOGFLAGS_DISABLED);
#ifndef DEBUG_sandervl
        RTLogChangeDestinations(pHlp->pLogger, RTLOGDEST_DEBUGGER, 0);
#endif
    }

    /*
     * Check if we need write to stderr.
     */
    pHlp->fStdErr = (!pHlp->pRelLogger || !(RTLogGetDestinations(pHlp->pRelLogger) & (RTLOGDEST_STDOUT | RTLOGDEST_STDERR)))
                 && (!pHlp->pLogger    || !(RTLogGetDestinations(pHlp->pLogger)    & (RTLOGDEST_STDOUT | RTLOGDEST_STDERR)));
#ifdef DEBUG_sandervl
    pHlp->fStdErr = false; /* takes too long to display here */
#endif
    pHlp->offStdErrBuf = 0;

    /*
     * Init the summary recording.
     */
    pHlp->fRecSummary  = true;
    pHlp->offSummary   = 0;
    pHlp->szSummary[0] = '\0';
}


/**
 * Deletes the fatal dump output helper.
 *
 * @param   pHlp        The structure to delete.
 */
static void vmmR3FatalDumpInfoHlpDelete(PVMMR3FATALDUMPINFOHLP pHlp)
{
    if (pHlp->pRelLogger)
    {
        RTLogFlush(pHlp->pRelLogger);
        RTLogChangeFlags(pHlp->pRelLogger,
                         pHlp->fRelLoggerFlags & RTLOGFLAGS_DISABLED,
                         pHlp->fRelLoggerFlags & RTLOGFLAGS_BUFFERED);
    }

    if (pHlp->pLogger)
    {
        RTLogFlush(pHlp->pLogger);
        RTLogChangeFlags(pHlp->pLogger,
                         pHlp->fLoggerFlags & RTLOGFLAGS_DISABLED,
                         pHlp->fLoggerFlags & RTLOGFLAGS_BUFFERED);
        RTLogChangeDestinations(pHlp->pLogger, 0, pHlp->fLoggerDestFlags & RTLOGDEST_DEBUGGER);
    }

    if (pHlp->fStdErr)
        vmmR3FatalDumpInfoHlpFlushStdErr(pHlp);
}


/**
 * @callback_method_impl{FNVMMEMTRENDEZVOUS}
 */
static DECLCALLBACK(VBOXSTRICTRC) vmmR3FatalDumpRendezvousDoneCallback(PVM pVM, PVMCPU pVCpu, void *pvUser)
{
    VM_FF_CLEAR(pVM, VM_FF_CHECK_VM_STATE);
    RT_NOREF(pVCpu, pvUser);
    return VINF_SUCCESS;
}


/**
 * Dumps the VM state on a fatal error.
 *
 * @param   pVM         The cross context VM structure.
 * @param   pVCpu       The cross context virtual CPU structure.
 * @param   rcErr       VBox status code.
 */
VMMR3DECL(void) VMMR3FatalDump(PVM pVM, PVMCPU pVCpu, int rcErr)
{
    /*
     * Create our output helper and sync it with the log settings.
     * This helper will be used for all the output.
     */
    VMMR3FATALDUMPINFOHLP   Hlp;
    PCDBGFINFOHLP           pHlp = &Hlp.Core;
    vmmR3FatalDumpInfoHlpInit(&Hlp);

    /* Release owned locks to make sure other VCPUs can continue in case they were waiting for one. */
    PDMR3CritSectLeaveAll(pVM);

    /*
     * Header.
     */
    pHlp->pfnPrintf(pHlp,
                    "!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!\n"
                    "!!\n"
                    "!!         VCPU%u: Guru Meditation %d (%Rrc)\n"
                    "!!\n",
                    pVCpu->idCpu, rcErr, rcErr);

    /*
     * Continue according to context.
     */
    bool fDoneHyper = false;
    bool fDoneImport = false;
    switch (rcErr)
    {
        /*
         * Hypervisor errors.
         */
        case VERR_VMM_RING0_ASSERTION:
        case VINF_EM_DBG_HYPER_ASSERTION:
        case VERR_VMM_RING3_CALL_DISABLED:
        case VERR_VMM_WRONG_HM_VMCPU_STATE:
        case VERR_VMM_CONTEXT_HOOK_STILL_ENABLED:
        {
            const char *pszMsg1 = VMMR3GetRZAssertMsg1(pVM);
            while (pszMsg1 && *pszMsg1 == '\n')
                pszMsg1++;
            const char *pszMsg2 = VMMR3GetRZAssertMsg2(pVM);
            while (pszMsg2 && *pszMsg2 == '\n')
                pszMsg2++;
            pHlp->pfnPrintf(pHlp,
                            "%s"
                            "%s",
                            pszMsg1,
                            pszMsg2);
            if (    !pszMsg2
                ||  !*pszMsg2
                ||  strchr(pszMsg2, '\0')[-1] != '\n')
                pHlp->pfnPrintf(pHlp, "\n");
        }
        RT_FALL_THRU();
        case VERR_TRPM_DONT_PANIC:
        case VERR_TRPM_PANIC:
        case VINF_EM_RAW_STALE_SELECTOR:
        case VINF_EM_RAW_IRET_TRAP:
        case VINF_EM_DBG_HYPER_BREAKPOINT:
        case VINF_EM_DBG_HYPER_STEPPED:
        case VINF_EM_TRIPLE_FAULT:
        case VERR_VMM_HYPER_CR3_MISMATCH:
        case VERR_VMM_LONG_JMP_ERROR:
        {
#if defined(VBOX_VMM_TARGET_ARMV8)
            AssertReleaseFailed();
#else
            /*
             * Active trap? This is only of partial interest when in hardware
             * assisted virtualization mode, thus the different messages.
             */
            TRPMEVENT       enmType;
            uint8_t         u8TrapNo   =       0xce;
            uint32_t        uErrorCode = 0xdeadface;
            RTGCUINTPTR     uCR2       = 0xdeadface;
            uint8_t         cbInstr    = UINT8_MAX;
            bool            fIcebp     = false;
            int rc2 = TRPMQueryTrapAll(pVCpu, &u8TrapNo, &enmType, &uErrorCode, &uCR2, &cbInstr, &fIcebp);
            if (RT_SUCCESS(rc2))
                pHlp->pfnPrintf(pHlp,
                                "!! ACTIVE TRAP=%02x ERRCD=%RX32 CR2=%RGv PC=%RGr Type=%d cbInstr=%02x fIcebp=%RTbool (Guest!)\n",
                                u8TrapNo, uErrorCode, uCR2, CPUMGetGuestRIP(pVCpu), enmType, cbInstr, fIcebp);

            /*
             * Dump the relevant hypervisor registers and stack.
             */
            if (rcErr == VERR_VMM_RING0_ASSERTION)
            {
                /* Dump the jmpbuf.  */
                pHlp->pfnPrintf(pHlp,
                                "!!\n"
                                "!! AssertJmpBuf:\n"
                                "!!\n");
                pHlp->pfnPrintf(pHlp,
                                "UnwindSp=%RHv UnwindRetSp=%RHv UnwindBp=%RHv UnwindPc=%RHv\n",
                                pVCpu->vmm.s.AssertJmpBuf.UnwindSp,
                                pVCpu->vmm.s.AssertJmpBuf.UnwindRetSp,
                                pVCpu->vmm.s.AssertJmpBuf.UnwindBp,
                                pVCpu->vmm.s.AssertJmpBuf.UnwindPc);
                pHlp->pfnPrintf(pHlp,
                                "UnwindRetPcValue=%RHv UnwindRetPcLocation=%RHv\n",
                                pVCpu->vmm.s.AssertJmpBuf.UnwindRetPcValue,
                                pVCpu->vmm.s.AssertJmpBuf.UnwindRetPcLocation);
                pHlp->pfnPrintf(pHlp,
                                "pfn=%RHv pvUser1=%RHv pvUser2=%RHv\n",
                                pVCpu->vmm.s.AssertJmpBuf.pfn,
                                pVCpu->vmm.s.AssertJmpBuf.pvUser1,
                                pVCpu->vmm.s.AssertJmpBuf.pvUser2);

                /* Dump the resume register frame on the stack. */
                PRTHCUINTPTR const pBP = (PRTHCUINTPTR)&pVCpu->vmm.s.abAssertStack[  pVCpu->vmm.s.AssertJmpBuf.UnwindBp
                                                                                   - pVCpu->vmm.s.AssertJmpBuf.UnwindSp];
#if HC_ARCH_BITS == 32
                pHlp->pfnPrintf(pHlp,
                                "eax=volatile ebx=%08x ecx=volatile edx=volatile esi=%08x edi=%08x\n"
                                "eip=%08x esp=%08x ebp=%08x efl=%08x\n"
                                ,
                                pBP[-3], pBP[-2], pBP[-1],
                                pBP[1], pVCpu->vmm.s.AssertJmpBuf.SavedEbp - 8, pBP[0], pBP[-4]);
#else
# ifdef RT_OS_WINDOWS
                pHlp->pfnPrintf(pHlp,
                                "rax=volatile         rbx=%016RX64 rcx=volatile         rdx=volatile\n"
                                "rsi=%016RX64 rdi=%016RX64  r8=volatile          r9=volatile        \n"
                                "r10=volatile         r11=volatile         r12=%016RX64 r13=%016RX64\n"
                                "r14=%016RX64 r15=%016RX64\n"
                                "rip=%016RX64 rsp=%016RX64 rbp=%016RX64 rfl=%08RX64\n"
                                ,
                                pBP[-7],
                                pBP[-6], pBP[-5],
                                pBP[-4], pBP[-3],
                                pBP[-2], pBP[-1],
                                pBP[1], pVCpu->vmm.s.AssertJmpBuf.UnwindRetSp, pBP[0], pBP[-8]);
# else
                pHlp->pfnPrintf(pHlp,
                                "rax=volatile         rbx=%016RX64 rcx=volatile         rdx=volatile\n"
                                "rsi=volatile         rdi=volatile          r8=volatile          r9=volatile        \n"
                                "r10=volatile         r11=volatile         r12=%016RX64 r13=%016RX64\n"
                                "r14=%016RX64 r15=%016RX64\n"
                                "rip=%016RX64 rsp=%016RX64 rbp=%016RX64 rflags=%08RX64\n"
                                ,
                                pBP[-5],
                                pBP[-4], pBP[-3],
                                pBP[-2], pBP[-1],
                                pBP[1], pVCpu->vmm.s.AssertJmpBuf.UnwindRetSp, pBP[0], pBP[-6]);
# endif
#endif

                /* Callstack. */
                DBGFADDRESS AddrPc, AddrBp, AddrSp;
                PCDBGFSTACKFRAME pFirstFrame;
                rc2 = DBGFR3StackWalkBeginEx(pVM->pUVM, pVCpu->idCpu, DBGFCODETYPE_RING0,
                                             DBGFR3AddrFromHostR0(&AddrBp, pVCpu->vmm.s.AssertJmpBuf.UnwindBp),
                                             DBGFR3AddrFromHostR0(&AddrSp, pVCpu->vmm.s.AssertJmpBuf.UnwindSp),
                                             DBGFR3AddrFromHostR0(&AddrPc, pVCpu->vmm.s.AssertJmpBuf.UnwindPc),
                                             RTDBGRETURNTYPE_INVALID, &pFirstFrame);
                if (RT_SUCCESS(rc2))
                {
                    pHlp->pfnPrintf(pHlp,
                                    "!!\n"
                                    "!! Call Stack:\n"
                                    "!!\n");
#if HC_ARCH_BITS == 32
                    pHlp->pfnPrintf(pHlp, "EBP      Ret EBP  Ret CS:EIP    Arg0     Arg1     Arg2     Arg3     CS:EIP        Symbol [line]\n");
#else
                    pHlp->pfnPrintf(pHlp, "RBP              Ret RBP          Ret RIP          RIP              Symbol [line]\n");
#endif
                    for (PCDBGFSTACKFRAME pFrame = pFirstFrame;
                         pFrame;
                         pFrame = DBGFR3StackWalkNext(pFrame))
                    {
#if HC_ARCH_BITS == 32
                        pHlp->pfnPrintf(pHlp,
                                        "%RHv %RHv %04RX32:%RHv %RHv %RHv %RHv %RHv",
                                        (RTHCUINTPTR)pFrame->AddrFrame.off,
                                        (RTHCUINTPTR)pFrame->AddrReturnFrame.off,
                                        (RTHCUINTPTR)pFrame->AddrReturnPC.Sel,
                                        (RTHCUINTPTR)pFrame->AddrReturnPC.off,
                                        pFrame->Args.au32[0],
                                        pFrame->Args.au32[1],
                                        pFrame->Args.au32[2],
                                        pFrame->Args.au32[3]);
                        pHlp->pfnPrintf(pHlp, " %RTsel:%08RHv", pFrame->AddrPC.Sel, pFrame->AddrPC.off);
#else
                        pHlp->pfnPrintf(pHlp,
                                        "%RHv %RHv %RHv %RHv",
                                        (RTHCUINTPTR)pFrame->AddrFrame.off,
                                        (RTHCUINTPTR)pFrame->AddrReturnFrame.off,
                                        (RTHCUINTPTR)pFrame->AddrReturnPC.off,
                                        (RTHCUINTPTR)pFrame->AddrPC.off);
#endif
                        if (pFrame->pSymPC)
                        {
                            RTGCINTPTR offDisp = pFrame->AddrPC.FlatPtr - pFrame->pSymPC->Value;
                            if (offDisp > 0)
                                pHlp->pfnPrintf(pHlp, " %s+%llx", pFrame->pSymPC->szName, (int64_t)offDisp);
                            else if (offDisp < 0)
                                pHlp->pfnPrintf(pHlp, " %s-%llx", pFrame->pSymPC->szName, -(int64_t)offDisp);
                            else
                                pHlp->pfnPrintf(pHlp, " %s", pFrame->pSymPC->szName);
                        }
                        if (pFrame->pLinePC)
                            pHlp->pfnPrintf(pHlp, " [%s @ 0i%d]", pFrame->pLinePC->szFilename, pFrame->pLinePC->uLineNo);
                        pHlp->pfnPrintf(pHlp, "\n");
                        for (uint32_t iReg = 0; iReg < pFrame->cSureRegs; iReg++)
                        {
                            const char *pszName = pFrame->paSureRegs[iReg].pszName;
                            if (!pszName)
                                pszName = DBGFR3RegCpuName(pVM->pUVM, pFrame->paSureRegs[iReg].enmReg,
                                                           pFrame->paSureRegs[iReg].enmType);
                            char szValue[1024];
                            szValue[0] = '\0';
                            DBGFR3RegFormatValue(szValue, sizeof(szValue), &pFrame->paSureRegs[iReg].Value,
                                                 pFrame->paSureRegs[iReg].enmType, false);
                            pHlp->pfnPrintf(pHlp, "     %-3s=%s\n", pszName, szValue);
                        }
                    }
                    DBGFR3StackWalkEnd(pFirstFrame);
                }

                /* Symbols on the stack. */
                uint32_t const          cbRawStack = RT_MIN(pVCpu->vmm.s.AssertJmpBuf.cbStackValid, sizeof(pVCpu->vmm.s.abAssertStack));
                uintptr_t const * const pauAddr    = (uintptr_t const *)&pVCpu->vmm.s.abAssertStack[0];
                uint32_t const          iEnd       = cbRawStack / sizeof(uintptr_t);
                uint32_t                iAddr      = 0;
                pHlp->pfnPrintf(pHlp,
                                "!!\n"
                                "!! Addresses on the stack (iAddr=%#x, iEnd=%#x)\n"
                                "!!\n",
                                iAddr, iEnd);
                while (iAddr < iEnd)
                {
                    uintptr_t const uAddr = pauAddr[iAddr];
                    if (uAddr > X86_PAGE_SIZE)
                    {
                        DBGFADDRESS  Addr;
                        DBGFR3AddrFromFlat(pVM->pUVM, &Addr, uAddr);
                        RTGCINTPTR   offDisp     = 0;
                        RTGCINTPTR   offLineDisp = 0;
                        PRTDBGSYMBOL pSym        = DBGFR3AsSymbolByAddrA(pVM->pUVM, DBGF_AS_R0, &Addr,
                                                                           RTDBGSYMADDR_FLAGS_LESS_OR_EQUAL
                                                                         | RTDBGSYMADDR_FLAGS_SKIP_ABS_IN_DEFERRED,
                                                                         &offDisp, NULL);
                        PRTDBGLINE   pLine       = DBGFR3AsLineByAddrA(pVM->pUVM, DBGF_AS_R0, &Addr, &offLineDisp, NULL);
                        if (pLine || pSym)
                        {
                            pHlp->pfnPrintf(pHlp, "%#06x: %p =>", iAddr * sizeof(uintptr_t), uAddr);
                            if (pSym)
                                pHlp->pfnPrintf(pHlp, " %s + %#x", pSym->szName, (intptr_t)offDisp);
                            if (pLine)
                                pHlp->pfnPrintf(pHlp, " [%s:%u + %#x]\n", pLine->szFilename, pLine->uLineNo, offLineDisp);
                            else
                                pHlp->pfnPrintf(pHlp, "\n");
                            RTDbgSymbolFree(pSym);
                            RTDbgLineFree(pLine);
                        }
                    }
                    iAddr++;
                }

                /* raw stack */
                Hlp.fRecSummary = false;
                pHlp->pfnPrintf(pHlp,
                                "!!\n"
                                "!! Raw stack (mind the direction).\n"
                                "!! pbEMTStackR0=%RHv cbRawStack=%#x\n"
                                "!! pbEmtStackR3=%p\n"
                                "!!\n"
                                "%.*Rhxd\n",
                                pVCpu->vmm.s.AssertJmpBuf.UnwindSp, cbRawStack,
                                &pVCpu->vmm.s.abAssertStack[0],
                                cbRawStack, &pVCpu->vmm.s.abAssertStack[0]);
            }
            else
            {
                pHlp->pfnPrintf(pHlp,
                                "!! Skipping ring-0 registers and stack, rcErr=%Rrc\n", rcErr);
            }
#endif /* !VBOX_VMM_TARGET_ARMV8 */
            break;
        }

        case VERR_IEM_INSTR_NOT_IMPLEMENTED:
        case VERR_IEM_ASPECT_NOT_IMPLEMENTED:
        case VERR_PATM_IPE_TRAP_IN_PATCH_CODE:
        case VERR_EM_GUEST_CPU_HANG:
        {
            CPUMImportGuestStateOnDemand(pVCpu, CPUMCTX_EXTRN_ABSOLUTELY_ALL);
            fDoneImport = true;

            DBGFR3Info(pVM->pUVM, "cpumguest", NULL, pHlp);
            DBGFR3Info(pVM->pUVM, "cpumguestinstr", NULL, pHlp);
            DBGFR3Info(pVM->pUVM, "cpumguesthwvirt", NULL, pHlp);
            break;
        }

        /*
         * For some problems (e.g. VERR_INVALID_STATE in VMMR0.cpp), there could be
         * additional details in the assertion messages.
         */
        default:
        {
            const char *pszMsg1 = VMMR3GetRZAssertMsg1(pVM);
            while (pszMsg1 && *pszMsg1 == '\n')
                pszMsg1++;
            if (pszMsg1 && *pszMsg1 != '\0')
                pHlp->pfnPrintf(pHlp, "AssertMsg1: %s\n", pszMsg1);

            const char *pszMsg2 = VMMR3GetRZAssertMsg2(pVM);
            while (pszMsg2 && *pszMsg2 == '\n')
                pszMsg2++;
            if (pszMsg2 && *pszMsg2 != '\0')
                pHlp->pfnPrintf(pHlp, "AssertMsg2: %s\n", pszMsg2);
            break;
        }

    } /* switch (rcErr) */
    Hlp.fRecSummary = false;


    /*
     * Generic info dumper loop.
     */
    if (!fDoneImport)
        CPUMImportGuestStateOnDemand(pVCpu, CPUMCTX_EXTRN_ABSOLUTELY_ALL);
    static struct
    {
        const char *pszInfo;
        const char *pszArgs;
    } const     aInfo[] =
    {
        { "mappings",        NULL },
        { "hma",             NULL },
        { "cpumguest",       "verbose" },
        { "cpumguesthwvirt", "verbose" },
        { "cpumguestinstr",  "verbose" },
        { "cpumhyper",       "verbose" },
        { "cpumhost",        "verbose" },
        { "mode",            "all" },
        { "cpuid",           "verbose" },
        { "handlers",        "phys virt hyper stats" },
        { "timers",          NULL },
        { "activetimers",    NULL },
    };
    for (unsigned i = 0; i < RT_ELEMENTS(aInfo); i++)
    {
        if (fDoneHyper && !strcmp(aInfo[i].pszInfo, "cpumhyper"))
            continue;
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
                    "mappings|hma|cpum|cpumguest|cpumguesthwvirt|cpumguestinstr|cpumhyper|cpumhost|mode|cpuid"
                    "|pgmpd|pgmcr3|timers|activetimers|handlers|help|exithistory",
                    "!!\n"
                    "!! {%s}\n"
                    "!!\n",
                    pHlp);


    /* done */
    pHlp->pfnPrintf(pHlp,
                    "!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!\n");


    /*
     * Repeat the summary to stderr so we don't have to scroll half a mile up.
     */
    vmmR3FatalDumpInfoHlpFlushStdErr(&Hlp);
    if (Hlp.szSummary[0])
        RTStrmPrintf(g_pStdErr,
                     "%s\n"
                     "!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!\n",
                     Hlp.szSummary);

    /*
     * Delete the output instance (flushing and restoring of flags).
     */
    vmmR3FatalDumpInfoHlpDelete(&Hlp);

    /*
     * Rendezvous with the other EMTs and clear the VM_FF_CHECK_VM_STATE so we can
     * stop burning CPU cycles.
     */
    VMMR3EmtRendezvous(pVM, VMMEMTRENDEZVOUS_FLAGS_TYPE_ONCE, vmmR3FatalDumpRendezvousDoneCallback, NULL);
}

