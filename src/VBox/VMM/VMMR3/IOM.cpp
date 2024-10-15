/* $Id: IOM.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IOM - Input / Output Monitor.
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


/** @page pg_iom        IOM - The Input / Output Monitor
 *
 * The input/output monitor will handle I/O exceptions routing them to the
 * appropriate device. It implements an API to register and deregister virtual
 * I/0 port handlers and memory mapped I/O handlers. A handler is PDM devices
 * and a set of callback functions.
 *
 * @see grp_iom
 *
 *
 * @section sec_iom_rawmode     Raw-Mode
 *
 * In raw-mode I/O port access is trapped (\#GP(0)) by ensuring that the actual
 * IOPL is 0 regardless of what the guest IOPL is. The \#GP handler use the
 * disassembler (DIS) to figure which instruction caused it (there are a number
 * of instructions in addition to the I/O ones) and if it's an I/O port access
 * it will hand it to IOMRCIOPortHandler (via EMInterpretPortIO).
 * IOMRCIOPortHandler will lookup the port in the AVL tree of registered
 * handlers. If found, the handler will be called otherwise default action is
 * taken. (Default action is to write into the void and read all set bits.)
 *
 * Memory Mapped I/O (MMIO) is implemented as a slightly special case of PGM
 * access handlers. An MMIO range is registered with IOM which then registers it
 * with the PGM access handler sub-system. The access handler catches all
 * access and will be called in the context of a \#PF handler. In RC and R0 this
 * handler is iomMmioPfHandler while in ring-3 it's iomR3MmioHandler (although
 * in ring-3 there can be alternative ways). iomMmioPfHandler will attempt to
 * emulate the instruction that is doing the access and pass the corresponding
 * reads / writes to the device.
 *
 * Emulating I/O port access is less complex and should be slightly faster than
 * emulating MMIO, so in most cases we should encourage the OS to use port I/O.
 * Devices which are frequently accessed should register GC handlers to speed up
 * execution.
 *
 *
 * @section sec_iom_hm     Hardware Assisted Virtualization Mode
 *
 * When running in hardware assisted virtualization mode we'll be doing much the
 * same things as in raw-mode. The main difference is that we're running in the
 * host ring-0 context and that we don't get faults (\#GP(0) and \#PG) but
 * exits.
 *
 *
 * @section sec_iom_rem         Recompiled Execution Mode
 *
 * When running in the recompiler things are different. I/O port access is
 * handled by calling IOMIOPortRead and IOMIOPortWrite directly. While MMIO can
 * be handled in one of two ways. The normal way is that we have a registered a
 * special RAM range with the recompiler and in the three callbacks (for byte,
 * word and dword access) we call IOMMMIORead and IOMMMIOWrite directly. The
 * alternative ways that the physical memory access which goes via PGM will take
 * care of it by calling iomR3MmioHandler via the PGM access handler machinery
 * - this shouldn't happen but it is an alternative...
 *
 *
 * @section sec_iom_other       Other Accesses
 *
 * I/O ports aren't really exposed in any other way, unless you count the
 * instruction interpreter in EM, but that's just what we're doing in the
 * raw-mode \#GP(0) case really. Now, it's possible to call IOMIOPortRead and
 * IOMIOPortWrite directly to talk to a device, but this is really bad behavior
 * and should only be done as temporary hacks (the PC BIOS device used to setup
 * the CMOS this way back in the dark ages).
 *
 * MMIO has similar direct routes as the I/O ports and these shouldn't be used
 * for the same reasons and with the same restrictions. OTOH since MMIO is
 * mapped into the physical memory address space, it can be accessed in a number
 * of ways thru PGM.
 *
 *
 * @section sec_iom_logging     Logging Levels
 *
 * Following assignments:
 *      - Level 5 is used for defering I/O port and MMIO writes to ring-3.
 *
 */

/** @todo MMIO - simplifying the device end.
 * - Add a return status for doing DBGFSTOP on access where there are no known
 *   registers.
 * -
 *
 *   */


/*********************************************************************************************************************************
*   Header Files                                                                                                                 *
*********************************************************************************************************************************/
#define LOG_GROUP LOG_GROUP_IOM
#include <VBox/vmm/iom.h>
#include <VBox/vmm/cpum.h>
#include <VBox/vmm/pgm.h>
#include <VBox/sup.h>
#include <VBox/vmm/hm.h>
#include <VBox/vmm/mm.h>
#include <VBox/vmm/stam.h>
#include <VBox/vmm/dbgf.h>
#include <VBox/vmm/pdmapi.h>
#include <VBox/vmm/pdmdev.h>
#include "IOMInternal.h"
#include <VBox/vmm/vm.h>

#include <VBox/param.h>
#include <iprt/assert.h>
#include <iprt/alloc.h>
#include <iprt/string.h>
#include <VBox/log.h>
#include <VBox/err.h>



/**
 * Initializes the IOM.
 *
 * @returns VBox status code.
 * @param   pVM         The cross context VM structure.
 */
VMMR3_INT_DECL(int) IOMR3Init(PVM pVM)
{
    LogFlow(("IOMR3Init:\n"));

    /*
     * Assert alignment and sizes.
     */
    AssertCompileMemberAlignment(VM, iom.s, 32);
    AssertCompile(sizeof(pVM->iom.s) <= sizeof(pVM->iom.padding));
    AssertCompileMemberAlignment(IOM, CritSect, sizeof(uintptr_t));

    /*
     * Initialize the REM critical section.
     */
#ifdef IOM_WITH_CRIT_SECT_RW
    int rc = PDMR3CritSectRwInit(pVM, &pVM->iom.s.CritSect, RT_SRC_POS, "IOM Lock");
#else
    int rc = PDMR3CritSectInit(pVM, &pVM->iom.s.CritSect, RT_SRC_POS, "IOM Lock");
#endif
    AssertRCReturn(rc, rc);

    /*
     * Register the MMIO access handler type.
     */
    rc = PGMR3HandlerPhysicalTypeRegister(pVM, PGMPHYSHANDLERKIND_MMIO, 0 /*fFlags*/,
                                          iomMmioHandlerNew, "MMIO", &pVM->iom.s.hNewMmioHandlerType);
    AssertRCReturn(rc, rc);

    /*
     * Info.
     */
    DBGFR3InfoRegisterInternal(pVM, "ioport", "Dumps all IOPort ranges. No arguments.", &iomR3IoPortInfo);
    DBGFR3InfoRegisterInternal(pVM, "mmio", "Dumps all MMIO ranges. No arguments.", &iomR3MmioInfo);

    /*
     * Statistics (names are somewhat contorted to make the registration
     * sub-trees appear at the end of each group).
     */
    STAM_REG(pVM, &pVM->iom.s.StatIoPortCommits,    STAMTYPE_COUNTER, "/IOM/IoPortCommits",     STAMUNIT_OCCURENCES, "Number of ring-3 I/O port commits.");
    STAM_REG(pVM, &pVM->iom.s.StatIoPortIn,         STAMTYPE_COUNTER, "/IOM/IoPortIN",          STAMUNIT_OCCURENCES, "Number of IN instructions (attempts)");
    STAM_REG(pVM, &pVM->iom.s.StatIoPortInS,        STAMTYPE_COUNTER, "/IOM/IoPortINS",         STAMUNIT_OCCURENCES, "Number of INS instructions (attempts)");
    STAM_REG(pVM, &pVM->iom.s.StatIoPortOutS,       STAMTYPE_COUNTER, "/IOM/IoPortOUT",         STAMUNIT_OCCURENCES, "Number of OUT instructions (attempts)");
    STAM_REG(pVM, &pVM->iom.s.StatIoPortOutS,       STAMTYPE_COUNTER, "/IOM/IoPortOUTS",        STAMUNIT_OCCURENCES, "Number of OUTS instructions (attempts)");

    STAM_REG(pVM, &pVM->iom.s.StatMmioHandlerR3,    STAMTYPE_COUNTER, "/IOM/MmioHandlerR3",     STAMUNIT_OCCURENCES, "Number of calls to iomMmioHandlerNew from ring-3.");
    STAM_REG(pVM, &pVM->iom.s.StatMmioHandlerR0,    STAMTYPE_COUNTER, "/IOM/MmioHandlerR0",     STAMUNIT_OCCURENCES, "Number of calls to iomMmioHandlerNew from ring-0.");
    STAM_REG(pVM, &pVM->iom.s.StatMmioReadsR0ToR3,  STAMTYPE_COUNTER, "/IOM/MmioR0ToR3Reads",   STAMUNIT_OCCURENCES, "Number of reads deferred to ring-3.");
    STAM_REG(pVM, &pVM->iom.s.StatMmioWritesR0ToR3, STAMTYPE_COUNTER, "/IOM/MmioR0ToR3Writes",  STAMUNIT_OCCURENCES, "Number of writes deferred to ring-3.");
    STAM_REG(pVM, &pVM->iom.s.StatMmioCommitsR0ToR3,STAMTYPE_COUNTER, "/IOM/MmioR0ToR3Commits", STAMUNIT_OCCURENCES, "Number of commits deferred to ring-3.");
    STAM_REG(pVM, &pVM->iom.s.StatMmioPfHandler,    STAMTYPE_PROFILE, "/IOM/MmioPfHandler",     STAMUNIT_TICKS_PER_CALL, "Number of calls to iomMmioPfHandlerNew.");
    STAM_REG(pVM, &pVM->iom.s.StatMmioPhysHandler,  STAMTYPE_PROFILE, "/IOM/MmioPhysHandler",   STAMUNIT_TICKS_PER_CALL, "Number of calls to IOMR0MmioPhysHandler.");
    STAM_REG(pVM, &pVM->iom.s.StatMmioCommitsDirect,STAMTYPE_COUNTER, "/IOM/MmioCommitsDirect", STAMUNIT_OCCURENCES, "Number of ring-3 MMIO commits direct to handler via handle hint.");
    STAM_REG(pVM, &pVM->iom.s.StatMmioCommitsPgm,   STAMTYPE_COUNTER, "/IOM/MmioCommitsPgm",    STAMUNIT_OCCURENCES, "Number of ring-3 MMIO commits via PGM.");
    STAM_REL_REG(pVM, &pVM->iom.s.StatMmioStaleMappings,   STAMTYPE_COUNTER, "/IOM/MmioMappingsStale",              STAMUNIT_TICKS_PER_CALL, "Number of times iomMmioHandlerNew got a call for a remapped range at the old mapping.");
    STAM_REL_REG(pVM, &pVM->iom.s.StatMmioTooDeepRecursion, STAMTYPE_COUNTER, "/IOM/MmioTooDeepRecursion",          STAMUNIT_OCCURENCES,     "Number of times iomMmioHandlerNew detected too deep recursion and took default action.");
    STAM_REG(pVM, &pVM->iom.s.StatMmioDevLockContentionR0, STAMTYPE_COUNTER, "/IOM/MmioDevLockContentionR0",        STAMUNIT_OCCURENCES,     "Number of device lock contention force return to ring-3.");

    LogFlow(("IOMR3Init: returns VINF_SUCCESS\n"));
    return VINF_SUCCESS;
}


/**
 * Called when a VM initialization stage is completed.
 *
 * @returns VBox status code.
 * @param   pVM             The cross context VM structure.
 * @param   enmWhat         The initialization state that was completed.
 */
VMMR3_INT_DECL(int) IOMR3InitCompleted(PVM pVM, VMINITCOMPLETED enmWhat)
{
#ifdef VBOX_WITH_STATISTICS
    if (enmWhat == VMINITCOMPLETED_RING0)
    {
        /*
         * Synchronize the ring-3 I/O port and MMIO statistics indices into the
         * ring-0 tables to simplify ring-0 code.  This also make sure that any
         * later calls to grow the statistics tables will fail.
         */
        if (!SUPR3IsDriverless())
        {
            int rc = VMMR3CallR0Emt(pVM, pVM->apCpusR3[0], VMMR0_DO_IOM_SYNC_STATS_INDICES, 0, NULL);
            AssertLogRelRCReturn(rc, rc);
        }

        /*
         * Register I/O port and MMIO stats now that we're done registering MMIO
         * regions and won't grow the table again.
         */
        for (uint32_t i = 0; i < pVM->iom.s.cIoPortRegs; i++)
        {
            PIOMIOPORTENTRYR3 pRegEntry = &pVM->iom.s.paIoPortRegs[i];
            if (   pRegEntry->fMapped
                && pRegEntry->idxStats != UINT16_MAX)
                iomR3IoPortRegStats(pVM, pRegEntry);
        }

        for (uint32_t i = 0; i < pVM->iom.s.cMmioRegs; i++)
        {
            PIOMMMIOENTRYR3 pRegEntry = &pVM->iom.s.paMmioRegs[i];
            if (   pRegEntry->fMapped
                && pRegEntry->idxStats != UINT16_MAX)
                iomR3MmioRegStats(pVM, pRegEntry);
        }
    }
#else
    RT_NOREF(pVM, enmWhat);
#endif

    /*
     * Freeze I/O port and MMIO registrations.
     */
    pVM->iom.s.fIoPortsFrozen = true;
    pVM->iom.s.fMmioFrozen    = true;
    return VINF_SUCCESS;
}


/**
 * The VM is being reset.
 *
 * @param   pVM     The cross context VM structure.
 */
VMMR3_INT_DECL(void) IOMR3Reset(PVM pVM)
{
    RT_NOREF(pVM);
}


/**
 * Applies relocations to data and code managed by this
 * component. This function will be called at init and
 * whenever the VMM need to relocate it self inside the GC.
 *
 * The IOM will update the addresses used by the switcher.
 *
 * @param   pVM     The cross context VM structure.
 * @param   offDelta    Relocation delta relative to old location.
 */
VMMR3_INT_DECL(void) IOMR3Relocate(PVM pVM, RTGCINTPTR offDelta)
{
    RT_NOREF(pVM, offDelta);
}

/**
 * Terminates the IOM.
 *
 * Termination means cleaning up and freeing all resources,
 * the VM it self is at this point powered off or suspended.
 *
 * @returns VBox status code.
 * @param   pVM         The cross context VM structure.
 */
VMMR3_INT_DECL(int) IOMR3Term(PVM pVM)
{
    /*
     * IOM is not owning anything but automatically freed resources,
     * so there's nothing to do here.
     */
    NOREF(pVM);
    return VINF_SUCCESS;
}


/**
 * Handles the unlikely and probably fatal merge cases.
 *
 * @returns Merged status code.
 * @param   rcStrict        Current EM status code.
 * @param   rcStrictCommit  The IOM I/O or MMIO write commit status to merge
 *                          with @a rcStrict.
 * @param   rcIom           For logging purposes only.
 * @param   pVCpu           The cross context virtual CPU structure of the
 *                          calling EMT.  For logging purposes.
 */
DECL_NO_INLINE(static, VBOXSTRICTRC) iomR3MergeStatusSlow(VBOXSTRICTRC rcStrict, VBOXSTRICTRC rcStrictCommit,
                                                          int rcIom, PVMCPU pVCpu)
{
    if (RT_FAILURE_NP(rcStrict))
        return rcStrict;

    if (RT_FAILURE_NP(rcStrictCommit))
        return rcStrictCommit;

    if (rcStrict == rcStrictCommit)
        return rcStrictCommit;

    AssertLogRelMsgFailed(("rcStrictCommit=%Rrc rcStrict=%Rrc IOPort={%#06x<-%#xx/%u} MMIO={%RGp<-%.*Rhxs} (rcIom=%Rrc)\n",
                           VBOXSTRICTRC_VAL(rcStrictCommit), VBOXSTRICTRC_VAL(rcStrict),
                           pVCpu->iom.s.PendingIOPortWrite.IOPort,
                           pVCpu->iom.s.PendingIOPortWrite.u32Value, pVCpu->iom.s.PendingIOPortWrite.cbValue,
                           pVCpu->iom.s.PendingMmioWrite.GCPhys,
                           pVCpu->iom.s.PendingMmioWrite.cbValue, &pVCpu->iom.s.PendingMmioWrite.abValue[0], rcIom));
    return VERR_IOM_FF_STATUS_IPE;
}


/**
 * Helper for IOMR3ProcessForceFlag.
 *
 * @returns Merged status code.
 * @param   rcStrict        Current EM status code.
 * @param   rcStrictCommit  The IOM I/O or MMIO write commit status to merge
 *                          with @a rcStrict.
 * @param   rcIom           Either VINF_IOM_R3_IOPORT_COMMIT_WRITE or
 *                          VINF_IOM_R3_MMIO_COMMIT_WRITE.
 * @param   pVCpu           The cross context virtual CPU structure of the
 *                          calling EMT.
 */
DECLINLINE(VBOXSTRICTRC) iomR3MergeStatus(VBOXSTRICTRC rcStrict, VBOXSTRICTRC rcStrictCommit, int rcIom, PVMCPU pVCpu)
{
    /* Simple. */
    if (RT_LIKELY(rcStrict == rcIom || rcStrict == VINF_EM_RAW_TO_R3 || rcStrict == VINF_SUCCESS))
        return rcStrictCommit;

    if (RT_LIKELY(rcStrictCommit == VINF_SUCCESS))
        return rcStrict;

    /* EM scheduling status codes. */
    if (RT_LIKELY(   rcStrict >= VINF_EM_FIRST
                  && rcStrict <= VINF_EM_LAST))
    {
        if (RT_LIKELY(   rcStrictCommit >= VINF_EM_FIRST
                      && rcStrictCommit <= VINF_EM_LAST))
            return rcStrict < rcStrictCommit ? rcStrict : rcStrictCommit;
    }

    /* Unlikely */
    return iomR3MergeStatusSlow(rcStrict, rcStrictCommit, rcIom, pVCpu);
}


/**
 * Called by force-flag handling code when VMCPU_FF_IOM is set.
 *
 * @returns Merge between @a rcStrict and what the commit operation returned.
 * @param   pVM         The cross context VM structure.
 * @param   pVCpu       The cross context virtual CPU structure of the calling EMT.
 * @param   rcStrict    The status code returned by ring-0 or raw-mode.
 * @thread  EMT(pVCpu)
 *
 * @remarks The VMCPU_FF_IOM flag is handled before the status codes by EM, so
 *          we're very likely to see @a rcStrict set to
 *          VINF_IOM_R3_IOPORT_COMMIT_WRITE and VINF_IOM_R3_MMIO_COMMIT_WRITE
 *          here.
 */
VMMR3_INT_DECL(VBOXSTRICTRC) IOMR3ProcessForceFlag(PVM pVM, PVMCPU pVCpu, VBOXSTRICTRC rcStrict)
{
    VMCPU_FF_CLEAR(pVCpu, VMCPU_FF_IOM);
    Assert(pVCpu->iom.s.PendingIOPortWrite.cbValue || pVCpu->iom.s.PendingMmioWrite.cbValue);

    if (pVCpu->iom.s.PendingIOPortWrite.cbValue)
    {
        Log5(("IOM: Dispatching pending I/O port write: %#x LB %u -> %RTiop\n", pVCpu->iom.s.PendingIOPortWrite.u32Value,
              pVCpu->iom.s.PendingIOPortWrite.cbValue, pVCpu->iom.s.PendingIOPortWrite.IOPort));
        STAM_COUNTER_INC(&pVM->iom.s.StatIoPortCommits);
        VBOXSTRICTRC rcStrictCommit = IOMIOPortWrite(pVM, pVCpu, pVCpu->iom.s.PendingIOPortWrite.IOPort,
                                                     pVCpu->iom.s.PendingIOPortWrite.u32Value,
                                                     pVCpu->iom.s.PendingIOPortWrite.cbValue);
        pVCpu->iom.s.PendingIOPortWrite.cbValue = 0;
        rcStrict = iomR3MergeStatus(rcStrict, rcStrictCommit, VINF_IOM_R3_IOPORT_COMMIT_WRITE, pVCpu);
    }


    if (pVCpu->iom.s.PendingMmioWrite.cbValue)
    {
        Log5(("IOM: Dispatching pending MMIO write: %RGp LB %#x\n",
              pVCpu->iom.s.PendingMmioWrite.GCPhys, pVCpu->iom.s.PendingMmioWrite.cbValue));

        /* Use new MMIO handle hint and bypass PGM if it still looks right. */
        size_t idxMmioRegionHint = pVCpu->iom.s.PendingMmioWrite.idxMmioRegionHint;
        if (idxMmioRegionHint < pVM->iom.s.cMmioRegs)
        {
            PIOMMMIOENTRYR3 pRegEntry    = &pVM->iom.s.paMmioRegs[idxMmioRegionHint];
            RTGCPHYS const GCPhysMapping = pRegEntry->GCPhysMapping;
            RTGCPHYS const offRegion     = pVCpu->iom.s.PendingMmioWrite.GCPhys - GCPhysMapping;
            if (offRegion < pRegEntry->cbRegion && GCPhysMapping != NIL_RTGCPHYS)
            {
                STAM_COUNTER_INC(&pVM->iom.s.StatMmioCommitsDirect);
                VBOXSTRICTRC rcStrictCommit = iomR3MmioCommitWorker(pVM, pVCpu, pRegEntry, offRegion);
                pVCpu->iom.s.PendingMmioWrite.cbValue = 0;
                return iomR3MergeStatus(rcStrict, rcStrictCommit, VINF_IOM_R3_MMIO_COMMIT_WRITE, pVCpu);
            }
        }

        /* Fall back on PGM. */
        STAM_COUNTER_INC(&pVM->iom.s.StatMmioCommitsPgm);
        VBOXSTRICTRC rcStrictCommit = PGMPhysWrite(pVM, pVCpu->iom.s.PendingMmioWrite.GCPhys,
                                                   pVCpu->iom.s.PendingMmioWrite.abValue, pVCpu->iom.s.PendingMmioWrite.cbValue,
                                                   PGMACCESSORIGIN_IOM);
        pVCpu->iom.s.PendingMmioWrite.cbValue = 0;
        rcStrict = iomR3MergeStatus(rcStrict, rcStrictCommit, VINF_IOM_R3_MMIO_COMMIT_WRITE, pVCpu);
    }

    return rcStrict;
}


/**
 * Notification from DBGF that the number of active I/O port or MMIO
 * breakpoints has change.
 *
 * For performance reasons, IOM will only call DBGF before doing I/O and MMIO
 * accesses where there are armed breakpoints.
 *
 * @param   pVM         The cross context VM structure.
 * @param   fPortIo     True if there are armed I/O port breakpoints.
 * @param   fMmio       True if there are armed MMIO breakpoints.
 */
VMMR3_INT_DECL(void) IOMR3NotifyBreakpointCountChange(PVM pVM, bool fPortIo, bool fMmio)
{
    /** @todo I/O breakpoints. */
    RT_NOREF3(pVM, fPortIo, fMmio);
}


/**
 * Notification from DBGF that an event has been enabled or disabled.
 *
 * For performance reasons, IOM may cache the state of events it implements.
 *
 * @param   pVM         The cross context VM structure.
 * @param   enmEvent    The event.
 * @param   fEnabled    The new state.
 */
VMMR3_INT_DECL(void) IOMR3NotifyDebugEventChange(PVM pVM, DBGFEVENT enmEvent, bool fEnabled)
{
    /** @todo IOM debug events. */
    RT_NOREF3(pVM, enmEvent, fEnabled);
}

