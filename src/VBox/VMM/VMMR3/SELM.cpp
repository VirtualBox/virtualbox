/* $Id: SELM.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * SELM - The Selector Manager.
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

/** @page pg_selm   SELM - The Selector Manager
 *
 * SELM takes care of GDT, LDT and TSS shadowing in raw-mode, and the injection
 * of a few hyper selector for the raw-mode context.  In the hardware assisted
 * virtualization mode its only task is to decode entries in the guest GDT or
 * LDT once in a while.
 *
 * @see grp_selm
 *
 *
 * @section seg_selm_shadowing   Shadowing
 *
 * SELMR3UpdateFromCPUM() and SELMR3SyncTSS() does the bulk synchronization
 * work.  The three structures (GDT, LDT, TSS) are all shadowed wholesale atm.
 * The idea is to do it in a more on-demand fashion when we get time.  There
 * also a whole bunch of issues with the current synchronization of all three
 * tables, see notes and todos in the code.
 *
 * When the guest makes changes to the GDT we will try update the shadow copy
 * without involving SELMR3UpdateFromCPUM(), see selmGCSyncGDTEntry().
 *
 * When the guest make LDT changes we'll trigger a full resync of the LDT
 * (SELMR3UpdateFromCPUM()), which, needless to say, isn't optimal.
 *
 * The TSS shadowing is limited to the fields we need to care about, namely SS0
 * and ESP0.  The Patch Manager makes use of these.  We monitor updates to the
 * guest TSS and will try keep our SS0 and ESP0 copies up to date this way
 * rather than go the SELMR3SyncTSS() route.
 *
 * When in raw-mode SELM also injects a few extra GDT selectors which are used
 * by the raw-mode (hyper) context.  These start their life at the high end of
 * the table and will be relocated when the guest tries to make use of them...
 * Well, that was that idea at least, only the code isn't quite there yet which
 * is why we have trouble with guests which actually have a full sized GDT.
 *
 * So, the summary of the current GDT, LDT and TSS shadowing is that there is a
 * lot of relatively simple and enjoyable work to be done, see @bugref{3267}.
 *
 */


/*********************************************************************************************************************************
*   Header Files                                                                                                                 *
*********************************************************************************************************************************/
#define LOG_GROUP LOG_GROUP_SELM
#include <VBox/vmm/selm.h>
#include <VBox/vmm/cpum.h>
#include <VBox/vmm/stam.h>
#include <VBox/vmm/em.h>
#include <VBox/vmm/hm.h>
#include <VBox/vmm/mm.h>
#include <VBox/vmm/ssm.h>
#include <VBox/vmm/pgm.h>
#include <VBox/vmm/trpm.h>
#include <VBox/vmm/dbgf.h>
#include "SELMInternal.h"
#include <VBox/vmm/vm.h>
#include <VBox/err.h>
#include <VBox/param.h>

#include <iprt/assert.h>
#include <VBox/log.h>
#include <iprt/asm.h>
#include <iprt/string.h>
#include <iprt/thread.h>
#include <iprt/string.h>


/*********************************************************************************************************************************
*   Internal Functions                                                                                                           *
*********************************************************************************************************************************/
static DECLCALLBACK(void) selmR3InfoGdtGuest(PVM pVM, PCDBGFINFOHLP pHlp, const char *pszArgs);
static DECLCALLBACK(void) selmR3InfoLdtGuest(PVM pVM, PCDBGFINFOHLP pHlp, const char *pszArgs);
//static DECLCALLBACK(void) selmR3InfoTssGuest(PVM pVM, PCDBGFINFOHLP pHlp, const char *pszArgs);



/**
 * Initializes the SELM.
 *
 * @returns VBox status code.
 * @param   pVM         The cross context VM structure.
 */
VMMR3DECL(int) SELMR3Init(PVM pVM)
{
    int rc;
    LogFlow(("SELMR3Init\n"));

    /*
     * Assert alignment and sizes.
     * (The TSS block requires contiguous back.)
     */
    AssertCompile(sizeof(pVM->selm.s) <= sizeof(pVM->selm.padding));    AssertRelease(sizeof(pVM->selm.s) <= sizeof(pVM->selm.padding));
    AssertCompileMemberAlignment(VM, selm.s, 32);                       AssertRelease(!(RT_UOFFSETOF(VM, selm.s) & 31));

    /*
     * Register the saved state data unit.
     */
    rc = SSMR3RegisterStub(pVM, "selm", 1);
    if (RT_FAILURE(rc))
        return rc;

    /*
     * Statistics.
     */
    STAM_REG(    pVM, &pVM->selm.s.StatLoadHidSelGst,              STAMTYPE_COUNTER, "/SELM/LoadHidSel/LoadedGuest",   STAMUNIT_OCCURENCES, "SELMLoadHiddenSelectorReg: Loaded from guest tables.");
    STAM_REG(    pVM, &pVM->selm.s.StatLoadHidSelShw,              STAMTYPE_COUNTER, "/SELM/LoadHidSel/LoadedShadow",  STAMUNIT_OCCURENCES, "SELMLoadHiddenSelectorReg: Loaded from shadow tables.");
    STAM_REL_REG(pVM, &pVM->selm.s.StatLoadHidSelReadErrors,       STAMTYPE_COUNTER, "/SELM/LoadHidSel/GstReadErrors", STAMUNIT_OCCURENCES, "SELMLoadHiddenSelectorReg: Guest table read errors.");
    STAM_REL_REG(pVM, &pVM->selm.s.StatLoadHidSelGstNoGood,        STAMTYPE_COUNTER, "/SELM/LoadHidSel/NoGoodGuest",   STAMUNIT_OCCURENCES, "SELMLoadHiddenSelectorReg: No good guest table entry.");

    /*
     * Register info handlers.
     */
    DBGFR3InfoRegisterInternalEx(pVM, "gdt", "Displays the guest GDT. No arguments.", &selmR3InfoGdtGuest, DBGFINFO_FLAGS_RUN_ON_EMT);
    DBGFR3InfoRegisterInternalEx(pVM, "ldt", "Displays the guest LDT. No arguments.", &selmR3InfoLdtGuest, DBGFINFO_FLAGS_RUN_ON_EMT);
    //DBGFR3InfoRegisterInternal(pVM, "tss", "Displays the guest TSS. No arguments.", &selmR3InfoTssGuest, DBGFINFO_FLAGS_RUN_ON_EMT);

    return rc;
}


/**
 * Applies relocations to data and code managed by this
 * component. This function will be called at init and
 * whenever the VMM need to relocate it self inside the GC.
 *
 * @param   pVM     The cross context VM structure.
 */
VMMR3DECL(void) SELMR3Relocate(PVM pVM)
{
    LogFlow(("SELMR3Relocate\n"));
    RT_NOREF(pVM);
}


/**
 * Terminates the SELM.
 *
 * Termination means cleaning up and freeing all resources,
 * the VM it self is at this point powered off or suspended.
 *
 * @returns VBox status code.
 * @param   pVM         The cross context VM structure.
 */
VMMR3DECL(int) SELMR3Term(PVM pVM)
{
    NOREF(pVM);
    return VINF_SUCCESS;
}


/**
 * The VM is being reset.
 *
 * For the SELM component this means that any GDT/LDT/TSS monitors
 * needs to be removed.
 *
 * @param   pVM     The cross context VM structure.
 */
VMMR3DECL(void) SELMR3Reset(PVM pVM)
{
    LogFlow(("SELMR3Reset:\n"));
    VM_ASSERT_EMT(pVM);
    RT_NOREF(pVM);
}


/**
 * Gets information about a 64-bit selector, SELMR3GetSelectorInfo helper.
 *
 * See SELMR3GetSelectorInfo for details.
 *
 * @returns VBox status code, see SELMR3GetSelectorInfo for details.
 *
 * @param   pVCpu       The cross context virtual CPU structure.
 * @param   Sel         The selector to get info about.
 * @param   pSelInfo    Where to store the information.
 */
static int selmR3GetSelectorInfo64(PVMCPU pVCpu, RTSEL Sel, PDBGFSELINFO pSelInfo)
{
    /*
     * Read it from the guest descriptor table.
     */
/** @todo this is bogus wrt the LDT/GDT limit on long selectors. */
    X86DESC64   Desc;
    RTGCPTR     GCPtrDesc;
    if (!(Sel & X86_SEL_LDT))
    {
        /* GDT */
        VBOXGDTR Gdtr;
        CPUMGetGuestGDTR(pVCpu, &Gdtr);
        if ((Sel | X86_SEL_RPL_LDT) > Gdtr.cbGdt)
            return VERR_INVALID_SELECTOR;
        GCPtrDesc = Gdtr.pGdt + (Sel & X86_SEL_MASK);
    }
    else
    {
        /* LDT */
        uint64_t GCPtrBase;
        uint32_t cbLimit;
        CPUMGetGuestLdtrEx(pVCpu, &GCPtrBase, &cbLimit);
        if ((Sel | X86_SEL_RPL_LDT) > cbLimit)
            return VERR_INVALID_SELECTOR;

        /* calc the descriptor location. */
        GCPtrDesc = GCPtrBase + (Sel & X86_SEL_MASK);
    }

    /* read the descriptor. */
    int rc = PGMPhysSimpleReadGCPtr(pVCpu, &Desc, GCPtrDesc, sizeof(Desc));
    if (RT_FAILURE(rc))
    {
        rc = PGMPhysSimpleReadGCPtr(pVCpu, &Desc, GCPtrDesc, sizeof(X86DESC));
        if (RT_FAILURE(rc))
            return rc;
        Desc.au64[1] = 0;
    }

    /*
     * Extract the base and limit
     * (We ignore the present bit here, which is probably a bit silly...)
     */
    pSelInfo->Sel     = Sel;
    pSelInfo->fFlags  = DBGFSELINFO_FLAGS_LONG_MODE;
    pSelInfo->u.Raw64 = Desc;
    if (Desc.Gen.u1DescType)
    {
        /*
         * 64-bit code selectors are wide open, it's not possible to detect
         * 64-bit data or stack selectors without also dragging in assumptions
         * about current CS (i.e. that's we're executing in 64-bit mode).  So,
         * the selinfo user needs to deal with this in the context the info is
         * used unfortunately.
         */
        if (    Desc.Gen.u1Long
            &&  !Desc.Gen.u1DefBig
            &&  (Desc.Gen.u4Type & X86_SEL_TYPE_CODE))
        {
            /* Note! We ignore the segment limit hacks that was added by AMD. */
            pSelInfo->GCPtrBase = 0;
            pSelInfo->cbLimit   = ~(RTGCUINTPTR)0;
        }
        else
        {
            pSelInfo->cbLimit   = X86DESC_LIMIT_G(&Desc);
            pSelInfo->GCPtrBase = X86DESC_BASE(&Desc);
        }
        pSelInfo->SelGate = 0;
    }
    else if (   Desc.Gen.u4Type == AMD64_SEL_TYPE_SYS_LDT
             || Desc.Gen.u4Type == AMD64_SEL_TYPE_SYS_TSS_AVAIL
             || Desc.Gen.u4Type == AMD64_SEL_TYPE_SYS_TSS_BUSY)
    {
        /* Note. LDT descriptors are weird in long mode, we ignore the footnote
           in the AMD manual here as a simplification. */
        pSelInfo->GCPtrBase = X86DESC64_BASE(&Desc);
        pSelInfo->cbLimit   = X86DESC_LIMIT_G(&Desc);
        pSelInfo->SelGate   = 0;
    }
    else if (   Desc.Gen.u4Type == AMD64_SEL_TYPE_SYS_CALL_GATE
             || Desc.Gen.u4Type == AMD64_SEL_TYPE_SYS_TRAP_GATE
             || Desc.Gen.u4Type == AMD64_SEL_TYPE_SYS_INT_GATE)
    {
        pSelInfo->cbLimit   = X86DESC64_BASE(&Desc);
        pSelInfo->GCPtrBase = Desc.Gate.u16OffsetLow
                            | ((uint32_t)Desc.Gate.u16OffsetHigh << 16)
                            | ((uint64_t)Desc.Gate.u32OffsetTop << 32);
        pSelInfo->SelGate   = Desc.Gate.u16Sel;
        pSelInfo->fFlags   |= DBGFSELINFO_FLAGS_GATE;
    }
    else
    {
        pSelInfo->cbLimit   = 0;
        pSelInfo->GCPtrBase = 0;
        pSelInfo->SelGate   = 0;
        pSelInfo->fFlags   |= DBGFSELINFO_FLAGS_INVALID;
    }
    if (!Desc.Gen.u1Present)
        pSelInfo->fFlags |= DBGFSELINFO_FLAGS_NOT_PRESENT;

    return VINF_SUCCESS;
}


/**
 * Worker for selmR3GetSelectorInfo32 and SELMR3GetShadowSelectorInfo that
 * interprets a legacy descriptor table entry and fills in the selector info
 * structure from it.
 *
 * @param  pSelInfo     Where to store the selector info. Only the fFlags and
 *                      Sel members have been initialized.
 * @param  pDesc        The legacy descriptor to parse.
 */
DECLINLINE(void) selmR3SelInfoFromDesc32(PDBGFSELINFO pSelInfo, PCX86DESC pDesc)
{
    pSelInfo->u.Raw64.au64[1] = 0;
    pSelInfo->u.Raw = *pDesc;
    if (    pDesc->Gen.u1DescType
        ||  !(pDesc->Gen.u4Type & 4))
    {
        pSelInfo->cbLimit   = X86DESC_LIMIT_G(pDesc);
        pSelInfo->GCPtrBase = X86DESC_BASE(pDesc);
        pSelInfo->SelGate   = 0;
    }
    else if (pDesc->Gen.u4Type != X86_SEL_TYPE_SYS_UNDEFINED4)
    {
        pSelInfo->cbLimit = 0;
        if (pDesc->Gen.u4Type == X86_SEL_TYPE_SYS_TASK_GATE)
            pSelInfo->GCPtrBase = 0;
        else
            pSelInfo->GCPtrBase = pDesc->Gate.u16OffsetLow
                                | (uint32_t)pDesc->Gate.u16OffsetHigh << 16;
        pSelInfo->SelGate = pDesc->Gate.u16Sel;
        pSelInfo->fFlags |= DBGFSELINFO_FLAGS_GATE;
    }
    else
    {
        pSelInfo->cbLimit = 0;
        pSelInfo->GCPtrBase = 0;
        pSelInfo->SelGate = 0;
        pSelInfo->fFlags |= DBGFSELINFO_FLAGS_INVALID;
    }
    if (!pDesc->Gen.u1Present)
        pSelInfo->fFlags |= DBGFSELINFO_FLAGS_NOT_PRESENT;
}


/**
 * Gets information about a 64-bit selector, SELMR3GetSelectorInfo helper.
 *
 * See SELMR3GetSelectorInfo for details.
 *
 * @returns VBox status code, see SELMR3GetSelectorInfo for details.
 *
 * @param   pVCpu       The cross context virtual CPU structure.
 * @param   Sel         The selector to get info about.
 * @param   pSelInfo    Where to store the information.
 */
static int selmR3GetSelectorInfo32(PVMCPU pVCpu, RTSEL Sel, PDBGFSELINFO pSelInfo)
{
    /*
     * Read the descriptor entry
     */
    pSelInfo->fFlags = 0;
    if (CPUMIsGuestInProtectedMode(pVCpu))
    {
        /*
         * Read it from the guest descriptor table.
         */
        pSelInfo->fFlags = DBGFSELINFO_FLAGS_PROT_MODE;

        RTGCPTR GCPtrDesc;
        if (!(Sel & X86_SEL_LDT))
        {
            /* GDT */
            VBOXGDTR Gdtr;
            CPUMGetGuestGDTR(pVCpu, &Gdtr);
            if ((Sel | X86_SEL_RPL_LDT) > Gdtr.cbGdt)
                return VERR_INVALID_SELECTOR;
            GCPtrDesc = Gdtr.pGdt + (Sel & X86_SEL_MASK);
        }
        else
        {
            /* LDT */
            uint64_t GCPtrBase;
            uint32_t cbLimit;
            CPUMGetGuestLdtrEx(pVCpu, &GCPtrBase, &cbLimit);
            if ((Sel | X86_SEL_RPL_LDT) > cbLimit)
                return VERR_INVALID_SELECTOR;

            /* calc the descriptor location. */
            GCPtrDesc = GCPtrBase + (Sel & X86_SEL_MASK);
        }

        /* read the descriptor. */
        X86DESC Desc;
        int rc = PGMPhysSimpleReadGCPtr(pVCpu, &Desc, GCPtrDesc, sizeof(Desc));
        if (RT_SUCCESS(rc))
        {
            /*
             * Extract the base and limit or sel:offset for gates.
             */
            pSelInfo->Sel = Sel;
            selmR3SelInfoFromDesc32(pSelInfo, &Desc);

            return VINF_SUCCESS;
        }
        return rc;
    }

    /*
     * We're in real mode.
     */
    pSelInfo->Sel       = Sel;
    pSelInfo->GCPtrBase = Sel << 4;
    pSelInfo->cbLimit   = 0xffff;
    pSelInfo->fFlags    = DBGFSELINFO_FLAGS_REAL_MODE;
    pSelInfo->u.Raw64.au64[0] = 0;
    pSelInfo->u.Raw64.au64[1] = 0;
    pSelInfo->SelGate   = 0;
    return VINF_SUCCESS;
}


/**
 * Gets information about a selector.
 *
 * Intended for the debugger mostly and will prefer the guest descriptor tables
 * over the shadow ones.
 *
 * @retval  VINF_SUCCESS on success.
 * @retval  VERR_INVALID_SELECTOR if the selector isn't fully inside the
 *          descriptor table.
 * @retval  VERR_SELECTOR_NOT_PRESENT if the LDT is invalid or not present. This
 *          is not returned if the selector itself isn't present, you have to
 *          check that for yourself (see DBGFSELINFO::fFlags).
 * @retval  VERR_PAGE_TABLE_NOT_PRESENT or VERR_PAGE_NOT_PRESENT if the
 *          pagetable or page backing the selector table wasn't present.
 * @returns Other VBox status code on other errors.
 *
 * @param   pVCpu       The cross context virtual CPU structure.
 * @param   Sel         The selector to get info about.
 * @param   pSelInfo    Where to store the information.
 */
VMMR3DECL(int) SELMR3GetSelectorInfo(PVMCPU pVCpu, RTSEL Sel, PDBGFSELINFO pSelInfo)
{
    AssertPtr(pSelInfo);
    if (CPUMIsGuestInLongMode(pVCpu))
        return selmR3GetSelectorInfo64(pVCpu, Sel, pSelInfo);
    return selmR3GetSelectorInfo32(pVCpu, Sel, pSelInfo);
}


/**
 * Formats a descriptor.
 *
 * @param   Desc        Descriptor to format.
 * @param   Sel         Selector number.
 * @param   pszOutput   Output buffer.
 * @param   cchOutput   Size of output buffer.
 */
static void selmR3FormatDescriptor(X86DESC Desc, RTSEL Sel, char *pszOutput, size_t cchOutput)
{
    /*
     * Make variable description string.
     */
    static struct
    {
        unsigned    cch;
        const char *psz;
    } const aTypes[32] =
    {
#define STRENTRY(str) { sizeof(str) - 1, str }
        /* system */
        STRENTRY("Reserved0 "),                  /* 0x00 */
        STRENTRY("TSS16Avail "),                 /* 0x01 */
        STRENTRY("LDT "),                        /* 0x02 */
        STRENTRY("TSS16Busy "),                  /* 0x03 */
        STRENTRY("Call16 "),                     /* 0x04 */
        STRENTRY("Task "),                       /* 0x05 */
        STRENTRY("Int16 "),                      /* 0x06 */
        STRENTRY("Trap16 "),                     /* 0x07 */
        STRENTRY("Reserved8 "),                  /* 0x08 */
        STRENTRY("TSS32Avail "),                 /* 0x09 */
        STRENTRY("ReservedA "),                  /* 0x0a */
        STRENTRY("TSS32Busy "),                  /* 0x0b */
        STRENTRY("Call32 "),                     /* 0x0c */
        STRENTRY("ReservedD "),                  /* 0x0d */
        STRENTRY("Int32 "),                      /* 0x0e */
        STRENTRY("Trap32 "),                     /* 0x0f */
        /* non system */
        STRENTRY("DataRO "),                     /* 0x10 */
        STRENTRY("DataRO Accessed "),            /* 0x11 */
        STRENTRY("DataRW "),                     /* 0x12 */
        STRENTRY("DataRW Accessed "),            /* 0x13 */
        STRENTRY("DataDownRO "),                 /* 0x14 */
        STRENTRY("DataDownRO Accessed "),        /* 0x15 */
        STRENTRY("DataDownRW "),                 /* 0x16 */
        STRENTRY("DataDownRW Accessed "),        /* 0x17 */
        STRENTRY("CodeEO "),                     /* 0x18 */
        STRENTRY("CodeEO Accessed "),            /* 0x19 */
        STRENTRY("CodeER "),                     /* 0x1a */
        STRENTRY("CodeER Accessed "),            /* 0x1b */
        STRENTRY("CodeConfEO "),                 /* 0x1c */
        STRENTRY("CodeConfEO Accessed "),        /* 0x1d */
        STRENTRY("CodeConfER "),                 /* 0x1e */
        STRENTRY("CodeConfER Accessed ")         /* 0x1f */
#undef SYSENTRY
    };
#define ADD_STR(psz, pszAdd) do { strcpy(psz, pszAdd); psz += strlen(pszAdd); } while (0)
    char        szMsg[128];
    char       *psz = &szMsg[0];
    unsigned    i = Desc.Gen.u1DescType << 4 | Desc.Gen.u4Type;
    memcpy(psz, aTypes[i].psz, aTypes[i].cch);
    psz += aTypes[i].cch;

    if (Desc.Gen.u1Present)
        ADD_STR(psz, "Present ");
    else
        ADD_STR(psz, "Not-Present ");
    if (Desc.Gen.u1Granularity)
        ADD_STR(psz, "Page ");
    if (Desc.Gen.u1DefBig)
        ADD_STR(psz, "32-bit ");
    else
        ADD_STR(psz, "16-bit ");
#undef ADD_STR
    *psz = '\0';

    /*
     * Limit and Base and format the output.
     */
    uint32_t    u32Limit = X86DESC_LIMIT_G(&Desc);
    uint32_t    u32Base  = X86DESC_BASE(&Desc);

    RTStrPrintf(pszOutput, cchOutput, "%04x - %08x %08x - base=%08x limit=%08x dpl=%d %s",
                Sel, Desc.au32[0], Desc.au32[1], u32Base, u32Limit, Desc.Gen.u2Dpl, szMsg);
}


/**
 * Dumps a descriptor.
 *
 * @param   Desc    Descriptor to dump.
 * @param   Sel     Selector number.
 * @param   pszMsg  Message to prepend the log entry with.
 */
VMMR3DECL(void) SELMR3DumpDescriptor(X86DESC Desc, RTSEL Sel, const char *pszMsg)
{
#ifdef LOG_ENABLED
    if (LogIsEnabled())
    {
        char szOutput[128];
        selmR3FormatDescriptor(Desc, Sel, &szOutput[0], sizeof(szOutput));
        Log(("%s: %s\n", pszMsg, szOutput));
    }
#else
    RT_NOREF3(Desc, Sel, pszMsg);
#endif
}


/**
 * Display the guest gdt.
 *
 * @param   pVM         The cross context VM structure.
 * @param   pHlp        The info helpers.
 * @param   pszArgs     Arguments, ignored.
 */
static DECLCALLBACK(void) selmR3InfoGdtGuest(PVM pVM, PCDBGFINFOHLP pHlp, const char *pszArgs)
{
    /** @todo SMP support! */
    PVMCPU      pVCpu = VMMGetCpu(pVM);
    CPUMImportGuestStateOnDemand(pVCpu, CPUMCTX_EXTRN_CR0  | CPUMCTX_EXTRN_CR3 | CPUMCTX_EXTRN_CR4
                                      | CPUMCTX_EXTRN_EFER | CPUMCTX_EXTRN_GDTR);

    VBOXGDTR    GDTR;
    CPUMGetGuestGDTR(pVCpu, &GDTR);
    RTGCPTR     GCPtrGDT = GDTR.pGdt;
    unsigned    cGDTs = ((unsigned)GDTR.cbGdt + 1) / sizeof(X86DESC);

    pHlp->pfnPrintf(pHlp, "Guest GDT (GCAddr=%RGv limit=%x):\n", GCPtrGDT, GDTR.cbGdt);
    for (unsigned iGDT = 0; iGDT < cGDTs; iGDT++, GCPtrGDT += sizeof(X86DESC))
    {
        X86DESC GDTE;
        int rc = PGMPhysSimpleReadGCPtr(pVCpu, &GDTE, GCPtrGDT, sizeof(GDTE));
        if (RT_SUCCESS(rc))
        {
            if (GDTE.Gen.u1Present)
            {
                char szOutput[128];
                selmR3FormatDescriptor(GDTE, iGDT << X86_SEL_SHIFT, &szOutput[0], sizeof(szOutput));
                pHlp->pfnPrintf(pHlp, "%s\n", szOutput);
            }
        }
        else if (rc == VERR_PAGE_NOT_PRESENT)
        {
            if ((GCPtrGDT & GUEST_PAGE_OFFSET_MASK) + sizeof(X86DESC) - 1 < sizeof(X86DESC))
                pHlp->pfnPrintf(pHlp, "%04x - page not present (GCAddr=%RGv)\n", iGDT << X86_SEL_SHIFT, GCPtrGDT);
        }
        else
            pHlp->pfnPrintf(pHlp, "%04x - read error rc=%Rrc GCAddr=%RGv\n", iGDT << X86_SEL_SHIFT, rc, GCPtrGDT);
    }
    NOREF(pszArgs);
}


/**
 * Display the guest ldt.
 *
 * @param   pVM         The cross context VM structure.
 * @param   pHlp        The info helpers.
 * @param   pszArgs     Arguments, ignored.
 */
static DECLCALLBACK(void) selmR3InfoLdtGuest(PVM pVM, PCDBGFINFOHLP pHlp, const char *pszArgs)
{
    /** @todo SMP support! */
    PVMCPU   pVCpu = VMMGetCpu(pVM);
    CPUMImportGuestStateOnDemand(pVCpu, CPUMCTX_EXTRN_CR0  | CPUMCTX_EXTRN_CR3  | CPUMCTX_EXTRN_CR4
                                      | CPUMCTX_EXTRN_EFER | CPUMCTX_EXTRN_GDTR | CPUMCTX_EXTRN_LDTR);

    uint64_t GCPtrLdt;
    uint32_t cbLdt;
    RTSEL    SelLdt = CPUMGetGuestLdtrEx(pVCpu, &GCPtrLdt, &cbLdt);
    if (!(SelLdt & X86_SEL_MASK_OFF_RPL))
    {
        pHlp->pfnPrintf(pHlp, "Guest LDT (Sel=%x): Null-Selector\n", SelLdt);
        return;
    }

    pHlp->pfnPrintf(pHlp, "Guest LDT (Sel=%x GCAddr=%RX64 limit=%x):\n", SelLdt, GCPtrLdt, cbLdt);
    unsigned cLdts  = (cbLdt + 1) >> X86_SEL_SHIFT;
    for (unsigned iLdt = 0; iLdt < cLdts; iLdt++, GCPtrLdt += sizeof(X86DESC))
    {
        X86DESC LdtE;
        int rc = PGMPhysSimpleReadGCPtr(pVCpu, &LdtE, GCPtrLdt, sizeof(LdtE));
        if (RT_SUCCESS(rc))
        {
            if (LdtE.Gen.u1Present)
            {
                char szOutput[128];
                selmR3FormatDescriptor(LdtE, (iLdt << X86_SEL_SHIFT) | X86_SEL_LDT, &szOutput[0], sizeof(szOutput));
                pHlp->pfnPrintf(pHlp, "%s\n", szOutput);
            }
        }
        else if (rc == VERR_PAGE_NOT_PRESENT)
        {
            if ((GCPtrLdt & GUEST_PAGE_OFFSET_MASK) + sizeof(X86DESC) - 1 < sizeof(X86DESC))
                pHlp->pfnPrintf(pHlp, "%04x - page not present (GCAddr=%RGv)\n", (iLdt << X86_SEL_SHIFT) | X86_SEL_LDT, GCPtrLdt);
        }
        else
            pHlp->pfnPrintf(pHlp, "%04x - read error rc=%Rrc GCAddr=%RGv\n", (iLdt << X86_SEL_SHIFT) | X86_SEL_LDT, rc, GCPtrLdt);
    }
    NOREF(pszArgs);
}


/**
 * Dumps the guest GDT
 *
 * @param   pVM     The cross context VM structure.
 */
VMMR3DECL(void) SELMR3DumpGuestGDT(PVM pVM)
{
    DBGFR3Info(pVM->pUVM, "gdt", NULL, NULL);
}


/**
 * Dumps the guest LDT
 *
 * @param   pVM     The cross context VM structure.
 */
VMMR3DECL(void) SELMR3DumpGuestLDT(PVM pVM)
{
    DBGFR3Info(pVM->pUVM, "ldt", NULL, NULL);
}

