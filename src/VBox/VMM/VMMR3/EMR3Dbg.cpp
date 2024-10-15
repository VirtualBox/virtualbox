/* $Id: EMR3Dbg.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * EM - Execution Monitor / Manager, Debugger Related Bits.
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
#define LOG_GROUP LOG_GROUP_EM
#include <VBox/vmm/em.h>
#include <VBox/vmm/hm.h>
#include <VBox/vmm/iem.h>
#include <VBox/vmm/nem.h>
#include <VBox/dbg.h>
#include "EMInternal.h"
#include <VBox/vmm/vm.h>
#include <iprt/string.h>
#include <iprt/ctype.h>


/**
 * Common worker for the  '.alliem' and '.iemrecompiled' commands.
 */
static int emR3DbgCmdSetPolicyCommon(PCDBGCCMD pCmd, PDBGCCMDHLP pCmdHlp, PUVM pUVM, PCDBGCVAR paArgs, unsigned cArgs,
                                     EMEXECPOLICY enmPolicy, const char *pszPolicy)
{
    int  rc;
    bool f;

    if (cArgs == 0)
    {
        rc = EMR3QueryExecutionPolicy(pUVM, enmPolicy, &f);
        if (RT_FAILURE(rc))
            return DBGCCmdHlpFailRc(pCmdHlp, pCmd, rc, "EMR3QueryExecutionPolicy(,%s,", pszPolicy);
        DBGCCmdHlpPrintf(pCmdHlp, f ? "%s: enabled\n" : "%s: disabled\n", pszPolicy);
    }
    else
    {
        rc = DBGCCmdHlpVarToBool(pCmdHlp, &paArgs[0], &f);
        if (RT_FAILURE(rc))
            return DBGCCmdHlpFailRc(pCmdHlp, pCmd, rc, "DBGCCmdHlpVarToBool");
        rc = EMR3SetExecutionPolicy(pUVM, enmPolicy, f);
        if (RT_FAILURE(rc))
            return DBGCCmdHlpFailRc(pCmdHlp, pCmd, rc, "EMR3SetExecutionPolicy(,%s,%RTbool)", pszPolicy, f);
    }
    return VINF_SUCCESS;
}


/** @callback_method_impl{FNDBGCCMD,
 * Implements the '.alliem' command. }
 */
static DECLCALLBACK(int) emR3DbgCmdAllIem(PCDBGCCMD pCmd, PDBGCCMDHLP pCmdHlp, PUVM pUVM, PCDBGCVAR paArgs, unsigned cArgs)
{
    return emR3DbgCmdSetPolicyCommon(pCmd, pCmdHlp, pUVM, paArgs, cArgs, EMEXECPOLICY_IEM_ALL, "EMEXECPOLICY_IEM_ALL");
}


/** @callback_method_impl{FNDBGCCMD,
 * Implements the '.iemrecompiled' command. }
 */
static DECLCALLBACK(int) emR3DbgCmdIemRecompiled(PCDBGCCMD pCmd, PDBGCCMDHLP pCmdHlp, PUVM pUVM, PCDBGCVAR paArgs, unsigned cArgs)
{
    return emR3DbgCmdSetPolicyCommon(pCmd, pCmdHlp, pUVM, paArgs, cArgs, EMEXECPOLICY_IEM_RECOMPILED, "EMEXECPOLICY_IEM_RECOMPILED");
}


/** Describes a optional boolean argument. */
static DBGCVARDESC const g_BoolArg = { 0, 1, DBGCVAR_CAT_ANY, 0, "boolean", "Boolean value." };

/** Commands.  */
static DBGCCMD const g_aCmds[] =
{
    {
        "alliem", 0, 1, &g_BoolArg, 1, 0, emR3DbgCmdAllIem, "[boolean]",
        "Enables or disables executing ALL code in IEM, if no arguments are given it displays the current status."
    },
    {
        "iemrecompiled", 0, 1, &g_BoolArg, 1, 0, emR3DbgCmdIemRecompiled, "[boolean]",
        "Enables or disables recompiled ALL-in-IEM execution, if no arguments are given it displays the current status."
    },
};


/**
 * Translates EMEXITTYPE into a name.
 *
 * @returns Pointer to read-only name, NULL if unknown type.
 * @param   enmExitType     The exit type to name.
 */
VMM_INT_DECL(const char *) EMR3GetExitTypeName(EMEXITTYPE enmExitType)
{
    switch (enmExitType)
    {
        case EMEXITTYPE_INVALID:            return "invalid";
        case EMEXITTYPE_IO_PORT_READ:       return "I/O port read";
        case EMEXITTYPE_IO_PORT_WRITE:      return "I/O port write";
        case EMEXITTYPE_IO_PORT_STR_READ:   return "I/O port string read";
        case EMEXITTYPE_IO_PORT_STR_WRITE:  return "I/O port string write";
        case EMEXITTYPE_MMIO:               return "MMIO access";
        case EMEXITTYPE_MMIO_READ:          return "MMIO read";
        case EMEXITTYPE_MMIO_WRITE:         return "MMIO write";
        case EMEXITTYPE_MSR_READ:           return "MSR read";
        case EMEXITTYPE_MSR_WRITE:          return "MSR write";
        case EMEXITTYPE_CPUID:              return "CPUID";
        case EMEXITTYPE_RDTSC:              return "RDTSC";
        case EMEXITTYPE_MOV_CRX:            return "MOV CRx";
        case EMEXITTYPE_MOV_DRX:            return "MOV DRx";
        case EMEXITTYPE_VMREAD:             return "VMREAD";
        case EMEXITTYPE_VMWRITE:            return "VMWRITE";

        /* Raw-mode only: */
        case EMEXITTYPE_INVLPG:             return "INVLPG";
        case EMEXITTYPE_LLDT:               return "LLDT";
        case EMEXITTYPE_RDPMC:              return "RDPMC";
        case EMEXITTYPE_CLTS:               return "CLTS";
        case EMEXITTYPE_STI:                return "STI";
        case EMEXITTYPE_INT:                return "INT";
        case EMEXITTYPE_SYSCALL:            return "SYSCALL";
        case EMEXITTYPE_SYSENTER:           return "SYSENTER";
        case EMEXITTYPE_HLT:                return "HLT";
    }
    return NULL;
}


/**
 * Translates flags+type into an exit name.
 *
 * @returns Exit name.
 * @param   uFlagsAndType   The exit to name.
 * @param   pszFallback     Buffer for formatting a numeric fallback.
 * @param   cbFallback      Size of fallback buffer.
 */
static const char *emR3HistoryGetExitName(uint32_t uFlagsAndType, char *pszFallback, size_t cbFallback)
{
    const char *pszExitName;
    switch (uFlagsAndType & EMEXIT_F_KIND_MASK)
    {
        case EMEXIT_F_KIND_EM:
            pszExitName = EMR3GetExitTypeName((EMEXITTYPE)(uFlagsAndType & EMEXIT_F_TYPE_MASK));
            break;

#if !defined(VBOX_VMM_TARGET_ARMV8)
        case EMEXIT_F_KIND_VMX:
            pszExitName = HMGetVmxExitName(uFlagsAndType & EMEXIT_F_TYPE_MASK);
            break;

        case EMEXIT_F_KIND_SVM:
            pszExitName = HMGetSvmExitName(uFlagsAndType & EMEXIT_F_TYPE_MASK);
            break;
#endif

        case EMEXIT_F_KIND_NEM:
            pszExitName = NEMR3GetExitName(uFlagsAndType & EMEXIT_F_TYPE_MASK);
            break;

        case EMEXIT_F_KIND_IEM:
            pszExitName = IEMR3GetExitName(uFlagsAndType & EMEXIT_F_TYPE_MASK);
            break;

        case EMEXIT_F_KIND_XCPT:
#if defined(VBOX_VMM_TARGET_ARMV8)
            pszExitName = NULL;
            AssertReleaseFailed();
#else
            switch (uFlagsAndType & EMEXIT_F_TYPE_MASK)
            {
                case X86_XCPT_DE:               return "Xcpt #DE";
                case X86_XCPT_DB:               return "Xcpt #DB";
                case X86_XCPT_NMI:              return "Xcpt #NMI";
                case X86_XCPT_BP:               return "Xcpt #BP";
                case X86_XCPT_OF:               return "Xcpt #OF";
                case X86_XCPT_BR:               return "Xcpt #BR";
                case X86_XCPT_UD:               return "Xcpt #UD";
                case X86_XCPT_NM:               return "Xcpt #NM";
                case X86_XCPT_DF:               return "Xcpt #DF";
                case X86_XCPT_CO_SEG_OVERRUN:   return "Xcpt #CO_SEG_OVERRUN";
                case X86_XCPT_TS:               return "Xcpt #TS";
                case X86_XCPT_NP:               return "Xcpt #NP";
                case X86_XCPT_SS:               return "Xcpt #SS";
                case X86_XCPT_GP:               return "Xcpt #GP";
                case X86_XCPT_PF:               return "Xcpt #PF";
                case X86_XCPT_MF:               return "Xcpt #MF";
                case X86_XCPT_AC:               return "Xcpt #AC";
                case X86_XCPT_MC:               return "Xcpt #MC";
                case X86_XCPT_XF:               return "Xcpt #XF";
                case X86_XCPT_VE:               return "Xcpt #VE";
                case X86_XCPT_CP:               return "Xcpt #CP";
                case X86_XCPT_VC:               return "Xcpt #VC";
                case X86_XCPT_SX:               return "Xcpt #SX";

                case X86_XCPT_DF | EMEXIT_F_XCPT_ERRCD:  return "Xcpt #DF ErrCd as PC";
                case X86_XCPT_TS | EMEXIT_F_XCPT_ERRCD:  return "Xcpt #TS ErrCd as PC";
                case X86_XCPT_NP | EMEXIT_F_XCPT_ERRCD:  return "Xcpt #NP ErrCd as PC";
                case X86_XCPT_SS | EMEXIT_F_XCPT_ERRCD:  return "Xcpt #SS ErrCd as PC";
                case X86_XCPT_GP | EMEXIT_F_XCPT_ERRCD:  return "Xcpt #GF ErrCd as PC";
                case X86_XCPT_PF | EMEXIT_F_XCPT_ERRCD:  return "Xcpt #PF ErrCd as PC";
                case X86_XCPT_AC | EMEXIT_F_XCPT_ERRCD:  return "Xcpt #AC ErrCd as PC";

                case X86_XCPT_PF | EMEXIT_F_XCPT_CR2:    return "Xcpt #PF CR2 as PC";

                default:
                    pszExitName = NULL;
                    break;
            }
#endif
            break;

        default:
            AssertFailed();
            pszExitName = NULL;
            break;
    }
    if (pszExitName)
        return pszExitName;
    RTStrPrintf(pszFallback, cbFallback, "%#06x", uFlagsAndType & (EMEXIT_F_KIND_MASK | EMEXIT_F_TYPE_MASK));
    return pszFallback;
}


/**
 * Displays the VM-exit history.
 *
 * @param   pVM         The cross context VM structure.
 * @param   pHlp        The info helper functions.
 * @param   pszArgs     Arguments, ignored.
 */
static DECLCALLBACK(void) emR3InfoExitHistory(PVM pVM, PCDBGFINFOHLP pHlp, const char *pszArgs)
{
    NOREF(pszArgs);

    /*
     * Figure out target cpu and parse arguments.
     */
    PVMCPU   pVCpu    = VMMGetCpu(pVM);
    if (!pVCpu)
        pVCpu = pVM->apCpusR3[0];
    bool     fReverse = true;
    uint32_t cLeft    = RT_ELEMENTS(pVCpu->em.s.aExitHistory);

    while (pszArgs && *pszArgs)
    {
        pszArgs = RTStrStripL(pszArgs);
        if (!*pszArgs)
            break;
        if (RT_C_IS_DIGIT(*pszArgs))
        {
            /* The number to dump. */
            uint32_t uValue = cLeft;
            RTStrToUInt32Ex(pszArgs, (char **)&pszArgs, 0, &uValue);
            if (uValue > 0)
                cLeft = RT_MIN(uValue, RT_ELEMENTS(pVCpu->em.s.aExitHistory));
        }
        else if (RTStrCmp(pszArgs, "reverse") == 0)
        {
            pszArgs += 7;
            fReverse = true;
        }
        else if (RTStrCmp(pszArgs, "ascending") == 0)
        {
            pszArgs += 9;
            fReverse = false;
        }
        else if (RTStrCmp(pszArgs, "asc") == 0)
        {
            pszArgs += 3;
            fReverse = false;
        }
        else
        {
            const char *pszStart = pszArgs;
            while (*pszArgs && !RT_C_IS_SPACE(*pszArgs))
                pszArgs++;
            pHlp->pfnPrintf(pHlp, "Unknown option: %.*s\n", pszArgs - pszStart, pszArgs);
        }
    }

    /*
     * Do the job.
     */
    uint64_t idx = pVCpu->em.s.iNextExit;
    if (idx == 0)
        pHlp->pfnPrintf(pHlp, "CPU[%u]: VM-exit history: empty\n", pVCpu->idCpu);
    else
    {
        /*
         * Print header.
         */
        pHlp->pfnPrintf(pHlp,
                        "CPU[%u]: VM-exit history:\n"
                        "   Exit No.:     TSC timestamp / delta    RIP (Flat/*)      Exit    Name\n"
                        , pVCpu->idCpu);

        /*
         * Adjust bounds if ascending order.
         */
        if (!fReverse)
        {
            if (idx > cLeft)
                idx -= cLeft;
            else
            {
                cLeft = idx;
                idx = 0;
            }
        }

        /*
         * Print the entries.
         */
        uint64_t uPrevTimestamp = 0;
        do
        {
            if (fReverse)
                idx -= 1;
            PCEMEXITENTRY const pEntry = &pVCpu->em.s.aExitHistory[(uintptr_t)idx & 0xff];

            /* Get the exit name. */
            char        szExitName[16];
            const char *pszExitName = emR3HistoryGetExitName(pEntry->uFlagsAndType, szExitName, sizeof(szExitName));

            /* Calc delta (negative if reverse order, positive ascending). */
            int64_t offDelta = uPrevTimestamp != 0 && pEntry->uTimestamp != 0 ? pEntry->uTimestamp - uPrevTimestamp : 0;
            uPrevTimestamp = pEntry->uTimestamp;

            char szPC[32];
            if (!(pEntry->uFlagsAndType & (EMEXIT_F_CS_EIP | EMEXIT_F_UNFLATTENED_PC)))
                RTStrPrintf(szPC, sizeof(szPC), "%016RX64 ", pEntry->uFlatPC);
            else if (pEntry->uFlagsAndType & EMEXIT_F_UNFLATTENED_PC)
                RTStrPrintf(szPC, sizeof(szPC), "%016RX64*", pEntry->uFlatPC);
            else
                RTStrPrintf(szPC, sizeof(szPC), "%04x:%08RX32*   ", (uint32_t)(pEntry->uFlatPC >> 32), (uint32_t)pEntry->uFlatPC);

            /* Do the printing. */
            if (pEntry->idxSlot == UINT32_MAX)
                pHlp->pfnPrintf(pHlp, " %10RU64: %#018RX64/%+-9RI64 %s %#07x %s\n",
                                idx, pEntry->uTimestamp, offDelta, szPC, pEntry->uFlagsAndType, pszExitName);
            else
            {
                /** @todo more on this later */
                pHlp->pfnPrintf(pHlp, " %10RU64: %#018RX64/%+-9RI64 %s %#07x %s slot=%#x\n",
                                idx, pEntry->uTimestamp, offDelta, szPC, pEntry->uFlagsAndType, pszExitName, pEntry->idxSlot);
            }

            /* Advance if ascending. */
            if (!fReverse)
                idx += 1;
        } while (--cLeft > 0 && idx > 0);
    }
}


int emR3InitDbg(PVM pVM)
{
    /*
     * Register info dumpers.
     */
    const char *pszExitsDesc = "Dumps the VM-exit history. Arguments: Number of entries; 'asc', 'ascending' or 'reverse'.";
    int rc = DBGFR3InfoRegisterInternalEx(pVM, "exits", pszExitsDesc, emR3InfoExitHistory, DBGFINFO_FLAGS_ALL_EMTS);
    AssertLogRelRCReturn(rc, rc);
    rc = DBGFR3InfoRegisterInternalEx(pVM, "exithistory", pszExitsDesc, emR3InfoExitHistory, DBGFINFO_FLAGS_ALL_EMTS);
    AssertLogRelRCReturn(rc, rc);

#ifdef VBOX_WITH_DEBUGGER
    /*
     * Register debugger commands.
     */
    rc = DBGCRegisterCommands(&g_aCmds[0], RT_ELEMENTS(g_aCmds));
    AssertLogRelRCReturn(rc, rc);
#endif

    return VINF_SUCCESS;
}

